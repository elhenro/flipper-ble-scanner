#pragma once

#include "view.h"
#include <storage/storage.h>

// snapshot readable over usb cli: storage read /ext/apps_data/ble_scanner/scan.log
#define BLE_SCAN_LOG_PATH APP_DATA_PATH("scan.log")
// characteristic dump, own file: the scan snapshot is rewritten every 2s
#define BLE_GATT_LOG_PATH APP_DATA_PATH("gatt.log")

/** usb power present: the log is only read over usb, skip sd writes on battery */
bool ble_scan_log_enabled(void);
/** model snapshot as text, fast enough to run under the model lock */
void ble_scan_log_format(FuriString* out, const BleViewModel* model);
bool ble_scan_log_save(Storage* storage, const FuriString* text);
bool ble_scan_log_save_gatt(Storage* storage, const FuriString* text);
