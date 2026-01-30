// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2025, Hao Li
 */
#include "pil_common.hpp"

#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;
namespace pil {

void write_segment_file(const fs::path& mdt_path, size_t segment_index,
                        tcb::span<const uint8_t> data)
{
    auto bxx_path = mdt_path;
    bxx_path.replace_extension(fmt::format(".b{:02d}", segment_index));

    std::ofstream bxx(bxx_path, std::ios::binary | std::ios::trunc);
    if (!bxx) {
        throw_system_error(fmt::format("Failed to create {}", bxx_path.string()));
    }
    bxx.exceptions(std::ios::failbit | std::ios::badbit);
    bxx.write(reinterpret_cast<const char*>(data.data()), data.size());
}

template<typename ElfHeader, typename ElfPhdr>
void split_impl(std::ifstream& mbn, std::ofstream& mdt, const fs::path& mdt_path,
                bool is_little_endian)
{
    auto ehdr = read_elf_header<ElfHeader>(mbn);
    auto phdrs = read_program_headers<ElfHeader, ElfPhdr>(mbn, ehdr, is_little_endian);

    // Write ELF header to mdt
    write_file_at(mdt, 0, tcb::span<const uint8_t>{
        reinterpret_cast<const uint8_t*>(&ehdr),
        sizeof(ElfHeader)
    });

    // Write program headers to mdt
    auto phoff = from_file_endian(ehdr.e_phoff, is_little_endian);
    for (size_t i = 0; i < phdrs.size(); ++i) {
        write_file_at(mdt, phoff + i * sizeof(ElfPhdr), tcb::span<const uint8_t>{
            reinterpret_cast<const uint8_t*>(&phdrs[i]),
            sizeof(ElfPhdr)
        });
    }

    // Process each segment
    for (size_t i = 0; i < phdrs.size(); ++i) {
        auto [p_offset, p_filesz, p_flags] = get_phdr_info(phdrs[i], is_little_endian);

        if (p_filesz == 0) continue;

        auto segment = read_file_at(mbn, p_offset, p_filesz);

        // Write to .bXX file
        write_segment_file(mdt_path, i, segment);

        // Hash segments (type 2) go into mdt after the program headers
        if (is_pil_hash_segment(p_flags)) {
            append_to_file(mdt, segment);
        }
    }
}

void split(const fs::path& mbn_path, const fs::path& mdt_path) {
    if (mdt_path.extension() != ".mdt") {
        throw Error(fmt::format("{} is not a .mdt file", mdt_path.string()));
    }

    std::ifstream mbn(mbn_path, std::ios::binary);
    if (!mbn) {
        throw_system_error(fmt::format("Failed to open {}", mbn_path.string()));
    }
    mbn.exceptions(std::ios::failbit | std::ios::badbit);

    std::ofstream mdt(mdt_path, std::ios::binary | std::ios::trunc);
    if (!mdt) {
        throw_system_error(fmt::format("Failed to create {}", mdt_path.string()));
    }
    mdt.exceptions(std::ios::failbit | std::ios::badbit);

    auto format = detect_elf_format(mbn);

    if (format.elf_class == ELFCLASS32) {
        split_impl<Elf32_Ehdr, Elf32_Phdr>(mbn, mdt, mdt_path, format.is_little_endian);
    } else if (format.elf_class == ELFCLASS64) {
        split_impl<Elf64_Ehdr, Elf64_Phdr>(mbn, mdt, mdt_path, format.is_little_endian);
    }
}

} // namespace pil

int main(int argc, char* argv[]) {
    try {
        if (argc != 3) {
            std::cerr << fmt::format("Usage: {} <mbn input> <mdt output>\n",
                                     fs::path(argv[0]).filename().string());
            return 1;
        }

        pil::split(argv[1], argv[2]);
        return 0;

    } catch (const std::ios_base::failure& e) {
        auto ec = errno ? std::error_code(errno, std::system_category())
                        : std::make_error_code(std::errc::io_error);
        std::cerr << fmt::format("I/O Error: {}\n", ec.message());
        return 1;
    } catch (const std::system_error& e) {
        std::cerr << fmt::format("Error: {} ({})\n", e.what(), e.code().message());
        return 1;
    } catch (const std::exception& e) {
        std::cerr << fmt::format("Error: {}\n", e.what());
        return 1;
    }
}
