#pragma once

// Target MAC is stored in NVS and set through Web Config.
// An empty target means no smartwatch data is accepted.
static constexpr bool PRINT_RAW_MATCHED = true;
static constexpr bool PRINT_ALL_BLE = false;  // Temporary discovery diagnostics.
static constexpr uint32_t SCAN_DURATION_MS = 5000;
static constexpr uint32_t PRINT_INTERVAL_MS = 1000;
static constexpr uint32_t PACKET_STALE_MS = 30000;
static constexpr size_t MAX_WATCHES = 16;
static constexpr size_t QUEUE_LENGTH = 32;

// W5500 signals verified against the supplied schematic, page 3.
static constexpr bool ENABLE_ETHERNET = true;
static constexpr int LAN_SCK = 12;
static constexpr int LAN_MISO = 13;
static constexpr int LAN_MOSI = 11;
static constexpr int LAN_CS = 10;
static constexpr int LAN_IRQ = 14;
static constexpr int LAN_RST = 21;

// The schematic uses UART0 on H1 (GPIO43 TX / GPIO44 RX).
// Do NOT enable USB CDC On Boot for this UART configuration.
// No GPIO is driven by this first BLE-only version.
