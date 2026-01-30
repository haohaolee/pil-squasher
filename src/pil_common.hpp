// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2025, Hao Li
 */
#ifndef PIL_COMMON_HPP
#define PIL_COMMON_HPP

#include <fstream>
#include <vector>
#include <array>
#include <system_error>
#include <cstring>
#include <cerrno>
#include <type_traits>

#include <fmt/format.h>
#include <tcb/span.hpp>

#include "elf.h"
#include "endian_utils.hpp"

namespace pil {

// C++17 polyfill for std::bit_cast
template<typename To, typename From>
To bit_cast(const From& src) noexcept {
    static_assert(sizeof(To) == sizeof(From), "sizes must match");
    static_assert(std::is_trivially_copyable<To>::value, "To must be trivially copyable");
    static_assert(std::is_trivially_copyable<From>::value, "From must be trivially copyable");
    To dst;
    std::memcpy(&dst, &src, sizeof(To));
    return dst;
}

// Qualcomm PIL segment type in p_flags (bits 24-26)
// This is a Qualcomm-specific extension, not part of standard ELF
constexpr uint32_t PIL_SEGMENT_TYPE_SHIFT = 24;
constexpr uint32_t PIL_SEGMENT_TYPE_MASK = 7;
constexpr uint32_t PIL_SEGMENT_TYPE_HASH = 2;

inline bool is_pil_hash_segment(uint32_t p_flags) {
    return ((p_flags >> PIL_SEGMENT_TYPE_SHIFT) & PIL_SEGMENT_TYPE_MASK)
           == PIL_SEGMENT_TYPE_HASH;
}

// Error handling

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

// File I/O utilities

inline auto read_file_at(std::ifstream& file, size_t offset, size_t size)
    -> std::vector<uint8_t>
{
    std::vector<uint8_t> buffer(size);

    file.seekg(offset);
    file.read(reinterpret_cast<char*>(buffer.data()), size);

    if (file.gcount() != static_cast<std::streamsize>(size)) {
        throw Error(fmt::format("Incomplete read: expected {} bytes, got {} bytes at offset {}",
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
        throw Error(fmt::format("Incomplete read: expected {} bytes, got {} bytes at offset {}",
                               sizeof(T), file.gcount(), offset));
    }

    return bit_cast<T>(raw);
}

inline void write_file_at(std::ofstream& file, size_t offset, tcb::span<const uint8_t> data) {
    file.seekp(offset);
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
}

inline void append_to_file(std::ofstream& file, tcb::span<const uint8_t> data) {
    file.seekp(0, std::ios::end);
    file.write(reinterpret_cast<const char*>(data.data()), data.size());
}

// ELF parsing utilities

struct ElfFormat {
    uint8_t elf_class;
    bool is_little_endian;
};

inline ElfFormat detect_elf_format(std::ifstream& file) {
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
        throw Error(fmt::format("Unsupported ELF class {}", e_ident[EI_CLASS]));
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

// Common file writing helpers

inline void write_elf_header_and_phdrs(std::ofstream& out,
                                       tcb::span<const uint8_t> ehdr_bytes,
                                       size_t phoff,
                                       tcb::span<const uint8_t> phdrs_bytes,
                                       size_t phdr_size)
{
    write_file_at(out, 0, ehdr_bytes);

    size_t num_phdrs = phdrs_bytes.size() / phdr_size;
    for (size_t i = 0; i < num_phdrs; ++i) {
        write_file_at(out, phoff + i * phdr_size,
                      phdrs_bytes.subspan(i * phdr_size, phdr_size));
    }
}

} // namespace pil

#endif // PIL_COMMON_HPP
