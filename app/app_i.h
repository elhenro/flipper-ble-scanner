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
    // service dump: reads every readable characteristic, one at a time
    bool dumping;
    bool dump_reading; // read in flight for dump_char
    uint8_t dump_char;
    uint8_t dump_index;
    uint8_t dump_ok;
    uint8_t dump_err;
    uint8_t dump_total;
    FuriString* dump_log;
    FuriString* dump_pending; // finished text, written after the model lock is released
} BleScannerApp;

void ble_scanner_select(BleScannerApp* app, uint8_t index);
void ble_scanner_scan_resume(BleScannerApp* app);
/** reads every readable characteristic of the open service, logs to gatt.log */
void ble_scanner_dump_start(BleScannerApp* app);
void ble_scanner_dump_cancel(BleScannerApp* app);

/** returns false when the app should exit (input.c) */
bool ble_scanner_handle_input(BleScannerApp* app, const InputEvent* event);
