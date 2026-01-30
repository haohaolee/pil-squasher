// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2025, Hao Li
 */
#pragma once

#include <cstdint>
#include <type_traits>

namespace pil {

namespace detail {
    enum class endian {
        little,
        big,
        native =
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && defined(__ORDER_BIG_ENDIAN__)
            // GCC, Clang, ICC
            (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__) ? little :
            (__BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)    ? big : little
#elif defined(_WIN32) || defined(_WIN64)
            // MSVC - Windows only supports little endian
            little
#elif defined(__LITTLE_ENDIAN__) || defined(__ARMEL__) || defined(__THUMBEL__) || \
      defined(__AARCH64EL__) || defined(_MIPSEL) || defined(__MIPSEL) || defined(__MIPSEL__)
            little
#elif defined(__BIG_ENDIAN__) || defined(__ARMEB__) || defined(__THUMBEB__) || \
      defined(__AARCH64EB__) || defined(_MIPSEB) || defined(__MIPSEB) || defined(__MIPSEB__)
            big
#else
            // Default to little endian (most modern systems are LE)
            little
#endif
    };
} // namespace detail

// Compile-time host endianness detection
constexpr bool is_little_endian() noexcept {
    return detail::endian::native == detail::endian::little;
}

// Byte swap for integral types
template<typename T>
constexpr T byteswap(T value) noexcept {
    static_assert(std::is_integral<T>::value, "T must be an integral type");

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
template<typename T>
constexpr T from_file_endian(T value, bool file_is_little_endian) noexcept {
    static_assert(std::is_integral<T>::value, "T must be an integral type");

    if (file_is_little_endian == is_little_endian()) {
        return value;
    }
    return byteswap(value);
}

} // namespace pil
