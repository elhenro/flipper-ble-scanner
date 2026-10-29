#pragma once

#include "view.h"
#include <storage/storage.h>

// snapshot readable over usb cli: storage read /ext/apps_data/ble_scanner/scan.log
#define BLE_SCAN_LOG_PATH APP_DATA_PATH("scan.log")

bool ble_scan_log_write(Storage* storage, const BleViewModel* model);
