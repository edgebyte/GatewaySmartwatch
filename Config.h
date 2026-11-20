#pragma once
#include <stddef.h>
#include <stdint.h>

// เว้นว่างเพื่อค้นหานาฬิกาทุกเรือนที่ตรงโปรโตคอล
// หากต้องการเฉพาะเรือน ให้คัดลอก MAC จาก Serial Monitor มาใส่
static constexpr const char* TARGET_MAC = "69:43:2D:20:8E:87";

static constexpr bool PRINT_RAW_MATCHED = false;

// เปิด true ชั่วคราว หากต้องการดูแพ็กเก็ต BLE รอบตัว
static constexpr bool PRINT_ALL_BLE = false;

static constexpr uint32_t SCAN_DURATION_MS = 5000;
static constexpr uint32_t PRINT_INTERVAL_MS = 1000;
static constexpr uint32_t PACKET_STALE_MS = 30000;

static constexpr size_t MAX_WATCHES = 16;
static constexpr size_t QUEUE_LENGTH = 32;