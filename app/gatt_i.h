#pragma once

#include "gatt.h"

#include <furi_ble/event_dispatcher.h>

#define BLE_GATT_NO_HANDLE 0xFFFF

struct BleGatt {
    FuriMutex* mutex;
    GapSvcEventHandler* handler;
    uint16_t conn_handle;
    uint8_t peer_addr[6]; // last connect target, for the log
    bool peer_set;
    bool hanging_up; // disconnect sent, waiting for its event
    uint32_t connect_started;
    uint8_t value_char; // index of the selected characteristic
    uint16_t chars_end; // end handle of the service whose chars are listed
    uint16_t descs_end; // descriptor search stops before the next declaration
    uint16_t desc_handle; // 0x2901 of the selected characteristic, 0 = none
    bool reading_desc; // next read response is the descriptor name, not the value
    bool reading_long; // read blob responses append to the value
    bool confirm_pending; // indication seen, its confirmation waits for the app thread
    bool passkey_pending; // peer wants a passkey we cannot enter, answer 0 from the tick
    bool authorize_pending; // peer asks for authorization, tick allows it
    bool rebond_pending; // peer lost its keys, tick allows a rebond
    BleGattInfo info;
};

static inline void ble_gatt_put16(uint8_t* p, uint16_t v) {
    p[0] = v & 0xFF;
    p[1] = v >> 8;
}

void ble_gatt_lock(BleGatt* gatt);
void ble_gatt_unlock(BleGatt* gatt);

/** called locked, unlocks. state is set before the command: its events may arrive
 * before the call returns */
bool ble_gatt_command(
    BleGatt* gatt,
    BleGattState state,
    uint16_t opcode,
    const uint8_t* params,
    uint8_t len);

/** ble event thread entry, claims only events of our own central link */
BleEventAckStatus ble_gatt_event_handler(void* event, void* context);
