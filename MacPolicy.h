#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace macpolicy {
inline int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
// Accept 12 hex digits or exactly six pairs separated by ':' or '-'.
// Blank is an explicit disabled filter, never a wildcard.
inline bool normalize(const char* input, char output[18]) {
    output[0] = 0;
    if (!input) return false;
    size_t n = strlen(input);
    while (n && (input[n - 1] == ' ' || input[n - 1] == '\t')) --n;
    while (n && (*input == ' ' || *input == '\t')) { ++input; --n; }
    if (!n) return true;
    if (n != 12 && n != 17) return false;
    const char sep = n == 17 ? input[2] : 0;
    if (n == 17 && sep != ':' && sep != '-') return false;
    uint8_t bytes[6];
    bool allZero = true, allFF = true;
    for (size_t i = 0; i < 6; ++i) {
        const size_t j = i * (n == 17 ? 3 : 2);
        const int hi = hex(input[j]), lo = hex(input[j + 1]);
        if (hi < 0 || lo < 0 || (n == 17 && i < 5 && input[j + 2] != sep)) return false;
        bytes[i] = uint8_t(hi * 16 + lo);
        if (bytes[i]) allZero = false;
        if (bytes[i] != 255) allFF = false;
    }
    if (allZero || allFF) return false;
    const char* digits = "0123456789ABCDEF";
    for (size_t i = 0; i < 6; ++i) {
        output[i * 3] = digits[bytes[i] >> 4];
        output[i * 3 + 1] = digits[bytes[i] & 15];
        output[i * 3 + 2] = i == 5 ? 0 : ':';
    }
    return true;
}
inline bool allowed(const char* configured, const char* candidate) {
    if (!configured || !configured[0] || !candidate) return false;
    char canonical[18];
    return normalize(candidate, canonical) && strcmp(configured, canonical) == 0;
}
inline uint32_t checksum(const void* bytes, size_t n) {
    const uint8_t* p = static_cast<const uint8_t*>(bytes);
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; ++i) { h ^= p[i]; h *= 16777619u; }
    return h;
}
} // namespace macpolicy
