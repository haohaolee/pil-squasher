// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2025, Hao Li
 */

#include "pil_common.hpp"

#include <iostream>
#include <filesystem>

namespace fs = std::filesystem;
namespace pil {

auto read_segment_data(std::ifstream& mdt, const fs::path& mdt_path,
                       size_t segment_index, size_t filesz,
                       bool is_hash_segment, size_t& hash_offset)
    -> std::vector<uint8_t>
{
    if (is_hash_segment) {
        auto segment = read_file_at(mdt, hash_offset, filesz);
        hash_offset += filesz;
        return segment;
    } else {
        auto bxx_path = mdt_path;
        bxx_path.replace_extension(std::format(".b{:02d}", segment_index));

        std::ifstream bxx(bxx_path, std::ios::binary);
        if (!bxx) {
            throw_system_error(std::format("Failed to open required segment file {}",
                                           bxx_path.string()));
        }
        bxx.exceptions(std::ios::failbit | std::ios::badbit);
        return read_file_at(bxx, 0, filesz);
    }
}

template<typename ElfHeader, typename ElfPhdr>
void squash_impl(std::ifstream& mdt, std::ofstream& mbn, const fs::path& mdt_path,
                 bool is_little_endian)
{
    auto ehdr = read_elf_header<ElfHeader>(mdt);
    auto phdrs = read_program_headers<ElfHeader, ElfPhdr>(mdt, ehdr, is_little_endian);

    write_file_at(mbn, 0, std::span{
        reinterpret_cast<const uint8_t*>(&ehdr),
        sizeof(ElfHeader)
    });

    auto phoff = from_file_endian(ehdr.e_phoff, is_little_endian);
    for (size_t i = 0; i < phdrs.size(); ++i) {
        write_file_at(mbn, phoff + i * sizeof(ElfPhdr), std::span{
            reinterpret_cast<const uint8_t*>(&phdrs[i]),
            sizeof(ElfPhdr)
        });
    }

    // Hash segments are stored sequentially in MDT after the first phdr filesz
    size_t hash_offset = from_file_endian(phdrs[0].p_filesz, is_little_endian);

    for (size_t i = 0; i < phdrs.size(); ++i) {
        auto [p_offset, p_filesz, p_flags] = get_phdr_info(phdrs[i], is_little_endian);

        if (p_filesz == 0) continue;

        auto segment = read_segment_data(mdt, mdt_path, i, p_filesz,
                                        is_pil_hash_segment(p_flags), hash_offset);

        write_file_at(mbn, p_offset, segment);
    }
}

void squash(const fs::path& mdt_path, const fs::path& mbn_path) {
    if (mdt_path.extension() != ".mdt") {
        throw Error(std::format("{} is not a .mdt file", mdt_path.string()));
    }

    std::ifstream mdt(mdt_path, std::ios::binary);
    if (!mdt) {
        throw_system_error(std::format("Failed to open {}", mdt_path.string()));
    }
    mdt.exceptions(std::ios::failbit | std::ios::badbit);

    std::ofstream mbn(mbn_path, std::ios::binary | std::ios::trunc);
    if (!mbn) {
        throw_system_error(std::format("Failed to create {}", mbn_path.string()));
    }
    mbn.exceptions(std::ios::failbit | std::ios::badbit);

    auto format = detect_elf_format(mdt);

    if (format.elf_class == ELFCLASS32) {
        squash_impl<Elf32_Ehdr, Elf32_Phdr>(mdt, mbn, mdt_path, format.is_little_endian);
    } else if (format.elf_class == ELFCLASS64) {
        squash_impl<Elf64_Ehdr, Elf64_Phdr>(mdt, mbn, mdt_path, format.is_little_endian);
    }
}

} // namespace pil

int main(int argc, char* argv[]) {
    try {
        if (argc != 3) {
            std::cerr << std::format("Usage: {} <mbn output> <mdt input>\n",
                                     fs::path(argv[0]).filename().string());
            return 1;
        }

        pil::squash(argv[2], argv[1]);
        return 0;

    } catch (const std::ios_base::failure& e) {
        auto ec = errno ? std::error_code(errno, std::system_category())
                        : std::make_error_code(std::errc::io_error);
        std::cerr << std::format("I/O Error: {}\n", ec.message());
        return 1;
    } catch (const std::system_error& e) {
        std::cerr << std::format("Error: {} ({})\n", e.what(), e.code().message());
        return 1;
    } catch (const std::exception& e) {
        std::cerr << std::format("Error: {}\n", e.what());
        return 1;
    }
}
