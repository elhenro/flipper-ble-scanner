#pragma once

#include "scan.h"
#include "gatt.h"
#include "view.h"

#include <gui/gui.h>
#include <input/input.h>
#include <storage/storage.h>

typedef struct {
    Gui* gui;
    ViewPort* view_port;
    FuriMessageQueue* input_queue;
    FuriMutex* model_mutex;
    BleViewModel model;
    BleScan* scan;
    BleGatt* gatt;
    Storage* storage;
    // selection follows the device across re-sorts
    uint8_t selected_addr[6];
    bool has_selection;
} BleScannerApp;

void ble_scanner_select(BleScannerApp* app, size_t index);
void ble_scanner_scan_resume(BleScannerApp* app);

/** returns false when the app should exit (input.c) */
bool ble_scanner_handle_input(BleScannerApp* app, const InputEvent* event);
