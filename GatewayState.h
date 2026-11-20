#pragma once

#include <stdint.h>
#include "WatchProtocol.h"

struct ScanEvent {
    uint32_t receivedMs;
    int rssi;
    uint8_t addressType;

    char mac[18];

    uint8_t size;
    uint8_t advSize;
    uint8_t bytes[62];
};

struct WatchState {
    bool used = false;
    bool haveAdv = false;
    bool haveRsp = false;
    bool dirty = false;
    bool urgent = false;
    bool offline = false;

    char mac[18] = {};
    char name[32] = {};

    uint8_t addressType = 0;
    int rssi = 0;

    uint32_t lastSeen = 0;
    uint32_t advMs = 0;
    uint32_t rspMs = 0;
    uint32_t printMs = 0;

    watchproto::Adv adv;
    watchproto::Rsp rsp;
};