#pragma once
#include <stdint.h>
#include "WatchProtocol.h"

// Types in a header keep Arduino's generated function prototypes valid.
struct ScanEvent {
    uint32_t receivedMs;
    int rssi;
    uint8_t addressType;
    char mac[18];
    uint8_t size, advSize;
    uint8_t bytes[62];
};
struct WatchState {
    bool used = false, haveAdv = false, haveRsp = false, dirty = false;
    bool urgent = false, offline = false;
    char mac[18] = {}, name[32] = {};
    uint8_t addressType = 0;
    int rssi = 0;
    uint32_t lastSeen = 0, advMs = 0, rspMs = 0, printMs = 0;
    watchproto::Adv adv;
    watchproto::Rsp rsp;
};
