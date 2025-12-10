// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2025, Hao Li
 */
#pragma once

#include <bit>
#include <cstdint>
#include <concepts>

namespace pil {

// Compile-time host endianness detection
constexpr bool is_little_endian() noexcept {
    return std::endian::native == std::endian::little;
}

// Byte swap for integral types
template<std::integral T>
constexpr T byteswap(T value) noexcept {
    if constexpr (sizeof(T) == 1) {
        return value;
    } else if constexpr (sizeof(T) == 2) {
        uint16_t v = static_cast<uint16_t>(value);
        return static_cast<T>((v << 8) | (v >> 8));
    } else if constexpr (sizeof(T) == 4) {
        uint32_t v = static_cast<uint32_t>(value);
        return static_cast<T>(
            ((v & 0xFF000000u) >> 24) |
            ((v & 0x00FF0000u) >> 8)  |
            ((v & 0x0000FF00u) << 8)  |
            ((v & 0x000000FFu) << 24)
        );
    } else if constexpr (sizeof(T) == 8) {
        uint64_t v = static_cast<uint64_t>(value);
        return static_cast<T>(
            ((v & 0xFF00000000000000ULL) >> 56) |
            ((v & 0x00FF000000000000ULL) >> 40) |
            ((v & 0x0000FF0000000000ULL) >> 24) |
            ((v & 0x000000FF00000000ULL) >> 8)  |
            ((v & 0x00000000FF000000ULL) << 8)  |
            ((v & 0x0000000000FF0000ULL) << 24) |
            ((v & 0x000000000000FF00ULL) << 40) |
            ((v & 0x00000000000000FFULL) << 56)
        );
    }
}

// Convert from file endianness to host endianness
template<std::integral T>
constexpr T from_file_endian(T value, bool file_is_little_endian) noexcept {
    if (file_is_little_endian == is_little_endian()) {
        return value;
    }
    return byteswap(value);
}

} // namespace pil
