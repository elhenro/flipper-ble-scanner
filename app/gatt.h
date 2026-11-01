#pragma once

#include "scan.h"

#define BLE_GATT_MAX_SERVICES 16
#define BLE_GATT_MAX_CHARS    24
#define BLE_GATT_VALUE_LEN    64
#define BLE_GATT_NAME_LEN     20

// characteristic properties (core spec vol 3 part g 3.3.1.1)
#define BLE_GATT_PROP_READ     0x02
#define BLE_GATT_PROP_WRITE_NR 0x04
#define BLE_GATT_PROP_WRITE    0x08
#define BLE_GATT_PROP_NOTIFY   0x10
#define BLE_GATT_PROP_INDICATE 0x20

// app side errors, distinct from hci/att codes
#define BLE_GATT_ERR_TIMEOUT 0xF0
#define BLE_GATT_ERR_PASSKEY 0xE0
// default att mtu 23, so a plain read stops at 20 bytes and needs a read blob for the rest
#define BLE_GATT_READ_CHUNK 20

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
    uint16_t cccd; // its client config descriptor, 0 = none found
    bool descs_done;
    bool notifying; // notifications or indications enabled
    // 0x2A00 value, the only real name apple devices ever hand out
    char peer_name[BLE_SCAN_NAME_LEN];
    bool peer_name_set;
    char desc[BLE_GATT_NAME_LEN]; // 0x2901 user description of the selected char
    bool desc_done; // search and read attempted, don't ask again
    // read long: the plain read came back full, the rest is appended from read blobs
    bool truncated;
    bool long_done; // fetched the rest already, don't ask again
    // write char value: pending until the procedure complete arrives
    bool write_pending;
    bool write_done;
    uint8_t write_len;
    // pairing, 0 none, 1 running, 2 paired, 3 failed (see pair_reason)
    uint8_t pair_state;
    uint8_t pair_reason;
    bool pair_confirm; // numeric comparison prompt waits for OK / Back
    bool resume_read; // the read that asked for pairing failed, redo it now
    uint32_t pair_numeric;
    // connect target, survives the per-attempt info reset
    uint8_t peer_addr[6];
    bool peer_set;
} BleGattInfo;

typedef struct BleGatt BleGatt;

BleGatt* ble_gatt_alloc(void);
void ble_gatt_free(BleGatt* gatt);

/** scan must be stopped first, only one gap procedure runs at a time */
bool ble_gatt_connect(BleGatt* gatt, const BleDevice* dev);
/** returns at once, link down arrives as event */
void ble_gatt_disconnect(BleGatt* gatt);
/** call periodically: cancels connection attempts that hang, hangs up abandoned links */
void ble_gatt_tick(BleGatt* gatt);

bool ble_gatt_discover_services(BleGatt* gatt);
bool ble_gatt_discover_chars(BleGatt* gatt, uint8_t service_index);
/** picks the characteristic the calls below work on */
void ble_gatt_select(BleGatt* gatt, uint8_t char_index);
bool ble_gatt_read(BleGatt* gatt);
/** fetches the bytes past the first read chunk, appended to the value */
bool ble_gatt_read_long(BleGatt* gatt);
/** writes to the selected char, with response when it has write */
bool ble_gatt_write(BleGatt* gatt, const uint8_t* data, uint8_t len);
/** pairs with the peer, the outcome arrives in gatt_event.c */
bool ble_gatt_pair(BleGatt* gatt);
/** answers the numeric comparison prompt */
bool ble_gatt_pair_confirm(BleGatt* gatt, bool yes);
/** looks for its cccd, sets descs_done */
bool ble_gatt_discover_descs(BleGatt* gatt);
/** reads the char's 0x2901 name, sets desc_done */
bool ble_gatt_read_desc(BleGatt* gatt);
/** writes the cccd (indicate bit when the char only indicates), fails when none was found */
bool ble_gatt_set_notify(BleGatt* gatt, bool enable);

void ble_gatt_snapshot(BleGatt* gatt, BleGattInfo* out);
