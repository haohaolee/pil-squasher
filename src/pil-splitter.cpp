// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2025, Hao Li
 */
#include <fstream>
#include <vector>
#include <array>
#include <filesystem>
#include <system_error>
#include <format>
#include <span>
#include <iostream>
#include <bit>
#include <cstring>
#include <cstdlib>
#include <cerrno>

#include "elf.h"
#include "endian_utils.hpp"

namespace fs = std::filesystem;

namespace pil {

constexpr uint32_t PIL_SEGMENT_TYPE_SHIFT = 24;
constexpr uint32_t PIL_SEGMENT_TYPE_MASK = 7;
constexpr uint32_t PIL_SEGMENT_TYPE_HASH = 2;

inline bool is_pil_hash_segment(uint32_t p_flags) {
    return ((p_flags >> PIL_SEGMENT_TYPE_SHIFT) & PIL_SEGMENT_TYPE_MASK)
           == PIL_SEGMENT_TYPE_HASH;
}

struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

[[noreturn]] inline void throw_system_error(std::string_view message) {
    if (errno != 0) {
        throw std::system_error(errno, std::system_category(), std::string(message));
    } else {
        throw std::system_error(std::make_error_code(std::errc::io_error), std::string(message));
    }
}

auto read_file_at(std::ifstream& file, size_t offset, size_t size)
    -> std::vector<uint8_t>
{
    std::vector<uint8_t> buffer(size);

    file.seekg(offset);
    file.read(reinterpret_cast<char*>(buffer.data()), size);

    if (file.gcount() != static_cast<std::streamsize>(size)) {
        throw Error(std::format("Incomplete read: expected {} bytes, got {} bytes at offset {}",
                               size, file.gcount(), offset));
    }

    return buffer;
}

template<typename T>
auto read_struct_at(std::ifstream& file, size_t offset) -> T {
    std::array<uint8_t, sizeof(T)> raw;

    file.seekg(offset);
    file.read(reinterpret_cast<char*>(raw.data()), sizeof(T));

    if (file.gcount() != sizeof(T)) {
        throw Error(std::format("Incomplete read: expected {} bytes, got {} bytes at offset {}",
                               sizeof(T), file.gcount(), offset));
    }

    return std::bit_cast<T>(raw);
}

void write_file_at(std::ofstream& file, size_t offset, std::span<const uint8_t> data) {
    file.seekp(offset);
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
}

void append_to_file(std::ofstream& file, std::span<const uint8_t> data) {
    file.seekp(0, std::ios::end);
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
}

struct ElfFormat {
    uint8_t elf_class;
    bool is_little_endian;
};

ElfFormat detect_elf_format(std::ifstream& file) {
    uint8_t e_ident[EI_NIDENT];
    file.seekg(0);
    file.read(reinterpret_cast<char*>(e_ident), EI_NIDENT);

    if (std::memcmp(e_ident, ELFMAG, SELFMAG) != 0) {
        throw Error("Not a valid ELF file");
    }

    bool is_little_endian;
    if (e_ident[EI_DATA] == ELFDATA2LSB) {
        is_little_endian = true;
    } else if (e_ident[EI_DATA] == ELFDATA2MSB) {
        is_little_endian = false;
    } else {
        throw Error("Unknown ELF data encoding");
    }

    if (e_ident[EI_CLASS] != ELFCLASS32 && e_ident[EI_CLASS] != ELFCLASS64) {
        throw Error(std::format("Unsupported ELF class {}", e_ident[EI_CLASS]));
    }

    return ElfFormat{e_ident[EI_CLASS], is_little_endian};
}

template<typename ElfHeader>
ElfHeader read_elf_header(std::ifstream& file) {
    return read_struct_at<ElfHeader>(file, 0);
}

template<typename ElfHeader, typename ElfPhdr>
auto read_program_headers(std::ifstream& file, const ElfHeader& ehdr, bool is_little_endian)
    -> std::vector<ElfPhdr>
{
    auto phoff = from_file_endian(ehdr.e_phoff, is_little_endian);
    auto phnum = from_file_endian(ehdr.e_phnum, is_little_endian);

    std::vector<ElfPhdr> phdrs(phnum);
    for (size_t i = 0; i < phnum; ++i) {
        phdrs[i] = read_struct_at<ElfPhdr>(file, phoff + i * sizeof(ElfPhdr));
    }

    return phdrs;
}

template<typename ElfPhdr>
struct PhdrInfo {
    decltype(ElfPhdr::p_offset) offset;
    decltype(ElfPhdr::p_filesz) filesz;
    decltype(ElfPhdr::p_flags) flags;
};

template<typename ElfPhdr>
PhdrInfo<ElfPhdr> get_phdr_info(const ElfPhdr& phdr, bool is_little_endian) {
    return PhdrInfo<ElfPhdr>{
        from_file_endian(phdr.p_offset, is_little_endian),
        from_file_endian(phdr.p_filesz, is_little_endian),
        from_file_endian(phdr.p_flags, is_little_endian)
    };
}

void write_segment_file(const fs::path& mdt_path, size_t segment_index,
                        std::span<const uint8_t> data)
{
    auto bxx_path = mdt_path;
    bxx_path.replace_extension(std::format(".b{:02d}", segment_index));

    std::ofstream bxx(bxx_path, std::ios::binary | std::ios::trunc);
    if (!bxx) {
        throw_system_error(std::format("Failed to create {}", bxx_path.string()));
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
    write_file_at(mdt, 0, std::span{
        reinterpret_cast<const uint8_t*>(&ehdr),
        sizeof(ElfHeader)
    });

    // Write program headers to mdt
    auto phoff = from_file_endian(ehdr.e_phoff, is_little_endian);
    for (size_t i = 0; i < phdrs.size(); ++i) {
        write_file_at(mdt, phoff + i * sizeof(ElfPhdr), std::span{
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
        throw Error(std::format("{} is not a .mdt file", mdt_path.string()));
    }

    std::ifstream mbn(mbn_path, std::ios::binary);
    if (!mbn) {
        throw_system_error(std::format("Failed to open {}", mbn_path.string()));
    }
    mbn.exceptions(std::ios::failbit | std::ios::badbit);

    std::ofstream mdt(mdt_path, std::ios::binary | std::ios::trunc);
    if (!mdt) {
        throw_system_error(std::format("Failed to create {}", mdt_path.string()));
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
            std::cerr << std::format("Usage: {} <mbn input> <mdt output>\n",
                                     fs::path(argv[0]).filename().string());
            return 1;
        }

        pil::split(argv[1], argv[2]);
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
