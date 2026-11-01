#pragma once

#include <furi.h>

#define BLE_SCAN_MAX_DEVICES 32
#define BLE_SCAN_NAME_LEN    16
#define BLE_SCAN_NO_COMPANY  0xFFFF
// vendor data kept per device, cut to what the details page can show
#define BLE_SCAN_MF_LEN   12 // manufacturer data after the company id
#define BLE_SCAN_SD_LEN   4 // service data after its 16-bit uuid
#define BLE_SCAN_UUID_MAX 3 // advertised 16-bit service uuids

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
    uint8_t mf_len;
    uint8_t mf[BLE_SCAN_MF_LEN]; // manufacturer data, company id in dev->company
    uint8_t sd_len;
    uint16_t sd_uuid;
    uint8_t sd[BLE_SCAN_SD_LEN];
    uint8_t uuid_count;
    uint16_t uuids[BLE_SCAN_UUID_MAX];
    bool connectable;
    bool scan_rsp; // answered an active scan request
    char name[BLE_SCAN_NAME_LEN];
    // 0x2A00 read over gatt, wins over the advertised name: apple never sends one
    char gatt_name[BLE_SCAN_NAME_LEN];
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
/** keeps a name read over gatt on the device record, false if the device is gone */
bool ble_scan_set_gatt_name(BleScan* scan, const uint8_t* addr, const char* name);
uint32_t ble_scan_report_count(BleScan* scan);
