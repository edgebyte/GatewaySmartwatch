#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace watchproto {

struct Adv {
    uint8_t heartRate = 255;
    uint8_t sbp = 255;
    uint8_t dbp = 255;

    uint32_t steps = 0;

    uint16_t sleepMinutes = 0;
    uint16_t deepSleepMinutes = 0;
};

struct Rsp {
    uint8_t spo2 = 255;

    uint16_t glucose100 = 65535;
    uint16_t bodyTemp10 = 65535;
    uint16_t skinTemp10 = 65535;

    bool hasBattery = false;
    bool hasSos = false;

    uint8_t battery = 255;
    uint8_t sos = 255;
};

struct Packet {
    bool signature = false;
    bool fee7 = false;
    bool hasAdv = false;
    bool hasRsp = false;
    bool malformed = false;
    bool hasName = false;

    uint8_t manufacturerMac[6] = {};
    char name[32] = {};

    Adv adv;
    Rsp rsp;
};

inline uint16_t le16(const uint8_t* p) {
    return uint16_t(p[0]) |
           (uint16_t(p[1]) << 8);
}

inline uint32_t le32(const uint8_t* p) {
    return uint32_t(p[0]) |
           (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) |
           (uint32_t(p[3]) << 24);
}

// รับเฉพาะข้อมูล BLE AD ไม่รวม header ของ Gateway
inline Packet parse(const uint8_t* bytes, size_t size) {
    Packet p;

    if (!bytes && size) {
        p.malformed = true;
        return p;
    }

    for (size_t i = 0; i < size;) {
        const size_t len = bytes[i];

        if (len == 0) {
            for (size_t j = i; j < size; ++j) {
                if (bytes[j] != 0) {
                    p.malformed = true;
                    break;
                }
            }
            break;
        }

        if (len > size - i - 1) {
            p.malformed = true;
            break;
        }

        const uint8_t type = bytes[i + 1];
        const uint8_t* d = bytes + i + 2;
        const size_t n = len - 1;

        switch (type) {
            case 0xFF:
                if (n == 8 &&
                    d[0] == 0xF8 &&
                    d[1] == 0xF8) {
                    p.signature = true;
                    memcpy(p.manufacturerMac, d + 2, 6);
                }
                break;

            case 0x02:
            case 0x03:
                if (n % 2) {
                    p.malformed = true;
                    break;
                }

                for (size_t j = 0; j + 1 < n; j += 2) {
                    if (le16(d + j) == 0xFEE7) {
                        p.fee7 = true;
                    }
                }
                break;

            case 0x08:
            case 0x09: {
                const size_t take =
                    n < sizeof(p.name) - 1
                    ? n
                    : sizeof(p.name) - 1;

                memcpy(p.name, d, take);
                p.name[take] = 0;
                p.hasName = true;
                break;
            }

            case 0xAA:
                // HR 1 + Steps 4 + Sleep 2 + Deep sleep 2
                // + SBP 1 + DBP 1 = 11 bytes
                if (n != 11 || p.hasAdv) {
                    p.malformed = true;
                    break;
                }

                p.adv.heartRate = d[0];
                p.adv.steps = le32(d + 1);
                p.adv.sleepMinutes = le16(d + 5);
                p.adv.deepSleepMinutes = le16(d + 7);
                p.adv.sbp = d[9];
                p.adv.dbp = d[10];

                p.hasAdv = true;
                break;

            case 0xBB:
                // ตัวอย่างจริงมีข้อมูล 7 bytes
                // Battery/SOS รองรับเมื่ออยู่ใน AD field
                // ที่ขยายความยาวแล้วเท่านั้น
                // รูปแบบส่วนขยายยังต้องยืนยันกับแพ็กเก็ตจริง
                if (n < 7 || n > 9 || p.hasRsp) {
                    p.malformed = true;
                    break;
                }

                p.rsp.spo2 = d[0];
                p.rsp.glucose100 = le16(d + 1);
                p.rsp.bodyTemp10 = le16(d + 3);
                p.rsp.skinTemp10 = le16(d + 5);

                if (n >= 8) {
                    p.rsp.hasBattery = true;
                    p.rsp.battery = d[7];
                }

                if (n >= 9) {
                    p.rsp.hasSos = true;
                    p.rsp.sos = d[8];
                }

                p.hasRsp = true;
                break;

            default:
                break;
        }

        i += len + 1;
    }

    // ไม่ส่งต่อข้อมูลสุขภาพบางส่วนจากแพ็กเก็ตผิดรูป
    if (p.malformed) {
        p.signature = false;
        p.fee7 = false;
        p.hasAdv = false;
        p.hasRsp = false;
        p.hasName = false;
    }

    return p;
}

}  // namespace watchproto