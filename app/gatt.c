#include "gatt_i.h"

#include <furi_hal_bt.h>
#include <string.h>

#define OP_DISCONNECT          0x0406 // hci link control
#define OP_CREATE_CONNECTION   0xFC9C // aci_gap_create_connection
#define OP_TERMINATE_GAP_PROC  0xFC9D
#define OP_DISC_ALL_SERVICES   0xFD12
#define OP_DISC_ALL_CHARS      0xFD15
#define OP_READ_CHAR_VALUE     0xFD18
#define OP_WRITE_CHAR_DESC     0xFD21
#define GAP_DIRECT_CONN_PROC   0x40
#define REASON_USER_TERMINATED 0x13

#define CONNECT_TIMEOUT_MS 10000

static void put16(uint8_t* p, uint16_t v) {
    p[0] = v & 0xFF;
    p[1] = v >> 8;
}

BleGatt* ble_gatt_alloc(void) {
    BleGatt* gatt = malloc(sizeof(BleGatt));
    memset(gatt, 0, sizeof(BleGatt));
    gatt->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    gatt->conn_handle = BLE_GATT_NO_HANDLE;
    gatt->handler = ble_event_dispatcher_register_svc_handler(ble_gatt_event_handler, gatt);
    return gatt;
}

void ble_gatt_free(BleGatt* gatt) {
    ble_gatt_disconnect(gatt);
    ble_event_dispatcher_unregister_svc_handler(gatt->handler);
    furi_mutex_free(gatt->mutex);
    free(gatt);
}

static void ble_gatt_lock(BleGatt* gatt) {
    furi_check(furi_mutex_acquire(gatt->mutex, FuriWaitForever) == FuriStatusOk);
}

// state is set before the command: its events may arrive before the call returns
static bool ble_gatt_command(
    BleGatt* gatt,
    BleGattState state,
    uint16_t opcode,
    const uint8_t* params,
    uint8_t len) {
    ble_gatt_lock(gatt);
    BleGattState prev = gatt->info.state;
    gatt->info.state = state;
    gatt->info.error = 0;
    furi_mutex_release(gatt->mutex);

    uint8_t status = furi_hal_bt_hci_command(opcode, params, len, NULL, 0);
    FURI_LOG_D("BleGatt", "cmd %04X len %u status %02X", opcode, len, status);
    if(status) {
        ble_gatt_lock(gatt);
        gatt->info.state = prev == BleGattStateConnecting ? BleGattStateFailed : prev;
        gatt->info.error = status;
        furi_mutex_release(gatt->mutex);
    }
    return status == 0;
}

bool ble_gatt_connect(BleGatt* gatt, const BleDevice* dev) {
    ble_gatt_lock(gatt);
    memset(&gatt->info, 0, sizeof(BleGattInfo));
    gatt->connect_started = furi_get_tick();
    furi_mutex_release(gatt->mutex);

    uint8_t p[24];
    put16(&p[0], 0x0060); // scan interval 60ms
    put16(&p[2], 0x0030); // scan window 30ms
    p[4] = dev->addr_type;
    memcpy(&p[5], dev->addr, 6);
    p[11] = 0; // own address public
    put16(&p[12], 0x0018); // conn interval 30..50ms
    put16(&p[14], 0x0028);
    put16(&p[16], 0); // latency
    put16(&p[18], 500); // supervision timeout 5s
    put16(&p[20], 0); // ce length
    put16(&p[22], 0);
    if(ble_gatt_command(gatt, BleGattStateConnecting, OP_CREATE_CONNECTION, p, 24)) return true;
    // command rejected: state was idle, mark failed so the ui shows the error
    ble_gatt_lock(gatt);
    gatt->info.state = BleGattStateFailed;
    furi_mutex_release(gatt->mutex);
    return false;
}

void ble_gatt_tick(BleGatt* gatt) {
    ble_gatt_lock(gatt);
    bool expired = gatt->info.state == BleGattStateConnecting &&
                   furi_get_tick() - gatt->connect_started > CONNECT_TIMEOUT_MS;
    if(expired) {
        gatt->info.state = BleGattStateFailed;
        gatt->info.error = BLE_GATT_ERR_TIMEOUT;
    }
    furi_mutex_release(gatt->mutex);
    if(expired) {
        uint8_t proc = GAP_DIRECT_CONN_PROC;
        furi_hal_bt_hci_command(OP_TERMINATE_GAP_PROC, &proc, 1, NULL, 0);
    }
}

void ble_gatt_disconnect(BleGatt* gatt) {
    ble_gatt_lock(gatt);
    uint16_t handle = gatt->conn_handle;
    bool connecting = gatt->info.state == BleGattStateConnecting;
    gatt->info.state = BleGattStateIdle;
    furi_mutex_release(gatt->mutex);

    if(connecting) {
        uint8_t proc = GAP_DIRECT_CONN_PROC;
        furi_hal_bt_hci_command(OP_TERMINATE_GAP_PROC, &proc, 1, NULL, 0);
    }
    if(handle == BLE_GATT_NO_HANDLE) return;

    uint8_t p[3];
    put16(&p[0], handle);
    p[2] = REASON_USER_TERMINATED;
    furi_hal_bt_hci_command(OP_DISCONNECT, p, 3, NULL, 0);
    // handler clears conn_handle on disconnection complete
    for(uint8_t i = 0; i < 20; i++) {
        ble_gatt_lock(gatt);
        bool down = gatt->conn_handle == BLE_GATT_NO_HANDLE;
        furi_mutex_release(gatt->mutex);
        if(down) break;
        furi_delay_ms(50);
    }
}

bool ble_gatt_discover_services(BleGatt* gatt) {
    ble_gatt_lock(gatt);
    gatt->info.service_count = 0;
    gatt->info.services_done = true;
    uint8_t p[2];
    put16(p, gatt->conn_handle);
    furi_mutex_release(gatt->mutex);
    return ble_gatt_command(gatt, BleGattStateBusy, OP_DISC_ALL_SERVICES, p, 2);
}

bool ble_gatt_discover_chars(BleGatt* gatt, uint8_t service_index) {
    ble_gatt_lock(gatt);
    const BleGattService* s = &gatt->info.services[service_index];
    uint8_t p[6];
    put16(&p[0], gatt->conn_handle);
    put16(&p[2], s->start);
    put16(&p[4], s->end);
    gatt->info.char_count = 0;
    furi_mutex_release(gatt->mutex);
    return ble_gatt_command(gatt, BleGattStateBusy, OP_DISC_ALL_CHARS, p, 6);
}

bool ble_gatt_read(BleGatt* gatt, uint8_t char_index) {
    ble_gatt_lock(gatt);
    uint16_t value_handle = gatt->info.chars[char_index].value_handle;
    if(gatt->info.value_handle != value_handle) {
        gatt->info.value_len = 0;
        gatt->info.value_updates = 0;
    }
    gatt->info.value_handle = value_handle;
    uint8_t p[4];
    put16(&p[0], gatt->conn_handle);
    put16(&p[2], value_handle);
    furi_mutex_release(gatt->mutex);
    return ble_gatt_command(gatt, BleGattStateBusy, OP_READ_CHAR_VALUE, p, 4);
}

bool ble_gatt_set_notify(BleGatt* gatt, uint8_t char_index, bool enable) {
    ble_gatt_lock(gatt);
    uint16_t value_handle = gatt->info.chars[char_index].value_handle;
    gatt->info.value_handle = value_handle;
    gatt->info.notifying = enable;
    uint8_t p[7];
    put16(&p[0], gatt->conn_handle);
    put16(&p[2], value_handle + 1);
    p[4] = 2;
    put16(&p[5], enable ? 0x0001 : 0x0000);
    furi_mutex_release(gatt->mutex);
    return ble_gatt_command(gatt, BleGattStateBusy, OP_WRITE_CHAR_DESC, p, 7);
}

void ble_gatt_snapshot(BleGatt* gatt, BleGattInfo* out) {
    ble_gatt_lock(gatt);
    *out = gatt->info;
    furi_mutex_release(gatt->mutex);
}
