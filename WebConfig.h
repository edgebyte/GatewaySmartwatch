#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ETH.h>
#include <SPI.h>
#include <esp_system.h>
#include <functional>
#include "Config.h"
#include "StoredConfig.h"
#include "WebPage.h"

class GatewayWebConfig {
public:
    static String json(const char* value) {
        String out = "\"";
        for (const unsigned char* p = reinterpret_cast<const unsigned char*>(value); *p; ++p) {
            if (*p == '"' || *p == '\\') { out += '\\'; out += char(*p); }
            else if (*p < 32) { char b[7]; snprintf(b, sizeof b, "\\u%04x", *p); out += b; }
            else out += char(*p);
        }
        return out + '"';
    }
    bool accepts(const char* mac) const {
        char target[18];
        portENTER_CRITICAL(&filterMux);
        memcpy(target, activeTarget, sizeof target);
        portEXIT_CRITICAL(&filterMux);
        return macpolicy::allowed(target, mac);
    }
    String targetMac() const { return String(settings.target); }

    bool begin(Stream& serial, std::function<void()> onTargetChange, std::function<String()> bleStatus) {
        log = &serial;
        changed = onTargetChange;
        ble = bleStatus;
        if (!prefs.begin("gw-web-v2", false)) {
            log->println("[CONFIG] NVS unavailable; Web Config disabled, BLE filter remains closed.");
            return false;
        }
        StoredConfig loaded{};
        const bool valid = prefs.getBytesLength("settings") == sizeof loaded &&
                           prefs.getBytes("settings", &loaded, sizeof loaded) == sizeof loaded &&
                           validStoredConfig(loaded);
        if (valid) settings = loaded;
        else {
            settings = StoredConfig{}; // Default: no MAC, no accepted watches.
            randomHex(settings.accessPass, 8); // 16 printable characters.
            if (!persist(settings)) {
                log->println("[CONFIG] Cannot save initial settings; Web Config disabled.");
                return false;
            }
        }
        publishFilter();
        randomHex(csrf, 16);
        WiFi.persistent(false);
        WiFi.mode(WIFI_AP_STA);
        WiFi.setAutoReconnect(true);
        WiFi.setHostname("edgebyte-gateway");
        const uint64_t id = ESP.getEfuseMac();
        char name[40];
        snprintf(name, sizeof name, "Gateway-%06lX", static_cast<unsigned long>(id & 0xFFFFFF));
        apName = name;
        apStarted = WiFi.softAP(apName.c_str(), settings.accessPass);
        if (settings.ssid[0]) WiFi.begin(settings.ssid, settings.wifiPass);
        if (ENABLE_ETHERNET) {
            lanSPI.begin(LAN_SCK, LAN_MISO, LAN_MOSI, LAN_CS);
            lanStarted = ETH.begin(ETH_PHY_W5500, 1, LAN_CS, LAN_IRQ, LAN_RST, lanSPI, 20);
            if (lanStarted) ETH.setHostname("edgebyte-gateway-lan");
        }
        const char* headers[] = {"Authorization"};
        server.collectHeaders(headers, 1);
        server.on("/", HTTP_GET, [this]() {
            if (!authorize()) return;
            String page = FPSTR(CONFIG_PAGE);
            page.replace("%CSRF%", csrf);
            noCache();
            server.sendHeader("Content-Security-Policy", "default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'; frame-ancestors 'none'; base-uri 'none'; form-action 'self'");
            server.send(200, "text/html; charset=utf-8", page);
        });
        server.on("/api/status", HTTP_GET, [this]() {
            if (!authorize()) return;
            noCache();
            String s;
            s.reserve(900);
            s = "{\"firmware\":\"2.1\",\"target_mac\":" + json(settings.target);
            s += ",\"wifi_ssid\":" + json(settings.ssid);
            s += ",\"ap_ssid\":" + json(apName.c_str());
            s += ",\"ap_ip\":" + json(WiFi.softAPIP().toString().c_str());
            s += ",\"wifi_connected\":"; s += WiFi.status() == WL_CONNECTED ? "true" : "false";
            s += ",\"wifi_ip\":" + json(WiFi.localIP().toString().c_str());
            s += ",\"lan_enabled\":"; s += ENABLE_ETHERNET ? "true" : "false";
            s += ",\"lan_started\":"; s += lanStarted ? "true" : "false";
            s += ",\"lan_link\":"; s += lanStarted && ETH.linkUp() ? "true" : "false";
            s += ",\"lan_ip\":" + json(lanStarted ? ETH.localIP().toString().c_str() : "0.0.0.0");
            s += ",\"ble\":" + ble() + "}";
            server.send(200, "application/json; charset=utf-8", s);
        });
        server.on("/api/config", HTTP_POST, [this]() { save(); });
        server.onNotFound([this]() {
            if (!authorize()) return;
            noCache(); server.send(404, "application/json", "{\"error\":\"Not found\"}");
        });
        server.begin();
        ready = true;
        printInfo();
        return true;
    }
    void tick() {
        if (!ready) return;
        server.handleClient();
        const uint32_t now = millis();
        if (restartPending && uint32_t(now - restartAt) >= 1500) ESP.restart();
        if (uint32_t(now - ipLogAt) >= 10000) {
            ipLogAt = now;
            String ips = WiFi.localIP().toString() + "/" + (lanStarted ? ETH.localIP().toString() : "0.0.0.0");
            if (ips != previousIPs) {
                previousIPs = ips;
                log->print("[NET] WiFi IP: "); log->print(WiFi.localIP());
                log->print(" | LAN IP: "); log->println(lanStarted ? ETH.localIP().toString() : "unavailable");
            }
        }
        // Local physical-access recovery; no unauthenticated reset HTTP route.
        for (int count = 0; count < 32 && log->available(); ++count) {
            const int c = log->read();
            if (c == '\r' || c == '\n') {
                if (command == "INFO") printInfo();
                else if (command == "RESETCFG") {
                    if (prefs.remove("settings")) {
                        settings.target[0] = 0; publishFilter(); changed();
                        log->println("[CONFIG] Settings cleared. Restarting with a new access password.");
                        restartPending = true; restartAt = now;
                    } else log->println("[CONFIG] Reset failed; settings unchanged.");
                }
                command = "";
            } else if (c >= 32 && c < 127) {
                if (command.length() < 32) command += char(c);
            }
        }
    }

private:
    Preferences prefs;
    WebServer server{80};
    SPIClass lanSPI{FSPI};
    Stream* log = nullptr;
    StoredConfig settings{};
    mutable portMUX_TYPE filterMux = portMUX_INITIALIZER_UNLOCKED;
    char activeTarget[18] = {};
    char csrf[33] = {};
    String apName, previousIPs, command;
    bool ready = false, apStarted = false, lanStarted = false, restartPending = false;
    uint32_t restartAt = 0, ipLogAt = 0;
    std::function<void()> changed;
    std::function<String()> ble;

    static void randomHex(char* dest, size_t byteCount) {
        uint8_t random[16];
        esp_fill_random(random, byteCount);
        for (size_t i = 0; i < byteCount; ++i) snprintf(dest + i * 2, 3, "%02X", random[i]);
    }
    void publishFilter() {
        portENTER_CRITICAL(&filterMux);
        memcpy(activeTarget, settings.target, sizeof activeTarget);
        portEXIT_CRITICAL(&filterMux);
    }
    bool persist(StoredConfig& config) {
        sealConfig(config);
        return prefs.putBytes("settings", &config, sizeof config) == sizeof config;
    }
    void noCache() {
        server.sendHeader("Cache-Control", "no-store");
        server.sendHeader("X-Content-Type-Options", "nosniff");
        server.sendHeader("X-Frame-Options", "DENY");
    }
    bool authorize() {
        // Explicitly reject non-Basic schemes before calling authenticate().
        // This also protects builds on older Arduino-ESP32 WebServer releases.
        if (!server.header("Authorization").startsWith("Basic ") ||
            !server.authenticate("admin", settings.accessPass)) {
            noCache();
            server.requestAuthentication(BASIC_AUTH, "GatewaySmartwatch");
            return false;
        }
        return true;
    }
    void error(int status, const char* message) {
        noCache();
        server.send(status, "application/json; charset=utf-8", "{\"error\":" + json(message) + "}");
    }
    void save() {
        if (!authorize()) return;
        if (restartPending) { error(409, "Gateway is restarting; reload after reconnecting."); return; }
        if (!server.hasArg("csrf") || server.arg("csrf") != csrf) { error(403, "Reload the configuration page and try again."); return; }
        if (!server.hasArg("mac") || !server.hasArg("ssid")) { error(400, "MAC and SSID fields are required."); return; }
        const String mac = server.arg("mac"), ssid = server.arg("ssid");
        const String wifiPass = server.arg("wifi_password"), accessPass = server.arg("access_password");
        const bool clearWiFiPass = server.arg("clear_wifi_password") == "1";
        if (mac.length() != strlen(mac.c_str()) || ssid.length() != strlen(ssid.c_str()) ||
            wifiPass.length() != strlen(wifiPass.c_str()) || accessPass.length() != strlen(accessPass.c_str())) {
            error(400, "Embedded zero bytes are not allowed."); return;
        }
        if (mac.length() > 64 || ssid.length() > 32 || wifiPass.length() > 64 || accessPass.length() > 32) {
            error(400, "Input exceeds the allowed length."); return;
        }
        StoredConfig next = settings;
        if (!macpolicy::normalize(mac.c_str(), next.target)) { error(400, "Invalid MAC. Use six hex pairs, e.g. 69:43:2D:20:8E:87."); return; }
        if (clearWiFiPass && wifiPass.length()) { error(400, "Choose either an open Wi-Fi network or a password."); return; }
        if (ssid != settings.ssid && ssid.length() && !wifiPass.length() && !clearWiFiPass) {
            error(400, "For a new SSID, enter its password or select the open-network checkbox."); return;
        }
        strlcpy(next.ssid, ssid.c_str(), sizeof next.ssid);
        if (!ssid.length() || clearWiFiPass) next.wifiPass[0] = 0;
        else if (wifiPass.length()) strlcpy(next.wifiPass, wifiPass.c_str(), sizeof next.wifiPass);
        if (!validWiFiPassword(next.wifiPass)) { error(400, "Wi-Fi password must be 8-63 characters, 64 hex digits, or explicitly open."); return; }
        if (accessPass.length()) {
            if (!validAccessPassword(accessPass.c_str())) { error(400, "Gateway password must be 8-32 printable ASCII characters without spaces."); return; }
            strlcpy(next.accessPass, accessPass.c_str(), sizeof next.accessPass);
        }
        const bool targetChanged = strcmp(next.target, settings.target) != 0;
        const bool networkChanged = strcmp(next.ssid, settings.ssid) != 0 ||
                                    strcmp(next.wifiPass, settings.wifiPass) != 0 ||
                                    strcmp(next.accessPass, settings.accessPass) != 0;
        const bool anyChanged = targetChanged || networkChanged;
        if (anyChanged && !persist(next)) { error(500, "Cannot save configuration. Previous settings remain active."); return; }
        settings = next;
        if (targetChanged) { publishFilter(); changed(); }
        noCache();
        String response = "{\"saved\":true,\"target_mac\":" + json(settings.target) + ",\"restarting\":";
        response += networkChanged ? "true}" : "false}";
        server.send(200, "application/json", response);
        if (networkChanged) { restartPending = true; restartAt = millis(); }
        log->print("[CONFIG] Allowed MAC: "); log->println(settings.target[0] ? settings.target : "NONE (blocked)");
    }
    void printInfo() {
        log->println("\n[WEB CONFIG] GatewaySmartwatch 2.1");
        log->print("AP SSID: "); log->println(apName);
        log->print("AP started: "); log->println(apStarted ? "yes" : "no");
        log->print("AP password / Web password: "); log->println(settings.accessPass);
        log->println("Web username: admin");
        log->print("AP URL: http://"); log->println(WiFi.softAPIP());
        log->print("WiFi IP: "); log->println(WiFi.localIP());
        log->print("LAN IP: "); log->println(lanStarted ? ETH.localIP().toString() : "unavailable");
        log->print("Allowed watch MAC: "); log->println(settings.target[0] ? settings.target : "NONE (blocked)");
        log->println("UART commands: INFO | RESETCFG (clears MAC, Wi-Fi and access password)");
    }
};
