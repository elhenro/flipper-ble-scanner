#include "app_i.h"

#include <math.h>

#define TAG "BleScanner"

static bool ble_input_is_press(const InputEvent* event) {
    return event->type == InputTypeShort || event->type == InputTypeRepeat;
}

// cursor over a list shown BLE_VIEW_ROWS at a time
static void ble_input_move(uint8_t* sel, uint8_t* scroll, uint8_t count, InputKey key) {
    if(!count) return;
    if(key == InputKeyUp) *sel = *sel ? *sel - 1 : count - 1;
    if(key == InputKeyDown) *sel = (*sel + 1) % count;
    if(*sel < *scroll) *scroll = *sel;
    if(*sel >= *scroll + BLE_VIEW_ROWS) *scroll = *sel - BLE_VIEW_ROWS + 1;
}

static void ble_input_calibrate(BleViewModel* m) {
    // device held at 1m becomes the path loss reference
    m->ref = (int8_t)roundf(m->devices[m->selected].rssi_avg);
    FURI_LOG_I(TAG, "calibrated ref %d", m->ref);
}

static bool ble_input_list(BleScannerApp* app, const InputEvent* event) {
    BleViewModel* m = &app->model;
    if(event->key == InputKeyBack && event->type == InputTypeShort) return false;
    if(event->key == InputKeyLeft && event->type == InputTypeShort) {
        m->info_page = 0;
        m->screen = BleScreenInfo;
        return true;
    }
    if(!m->count) return true;
    if(event->key == InputKeyOk && event->type == InputTypeLong) {
        ble_input_calibrate(m);
    } else if(event->key == InputKeyOk && event->type == InputTypeShort) {
        m->screen = BleScreenDetail;
    } else if(ble_input_is_press(event)) {
        if(event->key == InputKeyUp) {
            ble_scanner_select(app, m->selected ? m->selected - 1 : m->count - 1);
        } else if(event->key == InputKeyDown) {
            ble_scanner_select(app, (m->selected + 1) % m->count);
        }
    }
    return true;
}

static void ble_input_info(BleScannerApp* app, const InputEvent* event) {
    BleViewModel* m = &app->model;
    if(event->type != InputTypeShort) return;
    if(event->key == InputKeyUp || event->key == InputKeyDown) {
        m->info_page = (m->info_page + 1) % BLE_INFO_PAGES;
    } else {
        m->screen = BleScreenList;
    }
}

static void ble_input_detail(BleScannerApp* app, const InputEvent* event) {
    BleViewModel* m = &app->model;
    if(event->key == InputKeyBack && event->type == InputTypeShort) {
        m->screen = BleScreenList;
    } else if(event->key == InputKeyOk && event->type == InputTypeLong) {
        ble_input_calibrate(m);
    } else if(event->key == InputKeyLeft && ble_input_is_press(event)) {
        m->ref--;
    } else if(event->key == InputKeyRight && ble_input_is_press(event)) {
        m->ref++;
    } else if(event->key == InputKeyOk && event->type == InputTypeShort) {
        if(!m->devices[m->selected].connectable) return;
        m->peer = m->devices[m->selected];
        // one gap procedure at a time: observation must end before connecting
        ble_scan_stop(app->scan);
        m->scanning = false;
        m->service_sel = m->service_scroll = 0;
        ble_gatt_connect(app->gatt, &m->peer);
        m->screen = BleScreenServices;
    }
}

static void ble_input_services(BleScannerApp* app, const InputEvent* event) {
    BleViewModel* m = &app->model;
    const BleGattInfo* gatt = &m->gatt;
    if(event->key == InputKeyBack && event->type == InputTypeShort) {
        ble_gatt_disconnect(app->gatt);
        ble_scanner_scan_resume(app);
        m->screen = BleScreenDetail;
    } else if(event->key == InputKeyOk && event->type == InputTypeShort) {
        if(gatt->state == BleGattStateFailed) {
            ble_gatt_connect(app->gatt, &m->peer);
        } else if(gatt->state == BleGattStateReady && gatt->service_count) {
            m->char_sel = m->char_scroll = 0;
            ble_gatt_discover_chars(app->gatt, m->service_sel);
            m->screen = BleScreenChars;
        }
    } else if(ble_input_is_press(event)) {
        ble_input_move(&m->service_sel, &m->service_scroll, gatt->service_count, event->key);
    }
}

static void ble_input_chars(BleScannerApp* app, const InputEvent* event) {
    BleViewModel* m = &app->model;
    const BleGattInfo* gatt = &m->gatt;
    if(event->key == InputKeyBack && event->type == InputTypeShort) {
        m->screen = BleScreenServices;
    } else if(event->key == InputKeyOk && event->type == InputTypeShort) {
        if(gatt->state != BleGattStateReady || !gatt->char_count) return;
        if(gatt->chars[m->char_sel].props & BLE_GATT_PROP_READ) {
            ble_gatt_read(app->gatt, m->char_sel);
        }
        m->screen = BleScreenValue;
    } else if(ble_input_is_press(event)) {
        ble_input_move(&m->char_sel, &m->char_scroll, gatt->char_count, event->key);
    }
}

static void ble_input_value(BleScannerApp* app, const InputEvent* event) {
    BleViewModel* m = &app->model;
    const BleGattInfo* gatt = &m->gatt;
    uint8_t props = gatt->chars[m->char_sel].props;
    bool ready = gatt->state == BleGattStateReady;
    if(event->key == InputKeyBack && event->type == InputTypeShort) {
        if(gatt->notifying && ready) ble_gatt_set_notify(app->gatt, m->char_sel, false);
        m->screen = BleScreenChars;
    } else if(event->type != InputTypeShort || !ready) {
        return;
    } else if(event->key == InputKeyOk && (props & BLE_GATT_PROP_READ)) {
        ble_gatt_read(app->gatt, m->char_sel);
    } else if(event->key == InputKeyRight && (props & BLE_GATT_PROP_NOTIFY)) {
        ble_gatt_set_notify(app->gatt, m->char_sel, !gatt->notifying);
    }
}

bool ble_scanner_handle_input(BleScannerApp* app, const InputEvent* event) {
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
