#include "gatt_i.h"

#include <furi_hal_bt.h>
#include <string.h>

#define TAG "BleGatt"

#define OP_DISCONNECT          0x0406 // hci link control
#define OP_CREATE_CONNECTION   0xFC9C // aci_gap_create_connection
#define OP_TERMINATE_GAP_PROC  0xFC9D
#define OP_CONFIRM_INDICATION  0xFD25 // aci_gatt_confirm_indication
#define GAP_DIRECT_CONN_PROC   0x40
#define REASON_USER_TERMINATED 0x13

#define CONNECT_TIMEOUT_MS 10000

void ble_gatt_lock(BleGatt* gatt) {
    furi_check(furi_mutex_acquire(gatt->mutex, FuriWaitForever) == FuriStatusOk);
}

void ble_gatt_unlock(BleGatt* gatt) {
    furi_mutex_release(gatt->mutex);
}

bool ble_gatt_command(
    BleGatt* gatt,
    BleGattState state,
    uint16_t opcode,
    const uint8_t* params,
    uint8_t len) {
    BleGattState prev = gatt->info.state;
    gatt->info.state = state;
    gatt->info.error = 0;
    ble_gatt_unlock(gatt);

    uint8_t status = furi_hal_bt_hci_command(opcode, params, len, NULL, 0);
    FURI_LOG_D(TAG, "cmd %04X len %u status %02X", opcode, len, status);
    if(status) {
        ble_gatt_lock(gatt);
        // rejected connect leaves no link to wait for. state moved on = link event won
        if(gatt->info.state == state) {
            gatt->info.state = state == BleGattStateConnecting ? BleGattStateFailed : prev;
        }
        gatt->info.error = status;
        ble_gatt_unlock(gatt);
    }
    return status == 0;
}

static void ble_gatt_terminate_connect(void) {
    uint8_t proc = GAP_DIRECT_CONN_PROC;
    furi_hal_bt_hci_command(OP_TERMINATE_GAP_PROC, &proc, 1, NULL, 0);
}

// event handler clears conn_handle on disconnection complete
static void ble_gatt_hangup(BleGatt* gatt) {
    ble_gatt_lock(gatt);
    uint16_t handle = gatt->conn_handle;
    bool send = handle != BLE_GATT_NO_HANDLE && !gatt->hanging_up;
    if(send) gatt->hanging_up = true;
    ble_gatt_unlock(gatt);
    if(!send) return;

    uint8_t p[3];
    ble_gatt_put16(&p[0], handle);
    p[2] = REASON_USER_TERMINATED;
    if(furi_hal_bt_hci_command(OP_DISCONNECT, p, 3, NULL, 0)) {
        // let the next tick retry
        ble_gatt_lock(gatt);
        gatt->hanging_up = false;
        ble_gatt_unlock(gatt);
    }
}

// up to 1s for the disconnection complete event
static void ble_gatt_wait_down(BleGatt* gatt) {
    for(uint8_t i = 0; i < 20; i++) {
        ble_gatt_lock(gatt);
        bool down = gatt->conn_handle == BLE_GATT_NO_HANDLE;
        ble_gatt_unlock(gatt);
        if(down) return;
        furi_delay_ms(50);
    }
}

BleGatt* ble_gatt_alloc(void) {
    // zeroed by furi malloc
    BleGatt* gatt = malloc(sizeof(BleGatt));
    gatt->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    gatt->conn_handle = BLE_GATT_NO_HANDLE;
    gatt->handler = ble_event_dispatcher_register_svc_handler(ble_gatt_event_handler, gatt);
    return gatt;
}

void ble_gatt_free(BleGatt* gatt) {
    // handler must see the link go down, else gap.c takes the event for the phone link
    ble_gatt_disconnect(gatt);
    ble_gatt_wait_down(gatt);
    ble_event_dispatcher_unregister_svc_handler(gatt->handler);
    furi_mutex_free(gatt->mutex);
    free(gatt);
}

bool ble_gatt_connect(BleGatt* gatt, const BleDevice* dev) {
    // a leftover link would hand its disconnection event to this attempt
    ble_gatt_disconnect(gatt);
    ble_gatt_wait_down(gatt);

    uint8_t p[24];
    ble_gatt_put16(&p[0], 0x0060); // scan interval 60ms
    ble_gatt_put16(&p[2], 0x0030); // scan window 30ms
    p[4] = dev->addr_type;
    memcpy(&p[5], dev->addr, 6);
    p[11] = 0; // own address public
    ble_gatt_put16(&p[12], 0x0018); // conn interval 30..50ms
    ble_gatt_put16(&p[14], 0x0028);
    ble_gatt_put16(&p[16], 0); // latency
    ble_gatt_put16(&p[18], 500); // supervision timeout 5s
    ble_gatt_put16(&p[20], 0); // ce length
    ble_gatt_put16(&p[22], 0);

    ble_gatt_lock(gatt);
    memset(&gatt->info, 0, sizeof(BleGattInfo));
    gatt->connect_started = furi_get_tick();
    return ble_gatt_command(gatt, BleGattStateConnecting, OP_CREATE_CONNECTION, p, sizeof(p));
}

void ble_gatt_tick(BleGatt* gatt) {
    ble_gatt_lock(gatt);
    bool expired = gatt->info.state == BleGattStateConnecting &&
                   furi_get_tick() - gatt->connect_started > CONNECT_TIMEOUT_MS;
    if(expired) {
        gatt->info.state = BleGattStateFailed;
        gatt->info.error = BLE_GATT_ERR_TIMEOUT;
    }
    // link that came up after the user gave up on it
    bool abandoned = gatt->info.state == BleGattStateIdle &&
                     gatt->conn_handle != BLE_GATT_NO_HANDLE;
    // an indication blocks the server until confirmed. hci_send_req waits for its own
    // command event, so it can't run on the ble event thread
    uint16_t confirm = BLE_GATT_NO_HANDLE;
    if(gatt->confirm_pending) {
        gatt->confirm_pending = false;
        confirm = gatt->conn_handle;
    }
    ble_gatt_unlock(gatt);
    if(expired) ble_gatt_terminate_connect();
    if(abandoned) ble_gatt_hangup(gatt);
    if(confirm != BLE_GATT_NO_HANDLE) {
        uint8_t p[2];
        ble_gatt_put16(p, confirm);
        furi_hal_bt_hci_command(OP_CONFIRM_INDICATION, p, 2, NULL, 0);
    }
}

void ble_gatt_disconnect(BleGatt* gatt) {
    ble_gatt_lock(gatt);
    bool connecting = gatt->info.state == BleGattStateConnecting;
    // idle also tells the handler a late connection is unwanted, tick hangs it up
    gatt->info.state = BleGattStateIdle;
    ble_gatt_unlock(gatt);
    if(connecting) ble_gatt_terminate_connect();
    ble_gatt_hangup(gatt);
}

void ble_gatt_snapshot(BleGatt* gatt, BleGattInfo* out) {
    ble_gatt_lock(gatt);
    *out = gatt->info;
    ble_gatt_unlock(gatt);
}
