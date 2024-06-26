#pragma once

#include <cstdint>

#if __cplusplus >= 202002L
#define OMHTR_CONSTEVAL consteval
#else
#define OMHTR_CONSTEVAL constexpr
#endif

#if defined(__GNUC__) || defined(__clang__)
#define OMHTR_FORCEINLINE __attribute__((always_inline)) inline
#elif defined(_MSC_VER)
#define OMHTR_FORCEINLINE __forceinline
#else
#define OMHTR_FORCEINLINE inline
#endif

namespace OMHTR {
    OMHTR_CONSTEVAL uint32_t fnv_prime = 16777619u;
    OMHTR_CONSTEVAL uint32_t fnv_offset_basis = 2166136261u;

    // Corrected FNV-1 hash function
    OMHTR_CONSTEVAL uint32_t fnv1_32_hash(const char* str, std::size_t length, uint32_t hash = fnv_offset_basis) {
        return length == 0 ? hash : fnv1_32_hash(str + 1, length - 1, (hash ^ static_cast<uint32_t>(*str)) * fnv_prime);
    }

    // Template wrapper to handle string literals
    template <std::size_t N>
    OMHTR_CONSTEVAL uint32_t fnv1_32(const char(&str)[N]) {
        return fnv1_32_hash(str, N - 1);  // N - 1 to exclude the null terminator
    }
}

#define OMHTR_FNV1HASH(x) \
[] OMHTR_FORCEINLINE {\
    OMHTR_CONSTEVAL auto fnv = OMHTR::fnv1_32(x);\
    return fnv;\
}()