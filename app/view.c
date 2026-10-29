#include "view.h"
#include "distance.h"
#include "vendor.h"

#include <stdio.h>

#define ROW_H   10
#define LIST_Y0 19

void ble_view_format_addr(char* buf, const uint8_t* addr) {
    snprintf(
        buf,
        18,
        "%02X:%02X:%02X:%02X:%02X:%02X",
        addr[5],
        addr[4],
        addr[3],
        addr[2],
        addr[1],
        addr[0]);
}

static void ble_view_draw_unsupported(Canvas* canvas) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 4, AlignCenter, AlignTop, "No BLE scan support");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 64, 22, AlignCenter, AlignTop, "Radio runs light stack");
    canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignTop, "(peripheral only).");
    canvas_draw_str_aligned(canvas, 64, 42, AlignCenter, AlignTop, "Flash full stack + patched fw");
}

static void ble_view_draw_list(Canvas* canvas, const BleViewModel* model) {
    char line[32];
    canvas_set_font(canvas, FontSecondary);
    if(model->scanning) {
        snprintf(line, sizeof(line), "SCAN %u dev ref%d", model->count, model->ref);
    } else {
        snprintf(line, sizeof(line), "Scan failed: 0x%02X", model->scan_status);
    }
    canvas_draw_str(canvas, 0, 8, line);
    canvas_draw_str_aligned(canvas, 127, 8, AlignRight, AlignBottom, "<info");
    canvas_draw_line(canvas, 0, 10, 127, 10);

    if(model->count == 0) {
        canvas_draw_str_aligned(canvas, 64, 36, AlignCenter, AlignCenter, "Listening...");
        return;
    }

    for(size_t row = 0; row < BLE_VIEW_ROWS; row++) {
        size_t i = model->scroll + row;
        if(i >= model->count) break;
        const BleDevice* dev = &model->devices[i];
        int32_t y = LIST_Y0 + row * ROW_H;

        if(i == model->selected) {
            canvas_draw_box(canvas, 0, y - 8, 128, ROW_H);
            canvas_set_color(canvas, ColorWhite);
        }
        char label[12];
        ble_vendor_display_name(label, sizeof(label), dev);
        canvas_draw_str(canvas, 2, y, label);
        snprintf(line, sizeof(line), "%d", (int)dev->rssi_avg);
        canvas_draw_str_aligned(canvas, 88, y, AlignRight, AlignBottom, line);
        snprintf(line, sizeof(line), "%.1fm", (double)ble_distance_m(dev->rssi_avg, model->ref));
        canvas_draw_str_aligned(canvas, 126, y, AlignRight, AlignBottom, line);
        canvas_set_color(canvas, ColorBlack);
    }
}

static void ble_view_draw_detail(Canvas* canvas, const BleViewModel* model) {
    const BleDevice* dev = &model->devices[model->selected];
    char line[40];

    canvas_set_font(canvas, FontPrimary);
    ble_vendor_display_name(line, 20, dev);
    canvas_draw_str(canvas, 0, 10, line);

    canvas_set_font(canvas, FontSecondary);
    ble_view_format_addr(line, dev->addr);
    canvas_draw_str(canvas, 0, 21, line);
    canvas_draw_str(canvas, 104, 21, dev->addr_type ? "rand" : "pub");
    if(dev->connectable) canvas_draw_str(canvas, 123, 21, "C");

    const char* vendor = ble_vendor_label(dev);
    if(dev->company != BLE_SCAN_NO_COMPANY) {
        snprintf(line, sizeof(line), "%s  co 0x%04X", vendor ? vendor : "?", dev->company);
    } else {
        snprintf(line, sizeof(line), "%s", vendor ? vendor : "no vendor data");
    }
    canvas_draw_str(canvas, 0, 31, line);

    snprintf(
        line,
        sizeof(line),
        "RSSI %d avg %.1f n%lu",
        dev->rssi,
        (double)dev->rssi_avg,
        dev->seen_count);
    canvas_draw_str(canvas, 0, 41, line);
    snprintf(
        line,
        sizeof(line),
        "~%.1fm (ref %d, n %.1f)",
        (double)ble_distance_m(dev->rssi_avg, model->ref),
        model->ref,
        (double)BLE_DISTANCE_N);
    canvas_draw_str(canvas, 0, 51, line);
    canvas_draw_str(
        canvas, 0, 62, dev->connectable ? "OK conn  </> ref  hold: cal" : "</> ref dB  hold OK: cal");
}

void ble_view_draw(Canvas* canvas, const BleViewModel* model) {
    canvas_clear(canvas);
    if(!model->supported) {
        ble_view_draw_unsupported(canvas);
    } else if(model->screen == BleScreenDetail && model->selected < model->count) {
        ble_view_draw_detail(canvas, model);
    } else if(model->screen == BleScreenInfo) {
        ble_view_draw_info(canvas, model);
    } else if(model->screen >= BleScreenServices) {
        ble_view_draw_gatt(canvas, model);
    } else {
        ble_view_draw_list(canvas, model);
    }
}
