#include "app_i.h"
#include "scan_log.h"
#include "distance.h"

#include <furi.h>
#include <string.h>

#define TAG "BleScanner"

#define REFRESH_MS     250
#define LOG_EVERY      8 // refreshes, ~2s
#define DEVICE_MAX_AGE 15000

static void ble_scanner_draw_callback(Canvas* canvas, void* context) {
    BleScannerApp* app = context;
    furi_check(furi_mutex_acquire(app->model_mutex, FuriWaitForever) == FuriStatusOk);
    ble_view_draw(canvas, &app->model);
    furi_mutex_release(app->model_mutex);
}

static void ble_scanner_input_callback(InputEvent* event, void* context) {
    BleScannerApp* app = context;
    furi_message_queue_put(app->input_queue, event, FuriWaitForever);
}

void ble_scanner_select(BleScannerApp* app, size_t index) {
    BleViewModel* m = &app->model;
    if(m->count == 0) return;
    m->selected = index;
    if(m->selected < m->scroll) m->scroll = m->selected;
    if(m->selected >= m->scroll + BLE_VIEW_ROWS) m->scroll = m->selected - BLE_VIEW_ROWS + 1;
    memcpy(app->selected_addr, m->devices[index].addr, 6);
    app->has_selection = true;
}

void ble_scanner_scan_resume(BleScannerApp* app) {
    app->model.scan_status = ble_scan_start(app->scan);
    app->model.scanning = app->model.scan_status == 0;
    FURI_LOG_I(TAG, "scan start status 0x%02X", app->model.scan_status);
}

static void ble_scanner_refresh(BleScannerApp* app) {
    BleViewModel* m = &app->model;
    m->count = ble_scan_snapshot(app->scan, m->devices, BLE_SCAN_MAX_DEVICES, DEVICE_MAX_AGE);
    m->reports = ble_scan_report_count(app->scan);

    size_t index = MIN(m->selected, m->count ? m->count - 1 : 0);
    for(size_t i = 0; app->has_selection && i < m->count; i++) {
        if(memcmp(m->devices[i].addr, app->selected_addr, 6) == 0) {
            index = i;
            break;
        }
    }
    if(m->count) {
        ble_scanner_select(app, index);
    } else {
        m->selected = m->scroll = 0;
        m->screen = BleScreenList;
    }
}

static void ble_scanner_gatt_tick(BleScannerApp* app) {
    BleViewModel* m = &app->model;
    ble_gatt_tick(app->gatt);
    ble_gatt_snapshot(app->gatt, &m->gatt);
    // discover once the link is up
    if(m->gatt.state == BleGattStateReady && !m->gatt.services_done) {
        ble_gatt_discover_services(app->gatt);
    }
}

static BleScannerApp* ble_scanner_alloc(void) {
    BleScannerApp* app = malloc(sizeof(BleScannerApp));
    memset(app, 0, sizeof(BleScannerApp));
    app->model.ref = BLE_DISTANCE_REF;
    app->model_mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    app->input_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    app->scan = ble_scan_alloc();
    app->storage = furi_record_open(RECORD_STORAGE);

    app->view_port = view_port_alloc();
    view_port_draw_callback_set(app->view_port, ble_scanner_draw_callback, app);
    view_port_input_callback_set(app->view_port, ble_scanner_input_callback, app);
    app->gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(app->gui, app->view_port, GuiLayerFullscreen);
    return app;
}

static void ble_scanner_free(BleScannerApp* app) {
    // gatt first: hangs up the link before its event handler goes away
    if(app->gatt) ble_gatt_free(app->gatt);
    ble_scan_free(app->scan);
    gui_remove_view_port(app->gui, app->view_port);
    view_port_free(app->view_port);
    furi_record_close(RECORD_GUI);
    furi_record_close(RECORD_STORAGE);
    furi_message_queue_free(app->input_queue);
    furi_mutex_free(app->model_mutex);
    free(app);
}

int32_t ble_scanner_main(void* p) {
    UNUSED(p);
    BleScannerApp* app = ble_scanner_alloc();
    BleViewModel* m = &app->model;

    m->supported = ble_scan_is_supported();
    // event dispatcher only exists with a running full stack
    if(m->supported) app->gatt = ble_gatt_alloc();
    ble_scanner_scan_resume(app);

    InputEvent event;
    uint32_t ticks = 0;
    bool running = true;
    while(running) {
        bool got_input = furi_message_queue_get(app->input_queue, &event, REFRESH_MS) ==
                         FuriStatusOk;
        furi_check(furi_mutex_acquire(app->model_mutex, FuriWaitForever) == FuriStatusOk);
        if(got_input) {
            running = m->supported ? ble_scanner_handle_input(app, &event) :
                                     event.key != InputKeyBack;
        } else {
            if(m->screen >= BleScreenServices) {
                ble_scanner_gatt_tick(app);
            } else if(m->scanning) {
                ble_scanner_refresh(app);
            }
            // log failures too, status is readable over usb without the screen
            if(++ticks % LOG_EVERY == 0) ble_scan_log_write(app->storage, m);
        }
        furi_mutex_release(app->model_mutex);
        view_port_update(app->view_port);
    }

    ble_scanner_free(app);
    return 0;
}
