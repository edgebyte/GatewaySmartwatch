/*
 * GatewaySmartwatch v1.0
 *
 * Board: ESP32-S3
 * Framework: Arduino-ESP32 3.x
 * Library: NimBLE-Arduino 2.3.6
 *
 * UART0:
 * GPIO43 TX -> USB-to-UART RX
 * GPIO44 RX -> USB-to-UART TX
 * GND       -> USB-to-UART GND
 *
 * USB CDC On Boot: Disabled
 * Serial Monitor: 115200 baud
 */

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <atomic>
#include <strings.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include "Config.h"
#include "WatchProtocol.h"
#include "GatewayState.h"

static QueueHandle_t scanQueue;
static WatchState watches[MAX_WATCHES];

static std::atomic<uint32_t> dropped{0};

static uint32_t malformedCount = 0;
static uint32_t capacityDrops = 0;

// คัดลอกข้อมูลออกจาก callback โดยไม่พิมพ์ Serial ใน callback
static void enqueue(const NimBLEAdvertisedDevice* dev) {
    const auto& payload = dev->getPayload();

    if (payload.empty() || payload.size() > 62) {
        return;
    }

    ScanEvent e{};

    e.receivedMs = millis();
    e.rssi = dev->getRSSI();
    e.addressType = dev->getAddressType();

    snprintf(
        e.mac,
        sizeof(e.mac),
        "%s",
        dev->getAddress().toString().c_str()
    );

    if (TARGET_MAC[0] &&
        strcasecmp(TARGET_MAC, e.mac) != 0) {
        return;
    }

    e.size = static_cast<uint8_t>(payload.size());

    const size_t advLen = dev->getAdvLength();

    if (advLen > payload.size() ||
        advLen > 31 ||
        payload.size() - advLen > 31) {
        return;
    }

    e.advSize = static_cast<uint8_t>(advLen);

    memcpy(e.bytes, payload.data(), e.size);

    if (xQueueSend(scanQueue, &e, 0) != pdTRUE) {
        dropped.fetch_add(1);
    }
}

class ScanCallbacks : public NimBLEScanCallbacks {
    void onDiscovered(
        const NimBLEAdvertisedDevice* dev
    ) override {
        // รับ ADV ก่อน แม้ไม่มี Scan Response
        enqueue(dev);
    }

    void onResult(
        const NimBLEAdvertisedDevice* dev
    ) override {
        // NimBLE รวม ADV และ Scan Response ไว้ใน payload
        enqueue(dev);
    }
};

static ScanCallbacks scanCallbacks;

static void printString(const char* str) {
    Serial.print('"');

    for (
        const unsigned char* p =
            reinterpret_cast<const unsigned char*>(str);
        *p;
        ++p
    ) {
        if (*p == '"' || *p == '\\') {
            Serial.print('\\');
            Serial.write(*p);
        } else if (*p < 32 || *p >= 127) {
            Serial.printf("\\u%04x", *p);
        } else {
            Serial.write(*p);
        }
    }

    Serial.print('"');
}

static void number(bool valid, uint32_t value) {
    if (valid) {
        Serial.print(value);
    } else {
        Serial.print("null");
    }
}

static void scaled(
    bool valid,
    uint16_t value,
    unsigned divisor
) {
    if (!valid) {
        Serial.print("null");
        return;
    }

    if (divisor == 100) {
        Serial.printf(
            "%u.%02u",
            value / 100,
            value % 100
        );
    } else {
        Serial.printf(
            "%u.%u",
            value / 10,
            value % 10
        );
    }
}

static void printWatch(WatchState& w, uint32_t now) {
    const bool freshAdv =
        w.haveAdv &&
        uint32_t(now - w.advMs) <= PACKET_STALE_MS;

    const bool freshRsp =
        w.haveRsp &&
        uint32_t(now - w.rspMs) <= PACKET_STALE_MS;

    Serial.print("{\"type\":\"watch\",\"mac\":");
    printString(w.mac);

    Serial.printf(
        ",\"address_type\":%u,\"name\":",
        w.addressType
    );
    printString(w.name);

    Serial.printf(
        ",\"uptime_ms\":%lu"
        ",\"rssi_dbm\":%d"
        ",\"online\":%s",
        static_cast<unsigned long>(now),
        w.rssi,
        w.offline ? "false" : "true"
    );

    Serial.print(",\"adv_rx_age_ms\":");
    number(w.haveAdv, uint32_t(now - w.advMs));

    Serial.print(",\"rsp_rx_age_ms\":");
    number(w.haveRsp, uint32_t(now - w.rspMs));

    Serial.print(",\"heart_rate_bpm\":");
    number(
        freshAdv && w.adv.heartRate != 255,
        w.adv.heartRate
    );

    Serial.print(",\"steps\":");
    number(
        freshAdv && w.adv.steps != 0,
        w.adv.steps
    );

    Serial.print(",\"sleep_min\":");
    number(
        freshAdv && w.adv.sleepMinutes != 0,
        w.adv.sleepMinutes
    );

    Serial.print(",\"deep_sleep_min\":");
    number(
        freshAdv && w.adv.deepSleepMinutes != 0,
        w.adv.deepSleepMinutes
    );

    Serial.print(",\"sbp_mmhg\":");
    number(
        freshAdv && w.adv.sbp != 255,
        w.adv.sbp
    );

    Serial.print(",\"dbp_mmhg\":");
    number(
        freshAdv && w.adv.dbp != 255,
        w.adv.dbp
    );

    Serial.print(",\"spo2_pct\":");
    number(
        freshRsp && w.rsp.spo2 != 255,
        w.rsp.spo2
    );

    Serial.print(",\"glucose_mmol_l\":");
    scaled(
        freshRsp && w.rsp.glucose100 != 65535,
        w.rsp.glucose100,
        100
    );

    Serial.print(",\"body_temp_c\":");
    scaled(
        freshRsp && w.rsp.bodyTemp10 != 65535,
        w.rsp.bodyTemp10,
        10
    );

    Serial.print(",\"skin_temp_c\":");
    scaled(
        freshRsp && w.rsp.skinTemp10 != 65535,
        w.rsp.skinTemp10,
        10
    );

    Serial.print(",\"battery_pct\":");
    number(
        freshRsp &&
        w.rsp.hasBattery &&
        w.rsp.battery >= 1 &&
        w.rsp.battery <= 100,
        w.rsp.battery
    );

    Serial.print(",\"sos\":");

    if (freshRsp &&
        w.rsp.hasSos &&
        w.rsp.sos <= 1) {
        Serial.print(w.rsp.sos ? "true" : "false");
    } else {
        Serial.print("null");
    }

    Serial.println('}');

    w.printMs = now;
    w.dirty = false;
    w.urgent = false;
}

static void printRaw(const ScanEvent& e) {
    Serial.print("{\"type\":\"raw\",\"mac\":");
    printString(e.mac);

    Serial.printf(
        ",\"address_type\":%u"
        ",\"rssi_dbm\":%d"
        ",\"adv_bytes\":%u"
        ",\"hex\":\"",
        e.addressType,
        e.rssi,
        e.advSize
    );

    for (size_t i = 0; i < e.size; ++i) {
        Serial.printf("%02X", e.bytes[i]);
    }

    Serial.println("\"}");
}

static WatchState* findWatch(const ScanEvent& e) {
    for (auto& w : watches) {
        if (w.used &&
            w.addressType == e.addressType &&
            strcmp(w.mac, e.mac) == 0) {
            return &w;
        }
    }

    return nullptr;
}

static WatchState* allocateWatch(const ScanEvent& e) {
    WatchState* slot = nullptr;

    for (auto& w : watches) {
        if (!w.used) {
            slot = &w;
            break;
        }
    }

    if (!slot) {
        // ใช้ช่องเดิมได้เมื่อนาฬิกาเรือนนั้นหายเกิน 5 นาที
        for (auto& w : watches) {
            if (uint32_t(e.receivedMs - w.lastSeen) > 300000) {
                slot = &w;
                break;
            }
        }
    }

    if (!slot) {
        ++capacityDrops;
        return nullptr;
    }

    *slot = WatchState{};

    slot->used = true;

    memcpy(slot->mac, e.mac, sizeof(slot->mac));

    slot->addressType = e.addressType;
    slot->printMs = e.receivedMs - PRINT_INTERVAL_MS;

    return slot;
}

static void processEvent(const ScanEvent& e) {
    const auto adv = watchproto::parse(
        e.bytes,
        e.advSize
    );

    const auto rsp = watchproto::parse(
        e.bytes + e.advSize,
        e.size - e.advSize
    );

    if (adv.malformed || rsp.malformed) {
        ++malformedCount;
    }

    WatchState* w = findWatch(e);

    const bool matches =
        adv.signature &&
        adv.fee7 &&
        adv.hasAdv;

    if (PRINT_ALL_BLE ||
        (PRINT_RAW_MATCHED && (matches || w))) {
        printRaw(e);
    }

    if (!w && !matches) {
        return;
    }

    if (!w) {
        w = allocateWatch(e);
    }

    if (!w) {
        return;
    }

    const bool trusted =
        matches ||
        (
            w->haveAdv &&
            uint32_t(e.receivedMs - w->advMs) <= PACKET_STALE_MS
        );

    bool updated = false;

    if (matches) {
        w->adv = adv.adv;
        w->haveAdv = true;
        w->advMs = e.receivedMs;

        updated = true;
    }

    if (trusted && rsp.hasRsp) {
        // พิมพ์ทันทีเมื่อสถานะ SOS เปลี่ยน
        // การรับ Broadcast ไม่รับประกันว่าจะรับทุกเหตุการณ์
        if (rsp.rsp.hasSos &&
            rsp.rsp.sos <= 1 &&
            (
                !w->haveRsp ||
                !w->rsp.hasSos ||
                w->rsp.sos != rsp.rsp.sos
            )) {
            w->urgent = true;
        }

        w->rsp = rsp.rsp;
        w->haveRsp = true;
        w->rspMs = e.receivedMs;

        updated = true;
    }

    if (trusted && rsp.hasName) {
        memcpy(w->name, rsp.name, sizeof(w->name));
    }

    if (updated) {
        w->rssi = e.rssi;
        w->lastSeen = e.receivedMs;
        w->offline = false;
        w->dirty = true;
    }
}

void setup() {
    Serial.begin(115200);
    delay(300);

    Serial.println(
        "{\"type\":\"boot\","
        "\"firmware\":\"GatewaySmartwatch-1.0\","
        "\"mode\":\"active_ble_scan\"}"
    );

    scanQueue = xQueueCreate(
        QUEUE_LENGTH,
        sizeof(ScanEvent)
    );

    if (!scanQueue) {
        Serial.println(
            "{\"type\":\"error\","
            "\"message\":\"queue allocation failed\"}"
        );

        while (true) {
            delay(1000);
        }
    }

    NimBLEDevice::init("GatewaySmartwatch");

    auto* scan = NimBLEDevice::getScan();

    // true = รับข้อมูลซ้ำจากเรือนเดิม เพื่ออัปเดตค่า
    scan->setScanCallbacks(&scanCallbacks, true);

    // Active Scan เพื่อขอ Scan Response
    scan->setActiveScan(true);

    scan->setInterval(100);
    scan->setWindow(100);

    // ประมวลผลผ่าน callback
    scan->setMaxResults(0);

    if (!scan->start(SCAN_DURATION_MS)) {
        Serial.println(
            "{\"type\":\"error\","
            "\"message\":\"scan start failed; retrying\"}"
        );
    }
}

void loop() {
    ScanEvent e;

    // จำกัดจำนวนต่อรอบ ป้องกันข้อมูล BLE ทำให้ timer ไม่ทำงาน
    for (
        size_t i = 0;
        i < QUEUE_LENGTH &&
        xQueueReceive(scanQueue, &e, 0) == pdTRUE;
        ++i
    ) {
        processEvent(e);

        const uint32_t eventNow = millis();

        for (auto& w : watches) {
            if (w.used && w.urgent) {
                printWatch(w, eventNow);
            }
        }
    }

    const uint32_t now = millis();

    for (auto& w : watches) {
        if (!w.used) {
            continue;
        }

        if (!w.offline &&
            uint32_t(now - w.lastSeen) > PACKET_STALE_MS) {
            w.offline = true;
            w.dirty = true;
        }

        // หมดอายุข้อมูล ADV/RSP แยกกัน
        if (w.haveAdv &&
            uint32_t(now - w.advMs) > PACKET_STALE_MS) {
            w.haveAdv = false;
            w.dirty = true;
        }

        if (w.haveRsp &&
            uint32_t(now - w.rspMs) > PACKET_STALE_MS) {
            w.haveRsp = false;
            w.dirty = true;
        }

        if (w.dirty &&
            uint32_t(now - w.printMs) >= PRINT_INTERVAL_MS) {
            printWatch(w, now);
        }
    }

    auto* scan = NimBLEDevice::getScan();

    static uint32_t retryMs = 0;

    if (!scan->isScanning() &&
        uint32_t(now - retryMs) >= 1000) {
        retryMs = now;

        if (!scan->start(SCAN_DURATION_MS)) {
            Serial.println(
                "{\"type\":\"error\","
                "\"message\":\"scan restart failed\"}"
            );
        }
    }

    static uint32_t statsMs = 0;

    if (uint32_t(now - statsMs) >= 10000) {
        statsMs = now;

        Serial.printf(
            "{\"type\":\"status\","
            "\"scanning\":%s,"
            "\"queue_dropped\":%lu,"
            "\"malformed\":%lu,"
            "\"capacity_dropped\":%lu}\n",
            scan->isScanning() ? "true" : "false",
            static_cast<unsigned long>(dropped.load()),
            static_cast<unsigned long>(malformedCount),
            static_cast<unsigned long>(capacityDrops)
        );
    }

    delay(5);
}