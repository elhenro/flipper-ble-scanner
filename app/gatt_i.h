#pragma once

#include "gatt.h"

#include <furi_ble/event_dispatcher.h>

#define BLE_GATT_NO_HANDLE 0xFFFF

struct BleGatt {
    FuriMutex* mutex;
    GapSvcEventHandler* handler;
    uint16_t conn_handle;
    uint32_t connect_started;
    BleGattInfo info;
};

/** ble event thread entry, claims only events of our own central link */
BleEventAckStatus ble_gatt_event_handler(void* event, void* context);
