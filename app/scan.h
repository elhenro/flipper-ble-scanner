#pragma once

#include <furi.h>

#define BLE_SCAN_MAX_DEVICES 32
#define BLE_SCAN_NAME_LEN    16
#define BLE_SCAN_NO_COMPANY  0xFFFF

typedef struct {
    uint8_t addr[6]; // little-endian, as sent by controller
    uint8_t addr_type;
    int8_t rssi;
    float rssi_avg;
    uint32_t last_seen;
    uint32_t seen_count;
    uint16_t company;
    uint8_t apple_type; // continuity message type, 0 = none
    uint16_t apple_model; // proximity pairing model id (airpods/beats)
    bool connectable;
    bool scan_rsp; // answered an active scan request
    char name[BLE_SCAN_NAME_LEN];
} BleDevice;

typedef struct BleScan BleScan;

BleScan* ble_scan_alloc(void);
void ble_scan_free(BleScan* scan);

#define BLE_SCAN_UNSUPPORTED 0xFF

/** 0 on success, BLE status code on failure, BLE_SCAN_UNSUPPORTED on light stack */
uint8_t ble_scan_start(BleScan* scan);
void ble_scan_stop(BleScan* scan);
bool ble_scan_is_supported(void);

/** copy devices seen within max_age_ms, strongest first, returns count */
size_t ble_scan_snapshot(BleScan* scan, BleDevice* out, size_t max, uint32_t max_age_ms);
uint32_t ble_scan_report_count(BleScan* scan);
