#include "view.h"
#include "uuid.h"
#include "vendor.h"

#include <stdio.h>

// att error codes, core spec vol 3 part f 3.4.1.1
static const char* ble_view_att_error(uint8_t code) {
    switch(code) {
    case 0x01:
        return "bad handle";
    case 0x02:
        return "read denied";
    case 0x03:
        return "write denied";
    case 0x05:
        return "need pair";
    case 0x06:
        return "unsupported";
    case 0x08:
        return "auth busy";
    case 0x0A:
        return "not found";
    case 0x0C:
        return "key short";
    case 0x0E:
        return "need crypt";
    case 0x0F:
        return "unlikely";
    case 0x12:
        return "out of res";
    default:
        return NULL;
    }
}

// hci disconnection reasons, spec vol 1 part f 2.1
static const char* ble_view_link_error(uint8_t code) {
    switch(code) {
    case 0x08:
        return "timeout";
    case 0x13:
        return "peer left";
    case 0x14:
        return "no resources";
    case 0x15:
        return "power off";
    case 0x16:
        return "local left";
    case 0x3B:
        return "bad params";
    case 0x3E:
        return "no link";
    default:
        return NULL;
    }
}

// state line shared by all gatt screens, NULL when nothing to report
static const char* ble_view_gatt_status(const BleGattInfo* gatt, char* buf, size_t size) {
    // the prompt wins over everything: pairing blocks the link until it is answered
    if(gatt->pair_confirm) {
        snprintf(buf, size, "Pair %06lu?", (unsigned long)gatt->pair_numeric);
        return buf;
    }
    switch(gatt->state) {
    case BleGattStateConnecting:
        return "Connecting...";
    case BleGattStateBusy:
        return "Working...";
    case BleGattStateFailed: {
        const char* name = ble_view_link_error(gatt->error);
        if(name) snprintf(buf, size, "Down 0x%02X %s", gatt->error, name);
        else snprintf(buf, size, "Link down: 0x%02X", gatt->error);
        return buf;
    }
    case BleGattStateIdle:
        return "Disconnected";
    default:
        if(gatt->error) {
            const char* name = ble_view_att_error(gatt->error);
            if(name) snprintf(buf, size, "Err 0x%02X %s", gatt->error, name);
            else snprintf(buf, size, "Error 0x%02X", gatt->error);
            return buf;
        }
        // stays for the rest of the link: the peer either keeps its keys or doesn't
        if(gatt->pair_state == 2) return "Paired";
        if(gatt->pair_state == 3) {
            snprintf(buf, size, "Pair fail 0x%02X", gatt->pair_reason);
            return buf;
        }
        return NULL;
    }
}

static void ble_view_gatt_header(Canvas* canvas, const char* title, const BleGattInfo* gatt) {
    char status[24];
    const char* text = ble_view_gatt_status(gatt, status, sizeof(status));
    ble_view_header(canvas, text ? text : title, NULL);
}

static void ble_view_props(char* buf, uint8_t props) {
    buf[0] = props & BLE_GATT_PROP_READ ? 'R' : '-';
    buf[1] = props & (BLE_GATT_PROP_WRITE | BLE_GATT_PROP_WRITE_NR) ? 'W' : '-';
    buf[2] = props & BLE_GATT_PROP_NOTIFY ? 'N' : '-';
    buf[3] = props & BLE_GATT_PROP_INDICATE ? 'I' : '-';
    buf[4] = '\0';
}

// one line of body text for a screen with no rows to show
static void ble_view_empty(Canvas* canvas, const char* text) {
    canvas_draw_str_aligned(canvas, 64, 36, AlignCenter, AlignCenter, text);
}

static void ble_view_draw_services(Canvas* canvas, const BleViewModel* model) {
    const BleGattInfo* gatt = &model->gatt;
    char title[24];
    ble_vendor_display_name(title, sizeof(title), &model->peer);
    ble_view_gatt_header(canvas, title, gatt);
    if(gatt->service_count == 0) {
        const char* msg;
        switch(gatt->state) {
        case BleGattStateConnecting:
            msg = "Back cancels";
            break;
        case BleGattStateBusy:
            msg = "Discovering...";
            break;
        case BleGattStateFailed:
            msg = "OK retry  Back up";
            break;
        case BleGattStateIdle:
            msg = "Disconnected";
            break;
        default:
            msg = gatt->error ? "OK retry  Back up" :
                  gatt->services_done ? "No services  Back up" :
                                        "Discovering...";
            break;
        }
        ble_view_empty(canvas, msg);
    }
    for(size_t row = 0; row < BLE_VIEW_ROWS; row++) {
        size_t i = model->service_scroll + row;
        if(i >= gatt->service_count) break;
        char name[24];
        ble_uuid_format(name, sizeof(name), gatt->services[i].uuid, gatt->services[i].uuid_len);
        ble_view_row(canvas, row, i == model->service_sel, name, NULL, NULL);
    }
}

static void ble_view_draw_chars(Canvas* canvas, const BleViewModel* model) {
    const BleGattInfo* gatt = &model->gatt;
    char title[24] = "";
    // the cursor can outlive a shorter rediscovery, draw what the count allows
    if(gatt->service_count) {
        uint8_t sel = model->service_sel < gatt->service_count ? model->service_sel : 0;
        const BleGattService* service = &gatt->services[sel];
        ble_uuid_format(title, sizeof(title), service->uuid, service->uuid_len);
    }
    // dump progress replaces the service name, the state line stays free for link errors
    if(model->dump_state == 1) {
        snprintf(title, sizeof(title), "Dump %u/%u", model->dump_done, model->dump_total);
    } else if(model->dump_state == 2) {
        snprintf(title, sizeof(title), "Dump saved");
    } else if(model->dump_state == 3) {
        snprintf(title, sizeof(title), "Dump failed");
    }
    ble_view_gatt_header(canvas, title, gatt);
    if(gatt->char_count == 0) {
        const char* msg;
        switch(gatt->state) {
        case BleGattStateConnecting:
            msg = "Connecting...";
            break;
        case BleGattStateBusy:
            msg = "Discovering...";
            break;
        case BleGattStateFailed:
            msg = "OK retry  Back up";
            break;
        case BleGattStateIdle:
            msg = "Disconnected";
            break;
        default:
            msg = gatt->error ? "OK retry  Back up" :
                  gatt->service_count ? "OK rescan  Back up" :
                                        "Back up";
            break;
        }
        ble_view_empty(canvas, msg);
    }
    for(size_t row = 0; row < BLE_VIEW_ROWS; row++) {
        size_t i = model->char_scroll + row;
        if(i >= gatt->char_count) break;
        char name[24];
        char props[5];
        ble_uuid_format(name, sizeof(name), gatt->chars[i].uuid, gatt->chars[i].uuid_len);
        ble_view_props(props, gatt->chars[i].props);
        ble_view_row(canvas, row, i == model->char_sel, name, NULL, props);
    }
}

static void ble_view_draw_value(Canvas* canvas, const BleViewModel* model) {
    const BleGattInfo* gatt = &model->gatt;
    char line[32] = "";
    // the cursor can outlive a shorter rediscovery or a dropped link
    bool has_chr = gatt->char_count != 0;
    uint8_t sel = has_chr && model->char_sel < gatt->char_count ? model->char_sel : 0;
    if(has_chr) {
        ble_uuid_format(line, sizeof(line), gatt->chars[sel].uuid, gatt->chars[sel].uuid_len);
    }
    // the 0x2901 name says more than a vendor uuid, when the device has one
    ble_view_gatt_header(canvas, gatt->desc[0] ? gatt->desc : line, gatt);

    if(model->editing) {
        // 8 bytes per row, 16 px apart: 6 px font leaves no room for closer
        for(uint8_t i = 0; i < model->edit_len; i++) {
            char hex[3];
            snprintf(hex, sizeof(hex), "%02X", model->edit_buf[i]);
            int32_t x = (i % 8) * 16;
            int32_t y = 20 + (i / 8) * 9;
            canvas_draw_str(canvas, x, y, hex);
            if(i == model->edit_pos / 2) {
                // underline the nibble being changed
                int32_t nx = x + (model->edit_pos % 2 ? 6 : 0);
                canvas_draw_line(canvas, nx, y + 2, nx + 5, y + 2);
            }
        }
        canvas_draw_str(canvas, 0, 54, "Up/Dn nibble  Back esc");
        canvas_draw_str_aligned(canvas, 127, 62, AlignRight, AlignBottom, "OK send");
        return;
    }

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

    uint8_t props_bits = has_chr ? gatt->chars[sel].props : 0;
    if(!has_chr) {
        ble_view_empty(canvas, "No characteristic");
    } else if(gatt->value_len == 0) {
        if(!(props_bits & BLE_GATT_PROP_READ)) {
            ble_view_empty(canvas, "not readable");
        } else if(gatt->state == BleGattStateBusy) {
            ble_view_empty(canvas, "Reading...");
        } else if(gatt->state == BleGattStateFailed || gatt->error) {
            ble_view_empty(canvas, "read failed");
        } else {
            ble_view_empty(canvas, "empty value");
        }
    }

    char props[5];
    ble_view_props(props, props_bits);
    snprintf(line, sizeof(line), "%s %ub #%lu", props, gatt->value_len, gatt->value_updates);
    canvas_draw_str(canvas, 0, 62, line);
    // notify needs the cccd, found by a descriptor search once the screen opens
    bool can_notify = props_bits & (BLE_GATT_PROP_NOTIFY | BLE_GATT_PROP_INDICATE);
    bool can_write = props_bits & (BLE_GATT_PROP_WRITE | BLE_GATT_PROP_WRITE_NR);
    const char* keys;
    if(gatt->pair_confirm) keys = "OK yes Back no";
    else if(gatt->truncated && !gatt->long_done) keys = "OK rest";
    else if(gatt->write_done) keys = "sent";
    else if(gatt->notifying) keys = "> stop ntf";
    else if(gatt->cccd && can_notify) keys = (props_bits & BLE_GATT_PROP_READ) ? "OK rd >ntf" : "> ntf";
    else if(can_notify && !(gatt->descs_done && gatt->desc_done)) keys = "find cccd";
    else if(can_notify) keys = (props_bits & BLE_GATT_PROP_READ) ? "OK read" : "no cccd";
    else if(can_write) keys = "hold OK write";
    else keys = (props_bits & BLE_GATT_PROP_READ) ? "OK read" : "no read";
    canvas_draw_str_aligned(canvas, 127, 62, AlignRight, AlignBottom, keys);
}

void ble_view_draw_gatt(Canvas* canvas, const BleViewModel* model) {
    if(model->screen == BleScreenServices) {
        ble_view_draw_services(canvas, model);
    } else if(model->screen == BleScreenChars) {
        ble_view_draw_chars(canvas, model);
    } else {
        ble_view_draw_value(canvas, model);
    }
}
