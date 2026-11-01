#include "app_i.h"

#include <string.h>

#define TAG "BleScanner"

static bool ble_input_is_press(const InputEvent* event) {
    return event->type == InputTypeShort || event->type == InputTypeRepeat;
}

// up/down with wraparound, other keys keep the cursor. count > 0
static uint8_t ble_input_step(uint8_t sel, uint8_t count, InputKey key) {
    if(sel >= count) return count - 1;
    if(key == InputKeyUp) return sel ? sel - 1 : count - 1;
    if(key == InputKeyDown) return (sel + 1) % count;
    return sel;
}

// cursor over a list shown BLE_VIEW_ROWS at a time
static void ble_input_move(uint8_t* sel, uint8_t* scroll, uint8_t count, InputKey key) {
    if(!count) return;
    *sel = ble_input_step(*sel, count, key);
    ble_view_scroll(*sel, scroll);
}

// a reconnect can bring fewer services or chars than the cursor was left on
static void ble_input_clamp(uint8_t* sel, uint8_t* scroll, uint8_t count) {
    if(!count) return;
    if(*sel >= count) *sel = count - 1;
    ble_view_scroll(*sel, scroll);
}

static void ble_input_connect(BleScannerApp* app) {
    BleViewModel* m = &app->model;
    // one gap procedure at a time: observation must end before connecting
    ble_scan_stop(app->scan);
    m->scanning = false;
    m->service_sel = m->service_scroll = 0;
    ble_gatt_connect(app->gatt, &m->peer);
    m->screen = BleScreenServices;
}

static bool ble_input_list(BleScannerApp* app, const InputEvent* event) {
    BleViewModel* m = &app->model;
    if(event->key == InputKeyBack && event->type == InputTypeShort) return false;
    if(!m->count) return true;
    if(event->key == InputKeyOk && event->type == InputTypeShort) {
        m->peer = m->devices[m->selected];
        m->detail_page = 0;
        m->screen = BleScreenDetail;
    } else if(ble_input_is_press(event)) {
        ble_scanner_select(app, ble_input_step(m->selected, m->count, event->key));
    }
    return true;
}

static void ble_input_info(BleScannerApp* app, const InputEvent* event) {
    BleViewModel* m = &app->model;
    if(event->type != InputTypeShort) return;
    if(event->key == InputKeyUp) {
        m->info_page = (m->info_page + BLE_INFO_PAGES - 1) % BLE_INFO_PAGES;
    } else if(event->key == InputKeyDown) {
        m->info_page = (m->info_page + 1) % BLE_INFO_PAGES;
    } else {
        m->screen = m->info_back;
    }
}

static void ble_input_detail(BleScannerApp* app, const InputEvent* event) {
    BleViewModel* m = &app->model;
    if(event->key == InputKeyBack && event->type == InputTypeShort) {
        m->screen = BleScreenList;
    } else if(
        (event->key == InputKeyUp || event->key == InputKeyDown) &&
        event->type == InputTypeShort) {
        m->detail_page ^= 1;
    } else if(event->key == InputKeyOk && event->type == InputTypeShort) {
        if(m->peer.connectable) ble_input_connect(app);
    }
}

static void ble_input_services(BleScannerApp* app, const InputEvent* event) {
    BleViewModel* m = &app->model;
    const BleGattInfo* gatt = &m->gatt;
    ble_input_clamp(&m->service_sel, &m->service_scroll, gatt->service_count);
    if(event->key == InputKeyBack && event->type == InputTypeShort) {
        ble_scanner_dump_cancel(app);
        ble_gatt_disconnect(app->gatt);
        ble_scanner_scan_resume(app);
        m->screen = BleScreenDetail;
    } else if(event->key == InputKeyOk && event->type == InputTypeShort) {
        if(gatt->state == BleGattStateFailed) {
            ble_input_connect(app);
        } else if(gatt->state == BleGattStateReady) {
            if(gatt->service_count) {
                m->char_sel = m->char_scroll = 0;
                m->dump_state = 0;
                ble_gatt_discover_chars(app->gatt, m->service_sel);
                m->screen = BleScreenChars;
            } else {
                // empty or failed discovery, let OK try it again
                ble_gatt_discover_services(app->gatt);
            }
        }
    } else if(ble_input_is_press(event)) {
        ble_input_move(&m->service_sel, &m->service_scroll, gatt->service_count, event->key);
    }
}

static void ble_input_chars(BleScannerApp* app, const InputEvent* event) {
    BleViewModel* m = &app->model;
    const BleGattInfo* gatt = &m->gatt;
    ble_input_clamp(&m->char_sel, &m->char_scroll, gatt->char_count);
    // a shorter rediscovery also moves the service cursor
    ble_input_clamp(&m->service_sel, &m->service_scroll, gatt->service_count);
    if(event->key == InputKeyBack && event->type == InputTypeShort) {
        ble_scanner_dump_cancel(app);
        m->dump_state = 0;
        m->screen = BleScreenServices;
    } else if(app->dumping) {
        // a dump reads one char at a time, navigating would mix its values in
        return;
    } else if(event->key == InputKeyOk && event->type == InputTypeLong) {
        ble_scanner_dump_start(app);
    } else if(event->key == InputKeyOk && event->type == InputTypeShort) {
        if(gatt->state == BleGattStateFailed) {
            // same retry as the services screen, which is where a reconnect lands
            ble_input_connect(app);
            return;
        }
        if(gatt->state != BleGattStateReady) return;
        if(!gatt->char_count) {
            // an empty list is either a failed discovery or a service without chars
            if(m->service_sel < gatt->service_count) {
                ble_gatt_discover_chars(app->gatt, m->service_sel);
            } else {
                m->screen = BleScreenServices;
            }
            return;
        }
        ble_gatt_select(app->gatt, m->char_sel);
        if(gatt->chars[m->char_sel].props & BLE_GATT_PROP_READ) ble_gatt_read(app->gatt);
        m->screen = BleScreenValue;
    } else if(ble_input_is_press(event)) {
        ble_input_move(&m->char_sel, &m->char_scroll, gatt->char_count, event->key);
    }
}

// value editor: nibble cursor, send or escape
static bool ble_input_edit(BleScannerApp* app, const InputEvent* event) {
    BleViewModel* m = &app->model;
    uint8_t max_pos = m->edit_len * 2 - 1;
    if(event->key == InputKeyBack && event->type == InputTypeShort) {
        m->editing = false;
        return true;
    }
    if(event->key == InputKeyOk && event->type == InputTypeShort) {
        m->editing = false;
        ble_gatt_write(app->gatt, m->edit_buf, m->edit_len);
        return true;
    }
    if(!ble_input_is_press(event)) return true;
    switch(event->key) {
    case InputKeyLeft:
        m->edit_pos = m->edit_pos ? m->edit_pos - 1 : max_pos;
        break;
    case InputKeyRight:
        m->edit_pos = (m->edit_pos + 1) % (max_pos + 1);
        break;
    case InputKeyUp:
    case InputKeyDown: {
        // one nibble at a time, a byte is two of them
        uint8_t i = m->edit_pos / 2;
        uint8_t shift = m->edit_pos % 2 ? 0 : 4;
        int8_t step = event->key == InputKeyUp ? 1 : -1;
        uint8_t nib = (m->edit_buf[i] >> shift) & 0x0F;
        nib = (nib + step) & 0x0F;
        m->edit_buf[i] = (m->edit_buf[i] & ~(0x0F << shift)) | (nib << shift);
        break;
    }
    default:
        break;
    }
    return true;
}

static void ble_input_value(BleScannerApp* app, const InputEvent* event) {
    BleViewModel* m = &app->model;
    const BleGattInfo* gatt = &m->gatt;
    if(m->editing) {
        // a link that went away takes the write with it
        if(gatt->state != BleGattStateReady) m->editing = false;
        else ble_input_edit(app, event);
        return;
    }
    ble_input_clamp(&m->char_sel, &m->char_scroll, gatt->char_count);
    // link loss or a re-discovery can empty the char list under the value screen
    if(!gatt->char_count) {
        m->screen = BleScreenChars;
        return;
    }
    uint8_t props = gatt->chars[m->char_sel].props;
    bool ready = gatt->state == BleGattStateReady;
    // the peer waits for the answer, so this works in any state
    if(gatt->pair_confirm) {
        if(event->type != InputTypeShort) return;
        if(event->key == InputKeyOk) ble_gatt_pair_confirm(app->gatt, true);
        else if(event->key == InputKeyBack) ble_gatt_pair_confirm(app->gatt, false);
        return;
    }
    if(event->key == InputKeyBack && event->type == InputTypeShort) {
        if(gatt->notifying && ready) ble_gatt_set_notify(app->gatt, false);
        m->screen = BleScreenChars;
    } else if(event->type == InputTypeLong && event->key == InputKeyOk && ready &&
              (props & (BLE_GATT_PROP_WRITE | BLE_GATT_PROP_WRITE_NR))) {
        // start from what is on screen, an empty value gets one zero byte to edit
        m->edit_len = gatt->value_len ? gatt->value_len : 1;
        if(m->edit_len > BLE_VIEW_EDIT_MAX) m->edit_len = BLE_VIEW_EDIT_MAX;
        memset(m->edit_buf, 0, sizeof(m->edit_buf));
        memcpy(m->edit_buf, gatt->value, m->edit_len);
        m->edit_pos = 0;
        m->editing = true;
    } else if(event->type != InputTypeShort || !ready) {
        return;
    } else if(event->key == InputKeyOk && gatt->truncated && !gatt->long_done) {
        // the value did not fit the read chunk, read blob fetches the rest
        ble_gatt_read_long(app->gatt);
    } else if(event->key == InputKeyOk && (props & BLE_GATT_PROP_READ)) {
        ble_gatt_read(app->gatt);
    } else if(
        event->key == InputKeyRight && gatt->cccd &&
        (props & (BLE_GATT_PROP_NOTIFY | BLE_GATT_PROP_INDICATE))) {
        ble_gatt_set_notify(app->gatt, !gatt->notifying);
    }
}

bool ble_scanner_handle_input(BleScannerApp* app, const InputEvent* event) {
    BleViewModel* m = &app->model;
    // the open editor owns every key, Left included
    if(m->editing) {
        ble_input_value(app, event);
        return true;
    }
    // info is one key away everywhere; during a dump the chars screen keeps it
    if(event->key == InputKeyLeft && event->type == InputTypeShort &&
       m->screen != BleScreenInfo && !(m->screen == BleScreenChars && app->dumping)) {
        m->info_back = m->screen;
        m->screen = BleScreenInfo;
        return true;
    }
    switch(app->model.screen) {
    case BleScreenList:
        return ble_input_list(app, event);
    case BleScreenDetail:
        ble_input_detail(app, event);
        break;
    case BleScreenInfo:
        ble_input_info(app, event);
        break;
    case BleScreenServices:
        ble_input_services(app, event);
        break;
    case BleScreenChars:
        ble_input_chars(app, event);
        break;
    case BleScreenValue:
        ble_input_value(app, event);
        break;
    }
    // commands above changed gatt state, show it without waiting for the next tick
    if(app->model.screen >= BleScreenServices) ble_gatt_snapshot(app->gatt, &app->model.gatt);
    return true;
}
