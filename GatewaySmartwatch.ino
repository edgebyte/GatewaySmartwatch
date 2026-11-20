/* GatewaySmartwatch v2.1
 * ESP32-S3 / Arduino-ESP32 4.0.0 / bundled BLE library
 * Active BLE scanner for the attached Gateway Broadcast protocol.
 * BLE broadcast scanner with persistent MAC filter and Wi-Fi/LAN Web Config.
 */
#include <Arduino.h>
#include <HardwareSerial.h>
#include <BLE.h>
#include "LegacyPayload.h"
#include <atomic>
#include <strings.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include "Config.h"
#include "WatchProtocol.h"
#include "GatewayState.h"
#include "WebConfig.h"
static GatewayWebConfig webConfig;
// Explicit UART0, matching the UART test sketch.
static HardwareSerial DebugUART(0);
static QueueHandle_t scanQueue;
static WatchState watches[MAX_WATCHES];
static std::atomic<uint32_t> dropped{0};
static uint32_t malformedCount = 0, capacityDrops = 0;

// Copy BLE payload into the queue; never print or retain device data in callbacks.
static void enqueue(const BLEAdvertisedDevice& dev) {
    const uint8_t* payload = dev.getPayload();
    const size_t size = dev.getPayloadLength();
    if (!payload || !size || size > 62) return;
    ScanEvent e{};
    e.receivedMs = millis();
    e.rssi = dev.getRSSI();
    e.addressType = static_cast<uint8_t>(dev.getAddressType());
    snprintf(e.mac, sizeof(e.mac), "%s", dev.getAddress().toString().c_str());
    if (!webConfig.accepts(e.mac)) return;
    const size_t advLen = watchAdvBoundary(payload, size);
    if (advLen > 31 || size - advLen > 31) return;
    e.size = static_cast<uint8_t>(size);
    e.advSize = static_cast<uint8_t>(advLen);
    memcpy(e.bytes, payload, size);
    if (xQueueSend(scanQueue, &e, 0) != pdTRUE) dropped.fetch_add(1);
}

static void printString(const char* str) {
    DebugUART.print('"');
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(str); *p; ++p) {
        if (*p == '"' || *p == '\\') { DebugUART.print('\\'); DebugUART.write(*p); }
        else if (*p < 32 || *p >= 127) DebugUART.printf("\\u%04x", *p);
        else DebugUART.write(*p);
    }
    DebugUART.print('"');
}
static void number(bool valid, uint32_t value) {
    if (valid) DebugUART.print(value); else DebugUART.print("null");
}
static void scaled(bool valid, uint16_t value, unsigned divisor) {
    if (!valid) { DebugUART.print("null"); return; }
    if (divisor == 100) DebugUART.printf("%u.%02u", value / 100, value % 100);
    else DebugUART.printf("%u.%u", value / 10, value % 10);
}
static void printWatch(WatchState& w, uint32_t now) {
    const bool freshAdv = w.haveAdv && uint32_t(now - w.advMs) <= PACKET_STALE_MS;
    const bool freshRsp = w.haveRsp && uint32_t(now - w.rspMs) <= PACKET_STALE_MS;
    DebugUART.print("{\"type\":\"watch\",\"mac\":"); printString(w.mac);
    DebugUART.printf(",\"address_type\":%u,\"name\":", w.addressType); printString(w.name);
    DebugUART.printf(",\"uptime_ms\":%lu,\"rssi_dbm\":%d,\"online\":%s",
                  static_cast<unsigned long>(now), w.rssi, w.offline ? "false" : "true");
    DebugUART.print(",\"adv_rx_age_ms\":"); number(w.haveAdv, uint32_t(now - w.advMs));
    DebugUART.print(",\"rsp_rx_age_ms\":"); number(w.haveRsp, uint32_t(now - w.rspMs));
    DebugUART.print(",\"heart_rate_bpm\":"); number(freshAdv && w.adv.heartRate != 255, w.adv.heartRate);
    DebugUART.print(",\"steps\":"); number(freshAdv && w.adv.steps != 0, w.adv.steps);
    DebugUART.print(",\"sleep_min\":"); number(freshAdv && w.adv.sleepMinutes != 0, w.adv.sleepMinutes);
    DebugUART.print(",\"deep_sleep_min\":"); number(freshAdv && w.adv.deepSleepMinutes != 0, w.adv.deepSleepMinutes);
    DebugUART.print(",\"sbp_mmhg\":"); number(freshAdv && w.adv.sbp != 255, w.adv.sbp);
    DebugUART.print(",\"dbp_mmhg\":"); number(freshAdv && w.adv.dbp != 255, w.adv.dbp);
    DebugUART.print(",\"spo2_pct\":"); number(freshRsp && w.rsp.spo2 != 255, w.rsp.spo2);
    DebugUART.print(",\"glucose_mmol_l\":"); scaled(freshRsp && w.rsp.glucose100 != 65535, w.rsp.glucose100, 100);
    DebugUART.print(",\"body_temp_c\":"); scaled(freshRsp && w.rsp.bodyTemp10 != 65535, w.rsp.bodyTemp10, 10);
    DebugUART.print(",\"skin_temp_c\":"); scaled(freshRsp && w.rsp.skinTemp10 != 65535, w.rsp.skinTemp10, 10);
    DebugUART.print(",\"battery_pct\":");
    number(freshRsp && w.rsp.hasBattery && w.rsp.battery >= 1 && w.rsp.battery <= 100, w.rsp.battery);
    DebugUART.print(",\"sos\":");
    if (freshRsp && w.rsp.hasSos && w.rsp.sos <= 1) DebugUART.print(w.rsp.sos ? "true" : "false");
    else DebugUART.print("null");
    DebugUART.println('}');
    w.printMs = now; w.dirty = false; w.urgent = false;
}
static void printRaw(const ScanEvent& e) {
    DebugUART.print("{\"type\":\"raw\",\"mac\":"); printString(e.mac);
    DebugUART.printf(",\"address_type\":%u,\"rssi_dbm\":%d,\"adv_bytes\":%u,\"hex\":\"",
                  e.addressType, e.rssi, e.advSize);
    for (size_t i = 0; i < e.size; ++i) DebugUART.printf("%02X", e.bytes[i]);
    DebugUART.println("\"}");
}
static WatchState* findWatch(const ScanEvent& e) {
    for (auto& w : watches)
        if (w.used && w.addressType == e.addressType && strcmp(w.mac, e.mac) == 0) return &w;
    return nullptr;
}
static WatchState* allocateWatch(const ScanEvent& e) {
    WatchState* slot = nullptr;
    for (auto& w : watches) if (!w.used) { slot = &w; break; }
    if (!slot) {
        // Reuse only a slot that has been absent for five minutes.
        for (auto& w : watches)
            if (uint32_t(e.receivedMs - w.lastSeen) > 300000) { slot = &w; break; }
    }
    if (!slot) { ++capacityDrops; return nullptr; }
    *slot = WatchState{};
    slot->used = true;
    memcpy(slot->mac, e.mac, sizeof(slot->mac));
    slot->addressType = e.addressType;
    slot->printMs = e.receivedMs - PRINT_INTERVAL_MS;
    return slot;
}
static void processEvent(const ScanEvent& e) {
    // Recheck here as queued packets may predate a Web Config update.
    if (!webConfig.accepts(e.mac)) return;
    const auto adv = watchproto::parse(e.bytes, e.advSize);
    const auto rsp = watchproto::parse(e.bytes + e.advSize, e.size - e.advSize);
    if (adv.malformed || rsp.malformed) ++malformedCount;
    WatchState* w = findWatch(e);
    const bool matches = adv.signature && adv.fee7 && adv.hasAdv;
    if (PRINT_ALL_BLE || (PRINT_RAW_MATCHED && (matches || w))) printRaw(e);
    if (!w && !matches) return;
    if (!w) w = allocateWatch(e);
    if (!w) return;
    // Never attach metrics solely because the same MAC was seen long ago.
    const bool trusted = matches || (w->haveAdv && uint32_t(e.receivedMs - w->advMs) <= PACKET_STALE_MS);
    bool updated = false;
    if (matches) {
        w->adv = adv.adv; w->haveAdv = true; w->advMs = e.receivedMs;
        updated = true;
    }
    if (trusted && rsp.hasRsp) {
        // SOS changes bypass the normal 1 Hz output throttle. This is reception
        // status only; BLE broadcast does not guarantee delivery of SOS events.
        if (rsp.rsp.hasSos && rsp.rsp.sos <= 1 &&
            (!w->haveRsp || !w->rsp.hasSos || w->rsp.sos != rsp.rsp.sos)) w->urgent = true;
        w->rsp = rsp.rsp; w->haveRsp = true; w->rspMs = e.receivedMs;
        updated = true;
    }
    if (trusted && rsp.hasName) memcpy(w->name, rsp.name, sizeof(w->name));
    if (updated) {
        w->rssi = e.rssi; w->lastSeen = e.receivedMs;
        w->offline = false; w->dirty = true;
    }
}
void setup() {
    DebugUART.begin(115200, SERIAL_8N1, 44, 43);
    delay(300);
    DebugUART.println("{\"type\":\"boot\",\"firmware\":\"GatewaySmartwatch-2.1\",\"mode\":\"filtered_ble_scan_web_config\"}");
    scanQueue = xQueueCreate(QUEUE_LENGTH, sizeof(ScanEvent));
    if (!scanQueue) {
        DebugUART.println("{\"type\":\"error\",\"message\":\"queue allocation failed\"}");
        while (true) delay(1000);
    }
    const auto bleInit = BLE.begin("GatewaySmartwatch");
    if (!bleInit) {
        DebugUART.print("[BLE] Initialization failed: ");
        DebugUART.println(bleInit.toString());
    }
    webConfig.begin(DebugUART, []() {
        // Publish of the new filter happens before this reset. A second check
        // in processEvent rejects old-MAC events that race with this reset.
        xQueueReset(scanQueue);
        for (auto& w : watches) w = WatchState{};
    }, []() -> String {
        String s = "{\"scanning\":";
        s += BLE.getScan().isScanning() ? "true" : "false";
        s += ",\"queue_dropped\":" + String(dropped.load());
        WatchState* latest = nullptr;
        for (auto& w : watches) if (w.used && webConfig.accepts(w.mac)) {
            if (!latest || uint32_t(millis() - w.lastSeen) < uint32_t(millis() - latest->lastSeen)) latest = &w;
        }
        if (latest) {
            s += ",\"last_mac\":" + GatewayWebConfig::json(latest->mac);
            s += ",\"name\":" + GatewayWebConfig::json(latest->name);
            s += ",\"rssi_dbm\":" + String(latest->rssi);
            s += ",\"age_ms\":" + String(uint32_t(millis() - latest->lastSeen));
        } else s += ",\"last_mac\":\"\"";
        return s + "}";
    });
    auto scan = BLE.getScan();
    scan.onResult([](const BLEAdvertisedDevice& dev) { enqueue(dev); });
    scan.setFilterDuplicates(false);
    scan.setActiveScan(true);                    // Required for BB response data.
    scan.setInterval(100);
    scan.setWindow(50); // Leave airtime for Wi-Fi configuration.
    if (!scan.start(SCAN_DURATION_MS))
        DebugUART.println("{\"type\":\"error\",\"message\":\"scan start failed; retrying\"}");
}
void loop() {
    webConfig.tick();
    ScanEvent e;
    // A bounded batch prevents a busy radio from starving timers/status output.
    for (size_t i = 0; i < QUEUE_LENGTH && xQueueReceive(scanQueue, &e, 0) == pdTRUE; ++i) {
        processEvent(e);
        const uint32_t now = millis();
        for (auto& w : watches) if (w.used && w.urgent) printWatch(w, now);
    }
    const uint32_t now = millis();
    for (auto& w : watches) {
        if (!w.used) continue;
        if (!w.offline && uint32_t(now - w.lastSeen) > PACKET_STALE_MS) {
            w.offline = true; w.dirty = true;
        }
        // Expire either segment independently while the other is still arriving.
        if (w.haveAdv && uint32_t(now - w.advMs) > PACKET_STALE_MS) {
            w.haveAdv = false; w.dirty = true;
        }
        if (w.haveRsp && uint32_t(now - w.rspMs) > PACKET_STALE_MS) {
            w.haveRsp = false; w.dirty = true;
        }
        if (w.dirty && uint32_t(now - w.printMs) >= PRINT_INTERVAL_MS) printWatch(w, now);
    }
    auto scan = BLE.getScan();
    static uint32_t retryMs = 0;
    if (!scan.isScanning() && uint32_t(now - retryMs) >= 1000) {
        retryMs = now;
        if (!scan.start(SCAN_DURATION_MS))
            DebugUART.println("{\"type\":\"error\",\"message\":\"scan restart failed\"}");
    }
    static uint32_t statsMs = 0;
    if (uint32_t(now - statsMs) >= 10000) {
        statsMs = now;
        DebugUART.printf("{\"type\":\"status\",\"scanning\":%s,\"queue_dropped\":%lu,\"malformed\":%lu,\"capacity_dropped\":%lu}\n",
                      scan.isScanning() ? "true" : "false",
                      static_cast<unsigned long>(dropped.load()),
                      static_cast<unsigned long>(malformedCount),
                      static_cast<unsigned long>(capacityDrops));
    }
    delay(5);
}
