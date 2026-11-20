#pragma once
#include <stddef.h>
#include <stdint.h>

// Arduino-ESP32 4 BLE returns merged ADV + scan response without a public
// ADV-length accessor. Locate the end of this watch's documented AA field.
// Include ADV zero padding in the ADV segment, so the response starts at its
// first real AD field. This is protocol-specific, not a generic BLE splitter.
inline size_t watchAdvBoundary(const uint8_t* data, size_t size) {
    if (!data || !size) return 0;
    for (size_t i = 0; i < size;) {
        const size_t len = data[i];
        if (!len || len > size - i - 1) return size;
        const uint8_t type = data[i + 1];
        // A response received without its ADV has a name or BB field first.
        if (i == 0 && (type == 0x08 || type == 0x09 || type == 0xBB)) return 0;
        i += len + 1;
        if (type == 0xAA && len == 12) {
            while (i < size && data[i] == 0) ++i;
            return i;
        }
    }
    return size;
}
