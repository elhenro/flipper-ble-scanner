#include "view.h"
#include "distance.h"
#include "vendor.h"

#include <stdio.h>

void ble_view_header(Canvas* canvas, const char* left, const char* right) {
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 0, 8, left);
    if(right) canvas_draw_str_aligned(canvas, 127, 8, AlignRight, AlignBottom, right);
    canvas_draw_line(canvas, 0, 10, 127, 10);
}

void ble_view_row(
    Canvas* canvas,
    size_t row,
    bool selected,
    const char* left,
    const char* mid,
    const char* right) {
    int32_t y = BLE_VIEW_LIST_Y0 + row * BLE_VIEW_ROW_H;
    if(selected) {
        canvas_draw_box(canvas, 0, y - 8, 128, BLE_VIEW_ROW_H);
        canvas_set_color(canvas, ColorWhite);
    }
    canvas_draw_str(canvas, 2, y, left);
    if(mid) canvas_draw_str_aligned(canvas, 88, y, AlignRight, AlignBottom, mid);
    if(right) canvas_draw_str_aligned(canvas, 126, y, AlignRight, AlignBottom, right);
    canvas_set_color(canvas, ColorBlack);
}

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
    if(model->scanning) {
        snprintf(line, sizeof(line), "SCAN %u dev ref%d", model->count, model->ref);
    } else {
        snprintf(line, sizeof(line), "Scan failed: 0x%02X", model->scan_status);
    }
    ble_view_header(canvas, line, "<info");

    if(model->count == 0) {
        canvas_draw_str_aligned(canvas, 64, 36, AlignCenter, AlignCenter, "Listening...");
        return;
    }

    for(size_t row = 0; row < BLE_VIEW_ROWS; row++) {
        size_t i = model->scroll + row;
        if(i >= model->count) break;
        const BleDevice* dev = &model->devices[i];
        char label[12];
        char rssi[8];
        ble_vendor_display_name(label, sizeof(label), dev);
        snprintf(rssi, sizeof(rssi), "%d", (int)dev->rssi_avg);
        snprintf(line, sizeof(line), "%.1fm", (double)ble_distance_m(dev->rssi_avg, model->ref));
        ble_view_row(canvas, row, i == model->selected, label, rssi, line);
    }
}

static void ble_view_draw_detail(Canvas* canvas, const BleViewModel* model) {
    const BleDevice* dev = &model->peer;
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

// "mf 4C 00 19" style dump, "-" when empty. buf 40 is enough
static void ble_view_hex(
    char* buf,
    size_t size,
    const char* tag,
    const uint8_t* data,
    size_t len,
    size_t offset,
    size_t max) {
    size_t used = MIN((size_t)snprintf(buf, size, "%s", tag), size - 1);
    size_t end = MIN(offset + max, len);
    if(end == offset) {
        snprintf(buf + used, size - used, " -");
        return;
    }
    for(size_t i = offset; i < end && used + 4 < size; i++) {
        used += snprintf(buf + used, size - used, " %02X", data[i]);
    }
}

static void ble_view_draw_vendor(Canvas* canvas, const BleViewModel* model) {
    const BleDevice* dev = &model->peer;
    char line[40];

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, "Vendor data");
    canvas_set_font(canvas, FontSecondary);

    if(dev->company != BLE_SCAN_NO_COMPANY) {
        const char* name = ble_vendor_company_name(dev->company);
        snprintf(line, sizeof(line), "co %04X %s", dev->company, name ? name : "?");
    } else {
        snprintf(line, sizeof(line), "co none");
    }
    canvas_draw_str(canvas, 0, 21, line);

    ble_view_hex(line, sizeof(line), "mf", dev->mf, dev->mf_len, 0, 6);
    canvas_draw_str(canvas, 0, 31, line);
    int32_t y = 31;
    if(dev->mf_len > 6) {
        ble_view_hex(line, sizeof(line), "mf", dev->mf, dev->mf_len, 6, 6);
        y += 10;
        canvas_draw_str(canvas, 0, y, line);
    }

    y += 10;
    if(dev->sd_uuid) {
        size_t used = snprintf(line, sizeof(line), "sd %04X", dev->sd_uuid);
        for(uint8_t i = 0; i < dev->sd_len && used + 4 < sizeof(line); i++) {
            used += snprintf(line + used, sizeof(line) - used, " %02X", dev->sd[i]);
        }
    } else {
        snprintf(line, sizeof(line), "sd -");
    }
    canvas_draw_str(canvas, 0, y, line);

    y += 10;
    line[0] = '\0';
    size_t used = 0;
    for(uint8_t i = 0; i < dev->uuid_count && used + 6 < sizeof(line); i++) {
        used += snprintf(line + used, sizeof(line) - used, "%s%04X", i ? " " : "uu ", dev->uuids[i]);
    }
    canvas_draw_str(canvas, 0, y, dev->uuid_count ? line : "uu -");
}

void ble_view_draw(Canvas* canvas, const BleViewModel* model) {
    canvas_clear(canvas);
    if(!model->supported) {
        ble_view_draw_unsupported(canvas);
    } else if(model->screen == BleScreenDetail) {
        if(model->detail_page) {
            ble_view_draw_vendor(canvas, model);
        } else {
            ble_view_draw_detail(canvas, model);
        }
    } else if(model->screen == BleScreenInfo) {
        ble_view_draw_info(canvas, model);
    } else if(model->screen >= BleScreenServices) {
        ble_view_draw_gatt(canvas, model);
    } else {
        ble_view_draw_list(canvas, model);
    }
}
