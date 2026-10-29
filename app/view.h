#pragma once

#include "scan.h"
#include "gatt.h"
#include <gui/canvas.h>

#define BLE_VIEW_ROWS 5

typedef enum {
    BleScreenList,
    BleScreenDetail,
    BleScreenInfo,
    BleScreenServices,
    BleScreenChars,
    BleScreenValue,
} BleScreen;

typedef struct {
    BleDevice devices[BLE_SCAN_MAX_DEVICES];
    size_t count;
    size_t selected;
    size_t scroll;
    BleScreen screen;
    bool supported;
    bool scanning;
    uint8_t scan_status;
    int8_t ref;
    uint32_t reports;
    // gatt session, snapshot refreshed by the main loop
    BleDevice peer;
    BleGattInfo gatt;
    uint8_t service_sel;
    uint8_t service_scroll;
    uint8_t char_sel;
    uint8_t char_scroll;
    uint8_t info_page;
} BleViewModel;

void ble_view_draw(Canvas* canvas, const BleViewModel* model);

/** "AA:BB:CC:DD:EE:FF", buf >= 18 */
void ble_view_format_addr(char* buf, const uint8_t* addr);

/** help pages (view_info.c) */
#define BLE_INFO_PAGES 2
void ble_view_draw_info(Canvas* canvas, const BleViewModel* model);

/** services, characteristics and value screens (view_gatt.c) */
void ble_view_draw_gatt(Canvas* canvas, const BleViewModel* model);
