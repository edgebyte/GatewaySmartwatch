#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

// Pure C++ decoder. Input is BLE AD data, without gateway event/MAC/RSSI headers.
namespace watchproto {
struct Adv {
    uint8_t heartRate = 255, sbp = 255, dbp = 255;
    uint32_t steps = 0;
    uint16_t sleepMinutes = 0, deepSleepMinutes = 0;
};
struct Rsp {
    uint8_t spo2 = 255;
    uint16_t glucose100 = 65535, bodyTemp10 = 65535, skinTemp10 = 65535;
    bool hasBattery = false, hasSos = false;
    uint8_t battery = 255, sos = 255;
};
struct Packet {
    bool signature = false, fee7 = false, hasAdv = false, hasRsp = false;
    bool malformed = false, hasName = false;
    uint8_t manufacturerMac[6] = {};
    char name[32] = {};
    Adv adv;
    Rsp rsp;
};
inline uint16_t le16(const uint8_t* p) {
    return uint16_t(p[0]) | (uint16_t(p[1]) << 8);
}
inline uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

// Parse each ADV/RSP segment independently so a corrupt length cannot cross
// the boundary. A malformed segment contributes no partial health values.
inline Packet parse(const uint8_t* bytes, size_t size) {
    Packet p;
    if (!bytes && size) { p.malformed = true; return p; }
    for (size_t i = 0; i < size;) {
        const size_t len = bytes[i];
        if (len == 0) {
            for (size_t j = i; j < size; ++j) {
                if (bytes[j] != 0) { p.malformed = true; break; }
            }
            break;
        }
        if (len > size - i - 1) { p.malformed = true; break; }
        const uint8_t type = bytes[i + 1];
        const uint8_t* d = bytes + i + 2;
        const size_t n = len - 1;
        switch (type) {
        case 0xFF:
            if (n == 8 && d[0] == 0xF8 && d[1] == 0xF8) {
                p.signature = true;
                memcpy(p.manufacturerMac, d + 2, 6);
            }
            break;
        case 0x02: case 0x03:
            if (n % 2) { p.malformed = true; break; }
            for (size_t j = 0; j + 1 < n; j += 2)
                if (le16(d + j) == 0xFEE7) p.fee7 = true;
            break;
        case 0x08: case 0x09: {
            const size_t take = n < sizeof(p.name) - 1 ? n : sizeof(p.name) - 1;
            memcpy(p.name, d, take);
            p.name[take] = 0;
            p.hasName = true;
            break;
        }
        case 0xAA:
            // 0C AA HR STEPS[4] SLEEP[2] DEEP[2] SBP DBP
            if (n != 11 || p.hasAdv) { p.malformed = true; break; }
            p.adv.heartRate = d[0];
            p.adv.steps = le32(d + 1);
            p.adv.sleepMinutes = le16(d + 5);
            p.adv.deepSleepMinutes = le16(d + 7);
            p.adv.sbp = d[9]; p.adv.dbp = d[10];
            p.hasAdv = true;
            break;
        case 0xBB:
            // Captured packet has 7 bytes. Optional 8th/9th bytes follow table
            // order only if the AD length includes them (unverified extension).
            if (n < 7 || n > 9 || p.hasRsp) { p.malformed = true; break; }
            p.rsp.spo2 = d[0];
            p.rsp.glucose100 = le16(d + 1);
            p.rsp.bodyTemp10 = le16(d + 3);
            p.rsp.skinTemp10 = le16(d + 5);
            if (n >= 8) { p.rsp.hasBattery = true; p.rsp.battery = d[7]; }
            if (n >= 9) { p.rsp.hasSos = true; p.rsp.sos = d[8]; }
            p.hasRsp = true;
            break;
        default: break;  // Other, well-formed AD fields are allowed.
        }
        i += len + 1;
    }
    if (p.malformed) {
        p.signature = p.fee7 = p.hasAdv = p.hasRsp = p.hasName = false;
    }
    return p;
}
}  // namespace watchproto
