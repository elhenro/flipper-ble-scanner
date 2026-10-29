#include "view.h"
#include "uuid.h"
#include "vendor.h"

#include <stdio.h>

#define ROW_H   10
#define LIST_Y0 19

static void ble_view_gatt_row(
    Canvas* canvas,
    size_t row,
    bool selected,
    const char* left,
    const char* right) {
    int32_t y = LIST_Y0 + row * ROW_H;
    if(selected) {
        canvas_draw_box(canvas, 0, y - 8, 128, ROW_H);
        canvas_set_color(canvas, ColorWhite);
    }
    canvas_draw_str(canvas, 2, y, left);
    if(right) canvas_draw_str_aligned(canvas, 126, y, AlignRight, AlignBottom, right);
    canvas_set_color(canvas, ColorBlack);
}

// state line shared by all gatt screens, NULL when nothing to report
static const char* ble_view_gatt_status(const BleGattInfo* gatt, char* buf, size_t size) {
    switch(gatt->state) {
    case BleGattStateConnecting:
        return "Connecting...";
    case BleGattStateBusy:
        return "Working...";
    case BleGattStateFailed:
        snprintf(buf, size, "Link down: 0x%02X", gatt->error);
        return buf;
    case BleGattStateIdle:
        return "Disconnected";
    default:
        if(gatt->error) {
            snprintf(buf, size, "Error 0x%02X", gatt->error);
            return buf;
        }
        return NULL;
    }
}

static void ble_view_gatt_header(Canvas* canvas, const char* title, const BleGattInfo* gatt) {
    char status[24];
    const char* text = ble_view_gatt_status(gatt, status, sizeof(status));
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 0, 8, text ? text : title);
    canvas_draw_line(canvas, 0, 10, 127, 10);
}

static void ble_view_props(char* buf, uint8_t props) {
    buf[0] = props & BLE_GATT_PROP_READ ? 'R' : '-';
    buf[1] = props & (BLE_GATT_PROP_WRITE | BLE_GATT_PROP_WRITE_NR) ? 'W' : '-';
    buf[2] = props & BLE_GATT_PROP_NOTIFY ? 'N' : '-';
    buf[3] = props & BLE_GATT_PROP_INDICATE ? 'I' : '-';
    buf[4] = '\0';
}

static void ble_view_draw_services(Canvas* canvas, const BleViewModel* model) {
    const BleGattInfo* gatt = &model->gatt;
    char title[24];
    ble_vendor_display_name(title, sizeof(title), &model->peer);
    ble_view_gatt_header(canvas, title, gatt);
    for(size_t row = 0; row < BLE_VIEW_ROWS; row++) {
        size_t i = model->service_scroll + row;
        if(i >= gatt->service_count) break;
        char name[24];
        ble_uuid_format(name, sizeof(name), gatt->services[i].uuid, gatt->services[i].uuid_len);
        ble_view_gatt_row(canvas, row, i == model->service_sel, name, NULL);
    }
}

static void ble_view_draw_chars(Canvas* canvas, const BleViewModel* model) {
    const BleGattInfo* gatt = &model->gatt;
    char title[24];
    const BleGattService* service = &gatt->services[model->service_sel];
    ble_uuid_format(title, sizeof(title), service->uuid, service->uuid_len);
    ble_view_gatt_header(canvas, title, gatt);
    for(size_t row = 0; row < BLE_VIEW_ROWS; row++) {
        size_t i = model->char_scroll + row;
        if(i >= gatt->char_count) break;
        char name[24];
        char props[5];
        ble_uuid_format(name, sizeof(name), gatt->chars[i].uuid, gatt->chars[i].uuid_len);
        ble_view_props(props, gatt->chars[i].props);
        ble_view_gatt_row(canvas, row, i == model->char_sel, name, props);
    }
}

static void ble_view_draw_value(Canvas* canvas, const BleViewModel* model) {
    const BleGattInfo* gatt = &model->gatt;
    const BleGattChar* chr = &gatt->chars[model->char_sel];
    char line[32];
    ble_uuid_format(line, sizeof(line), chr->uuid, chr->uuid_len);
    ble_view_gatt_header(canvas, line, gatt);

    // 8 bytes per hex line, 3 lines, then printable ascii of the same bytes
    for(uint8_t l = 0; l < 3 && l * 8 < gatt->value_len; l++) {
        size_t pos = 0;
        for(uint8_t i = l * 8; i < gatt->value_len && i < l * 8 + 8; i++) {
            pos += snprintf(line + pos, sizeof(line) - pos, "%02X ", gatt->value[i]);
        }
        canvas_draw_str(canvas, 0, 20 + l * 9, line);
    }
    size_t n = 0;
    for(; n < gatt->value_len && n < 24; n++) {
        char c = gatt->value[n];
        line[n] = (c >= 0x20 && c < 0x7F) ? c : '.';
    }
    line[n] = '\0';
    if(gatt->value_len) canvas_draw_str(canvas, 0, 48, line);

    char props[5];
    ble_view_props(props, chr->props);
    snprintf(line, sizeof(line), "%s %ub #%lu", props, gatt->value_len, gatt->value_updates);
    canvas_draw_str(canvas, 0, 62, line);
    canvas_draw_str_aligned(
        canvas, 127, 62, AlignRight, AlignBottom, gatt->notifying ? "> stop ntf" : "OK rd >ntf");
}

void ble_view_draw_gatt(Canvas* canvas, const BleViewModel* model) {
    canvas_set_font(canvas, FontSecondary);
    if(model->screen == BleScreenServices) {
        ble_view_draw_services(canvas, model);
    } else if(model->screen == BleScreenChars) {
        ble_view_draw_chars(canvas, model);
    } else {
        ble_view_draw_value(canvas, model);
    }
}
