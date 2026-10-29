#pragma once

#include "scan.h"

#define BLE_GATT_MAX_SERVICES 16
#define BLE_GATT_MAX_CHARS    24
#define BLE_GATT_VALUE_LEN    64

// characteristic properties (core spec vol 3 part g 3.3.1.1)
#define BLE_GATT_PROP_READ     0x02
#define BLE_GATT_PROP_WRITE_NR 0x04
#define BLE_GATT_PROP_WRITE    0x08
#define BLE_GATT_PROP_NOTIFY   0x10
#define BLE_GATT_PROP_INDICATE 0x20

// app side errors, distinct from hci/att codes
#define BLE_GATT_ERR_TIMEOUT 0xF0

typedef enum {
    BleGattStateIdle,
    BleGattStateConnecting,
    BleGattStateReady, // connected, no procedure running
    BleGattStateBusy, // discovery, read or write running
    BleGattStateFailed, // connect failed or link lost, see error
} BleGattState;

typedef struct {
    uint16_t start;
    uint16_t end;
    uint8_t uuid[16]; // little-endian as on air
    uint8_t uuid_len;
} BleGattService;

typedef struct {
    uint16_t value_handle;
    uint8_t props;
    uint8_t uuid[16];
    uint8_t uuid_len;
} BleGattChar;

typedef struct {
    BleGattState state;
    uint8_t error; // last hci/att error, 0 = none
    BleGattService services[BLE_GATT_MAX_SERVICES];
    uint8_t service_count;
    bool services_done;
    BleGattChar chars[BLE_GATT_MAX_CHARS];
    uint8_t char_count;
    uint16_t value_handle; // characteristic whose value is held below
    uint8_t value[BLE_GATT_VALUE_LEN];
    uint8_t value_len;
    uint32_t value_updates;
    bool notifying;
} BleGattInfo;

typedef struct BleGatt BleGatt;

BleGatt* ble_gatt_alloc(void);
void ble_gatt_free(BleGatt* gatt);

/** scan must be stopped first, only one gap procedure runs at a time */
bool ble_gatt_connect(BleGatt* gatt, const BleDevice* dev);
/** blocks up to 1s until the link is down */
void ble_gatt_disconnect(BleGatt* gatt);
/** call periodically, cancels connection attempts that hang */
void ble_gatt_tick(BleGatt* gatt);

bool ble_gatt_discover_services(BleGatt* gatt);
bool ble_gatt_discover_chars(BleGatt* gatt, uint8_t service_index);
bool ble_gatt_read(BleGatt* gatt, uint8_t char_index);
/** writes cccd, assumed at value handle + 1 (true for nearly all servers) */
bool ble_gatt_set_notify(BleGatt* gatt, uint8_t char_index, bool enable);

void ble_gatt_snapshot(BleGatt* gatt, BleGattInfo* out);
