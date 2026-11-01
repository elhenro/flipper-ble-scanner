#pragma once

#include "scan.h"
#include "gatt.h"
#include <gui/canvas.h>

#define BLE_VIEW_ROWS    5
#define BLE_VIEW_ROW_H   10
#define BLE_VIEW_LIST_Y0 19
// four hex rows of 8 bytes, all that fits above the hint lines
#define BLE_VIEW_EDIT_MAX 32

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
    uint8_t count;
    uint8_t selected;
    uint8_t scroll;
    BleScreen screen;
    // 0 = summary, 1 = raw vendor data
    uint8_t detail_page;
    bool supported;
    bool scanning;
    uint8_t scan_status;
    uint32_t reports;
    // device shown in details and connected to. a copy, so it can't turn into another one
    BleDevice peer;
    // gatt session, snapshot refreshed by the main loop
    BleGattInfo gatt;
    uint8_t service_sel;
    uint8_t service_scroll;
    uint8_t char_sel;
    uint8_t char_scroll;
    uint8_t info_page;
    // screen Left came from, Back on the info pages returns to it
    BleScreen info_back;
    // 0 idle, 1 dumping, 2 saved, 3 failed (view reads, app drives)
    uint8_t dump_state;
    uint8_t dump_done;
    uint8_t dump_total;
    // value editor: hex nibbles of a write, cursor in nibbles
    bool editing;
    uint8_t edit_len;
    uint8_t edit_pos;
    uint8_t edit_buf[BLE_GATT_VALUE_LEN];
} BleViewModel;

void ble_view_draw(Canvas* canvas, const BleViewModel* model);

/** scrolls a BLE_VIEW_ROWS window just far enough to show sel */
static inline void ble_view_scroll(uint8_t sel, uint8_t* scroll) {
    if(sel < *scroll) *scroll = sel;
    if(sel >= *scroll + BLE_VIEW_ROWS) *scroll = sel - BLE_VIEW_ROWS + 1;
}

/** top line with a rule below, right may be NULL */
void ble_view_header(Canvas* canvas, const char* left, const char* right);
/** list row, inverted when selected. mid ends at x 88, right at 126, both may be NULL */
void ble_view_row(
    Canvas* canvas,
    size_t row,
    bool selected,
    const char* left,
    const char* mid,
    const char* right);

/** "AA:BB:CC:DD:EE:FF", buf >= 18 */
void ble_view_format_addr(char* buf, const uint8_t* addr);

/** help pages (view_info.c) */
#define BLE_INFO_PAGES 6
void ble_view_draw_info(Canvas* canvas, const BleViewModel* model);

/** services, characteristics and value screens (view_gatt.c) */
void ble_view_draw_gatt(Canvas* canvas, const BleViewModel* model);
