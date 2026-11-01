#include "app_i.h"
#include "scan_log.h"
#include "uuid.h"
#include "vendor.h"

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

void ble_scanner_select(BleScannerApp* app, uint8_t index) {
    BleViewModel* m = &app->model;
    m->selected = index;
    ble_view_scroll(index, &m->scroll);
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
    if(!m->count) {
        m->selected = m->scroll = 0;
        return;
    }
    if(!app->has_selection) ble_scanner_select(app, 0);

    // a picked device that went quiet stays picked, the cursor just clamps meanwhile
    uint8_t index = MIN(m->selected, m->count - 1);
    for(uint8_t i = 0; i < m->count; i++) {
        if(memcmp(m->devices[i].addr, app->selected_addr, 6) == 0) {
            index = i;
            if(m->screen == BleScreenDetail) m->peer = m->devices[i];
            break;
        }
    }
    m->selected = index;
    ble_view_scroll(index, &m->scroll);
}

static void ble_scanner_dump_finish(BleScannerApp* app, bool ok) {
    BleViewModel* m = &app->model;
    furi_string_cat_printf(
        app->dump_log, "%s: %u ok, %u err\n", ok ? "done" : "failed", app->dump_ok, app->dump_err);
    // the sd write happens outside the model lock, the main loop picks the text up
    app->dump_pending = app->dump_log;
    app->dump_log = NULL;
    app->dumping = false;
    app->dump_reading = false;
    m->dump_state = ok ? 2 : 3;
}

// reads the next readable characteristic, called with the model lock held
static void ble_scanner_dump_step(BleScannerApp* app) {
    BleViewModel* m = &app->model;
    BleGattInfo* gatt = &m->gatt;
    if(!app->dumping) return;
    if(gatt->state == BleGattStateFailed || gatt->state == BleGattStateIdle) {
        ble_scanner_dump_finish(app, false);
        return;
    }
    if(gatt->state != BleGattStateReady) return;

    if(app->dump_reading) {
        app->dump_reading = false;
        const BleGattChar* chr = &gatt->chars[app->dump_char];
        char uuid[24];
        ble_uuid_format(uuid, sizeof(uuid), chr->uuid, chr->uuid_len);
        // select cleared the counter, a fresh value means the read really answered
        if(gatt->value_updates && gatt->value_handle == chr->value_handle) {
            furi_string_cat_printf(app->dump_log, "%s =", uuid);
            for(uint8_t i = 0; i < gatt->value_len; i++) {
                furi_string_cat_printf(app->dump_log, "%02X", gatt->value[i]);
            }
            furi_string_push_back(app->dump_log, '\n');
            app->dump_ok++;
        } else {
            furi_string_cat_printf(app->dump_log, "%s = err 0x%02X\n", uuid, gatt->error);
            app->dump_err++;
        }
        m->dump_done = app->dump_ok + app->dump_err;
        app->dump_index = app->dump_char + 1;
    }

    while(app->dump_index < gatt->char_count &&
          !(gatt->chars[app->dump_index].props & BLE_GATT_PROP_READ)) {
        app->dump_index++;
    }
    if(app->dump_index >= gatt->char_count) {
        ble_scanner_dump_finish(app, true);
        return;
    }

    app->dump_char = app->dump_index;
    ble_gatt_select(app->gatt, app->dump_char);
    if(ble_gatt_read(app->gatt)) {
        app->dump_reading = true;
    } else {
        // command refused, no event will come back for this char
        char uuid[24];
        const BleGattChar* chr = &gatt->chars[app->dump_char];
        ble_uuid_format(uuid, sizeof(uuid), chr->uuid, chr->uuid_len);
        furi_string_cat_printf(app->dump_log, "%s = refused\n", uuid);
        app->dump_err++;
        app->dump_index = app->dump_char + 1;
        m->dump_done = app->dump_ok + app->dump_err;
    }
}

void ble_scanner_dump_start(BleScannerApp* app) {
    BleViewModel* m = &app->model;
    const BleGattInfo* gatt = &m->gatt;
    if(app->dumping || gatt->state != BleGattStateReady || !gatt->char_count) return;
    uint8_t total = 0;
    for(uint8_t i = 0; i < gatt->char_count; i++) {
        if(gatt->chars[i].props & BLE_GATT_PROP_READ) total++;
    }
    if(!total) return;

    char name[24];
    char uuid[24];
    const BleGattService* service = &gatt->services[m->service_sel];
    ble_uuid_format(uuid, sizeof(uuid), service->uuid, service->uuid_len);
    ble_vendor_display_name(name, sizeof(name), &m->peer);
    app->dump_log = furi_string_alloc();
    furi_string_cat_printf(app->dump_log, "%s %s\n", name, uuid);

    app->dumping = true;
    app->dump_reading = false;
    app->dump_char = 0;
    app->dump_index = 0;
    app->dump_ok = 0;
    app->dump_err = 0;
    app->dump_total = total;
    m->dump_state = 1;
    m->dump_done = 0;
    m->dump_total = total;
    ble_scanner_dump_step(app);
}

void ble_scanner_dump_cancel(BleScannerApp* app) {
    if(!app->dumping) return;
    furi_string_free(app->dump_log);
    app->dump_log = NULL;
    app->dumping = false;
    app->dump_reading = false;
    app->model.dump_state = 0;
}

// a 0x2A00 read names the peer for good: apple phones never advertise one
static void ble_scanner_apply_name(BleScannerApp* app) {
    BleViewModel* m = &app->model;
    if(!m->gatt.peer_name_set) return;
    snprintf(m->peer.gatt_name, sizeof(m->peer.gatt_name), "%s", m->gatt.peer_name);
    ble_scan_set_gatt_name(app->scan, m->peer.addr, m->gatt.peer_name);
    for(uint8_t i = 0; i < m->count; i++) {
        if(memcmp(m->devices[i].addr, m->peer.addr, 6) == 0) {
            snprintf(m->devices[i].gatt_name, sizeof(m->devices[i].gatt_name), "%s", m->gatt.peer_name);
            break;
        }
    }
}

static void ble_scanner_gatt_refresh(BleScannerApp* app) {
    BleViewModel* m = &app->model;
    ble_gatt_snapshot(app->gatt, &m->gatt);
    ble_scanner_apply_name(app);
    // the dump issues its own reads and selects its own chars
    if(app->dumping) {
        ble_scanner_dump_step(app);
        return;
    }
    const BleGattInfo* gatt = &m->gatt;
    if(gatt->state != BleGattStateReady) return;
    // discover once the link is up, then the selected char's cccd (0x2902) and name (0x2901)
    if(!gatt->services_done) {
        ble_gatt_discover_services(app->gatt);
    } else if(m->screen == BleScreenValue && !gatt->descs_done) {
        ble_gatt_discover_descs(app->gatt);
    } else if(m->screen == BleScreenValue && !gatt->desc_done) {
        ble_gatt_read_desc(app->gatt);
    }
}

static BleScannerApp* ble_scanner_alloc(void) {
    // zeroed by furi malloc
    BleScannerApp* app = malloc(sizeof(BleScannerApp));
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
    ble_scanner_dump_cancel(app);
    if(app->dump_pending) furi_string_free(app->dump_pending);
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
    // event dispatcher only exists with a running full stack. gatt registers its handler
    // before the scan's, see ble_scan_start
    if(m->supported) app->gatt = ble_gatt_alloc();
    ble_scanner_scan_resume(app);

    InputEvent event;
    FuriString* log = furi_string_alloc();
    uint32_t ticks = 0;
    bool running = true;
    while(running) {
        bool got_input = furi_message_queue_get(app->input_queue, &event, REFRESH_MS) ==
                         FuriStatusOk;
        bool log_due = false;
        furi_check(furi_mutex_acquire(app->model_mutex, FuriWaitForever) == FuriStatusOk);
        if(got_input) {
            running = m->supported ? ble_scanner_handle_input(app, &event) :
                                     event.key != InputKeyBack;
        } else {
            // every screen: also hangs up links that came up after leaving the gatt screens
            if(app->gatt) ble_gatt_tick(app->gatt);
            if(m->screen >= BleScreenServices) {
                ble_scanner_gatt_refresh(app);
            } else if(m->scanning) {
                ble_scanner_refresh(app);
            } else {
                // no valid slot while the link is still going down, retry until it takes
                ble_scanner_scan_resume(app);
            }
            // log failures too, status is readable over usb without the screen
            log_due = ++ticks % LOG_EVERY == 0 && ble_scan_log_enabled();
            if(log_due) ble_scan_log_format(log, m);
        }
        furi_mutex_release(app->model_mutex);
        // sd write is slow, drawing must not wait for it
        if(log_due) ble_scan_log_save(app->storage, log);
        if(app->dump_pending) {
            ble_scan_log_save_gatt(app->storage, app->dump_pending);
            furi_string_free(app->dump_pending);
            app->dump_pending = NULL;
        }
        view_port_update(app->view_port);
    }

    furi_string_free(log);
    ble_scanner_free(app);
    return 0;
}
