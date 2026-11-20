#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "MacPolicy.h"

struct StoredConfig {
    uint32_t magic = 0x47574332;
    char target[18] = {};
    char ssid[33] = {};
    char wifiPass[65] = {};
    char accessPass[33] = {};
    uint32_t checksum = 0;
};
inline bool validAccessPassword(const char* pass) {
    const size_t n = strlen(pass);
    if (n < 8 || n > 32) return false;
    for (size_t i = 0; i < n; ++i)
        if (static_cast<unsigned char>(pass[i]) < 33 || static_cast<unsigned char>(pass[i]) > 126) return false;
    return true;
}
inline bool validWiFiPassword(const char* pass) {
    const size_t n = strlen(pass);
    if (n == 0 || (n >= 8 && n <= 63)) return true;
    if (n != 64) return false;
    for (size_t i = 0; i < n; ++i) if (macpolicy::hex(pass[i]) < 0) return false;
    return true;
}
inline void sealConfig(StoredConfig& c) {
    c.checksum = macpolicy::checksum(&c, offsetof(StoredConfig, checksum));
}
inline bool validStoredConfig(const StoredConfig& c) {
    if (c.magic != 0x47574332 ||
        !memchr(c.target, 0, sizeof c.target) || !memchr(c.ssid, 0, sizeof c.ssid) ||
        !memchr(c.wifiPass, 0, sizeof c.wifiPass) || !memchr(c.accessPass, 0, sizeof c.accessPass)) return false;
    char normal[18];
    return c.checksum == macpolicy::checksum(&c, offsetof(StoredConfig, checksum)) &&
           macpolicy::normalize(c.target, normal) && strcmp(c.target, normal) == 0 &&
           validAccessPassword(c.accessPass) && validWiFiPassword(c.wifiPass);
}
