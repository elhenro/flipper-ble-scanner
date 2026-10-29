#include "scan_log.h"
#include "distance.h"
#include "vendor.h"
#include "uuid.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// vsnprintf is not exported to faps, furi_string_vprintf is
static void ble_scan_log_line(File* file, const char* fmt, ...) {
    FuriString* line = furi_string_alloc();
    va_list args;
    va_start(args, fmt);
    furi_string_vprintf(line, fmt, args);
    va_end(args);
    storage_file_write(file, furi_string_get_cstr(line), furi_string_size(line));
    furi_string_free(line);
}

// gatt session state, lets the browser be tested over usb without the screen
static void ble_scan_log_gatt(File* file, const BleViewModel* model) {
    const BleGattInfo* g = &model->gatt;
    char uuid[24];
    ble_scan_log_line(
        file,
        "gatt screen %d state %d err 0x%02X services %u chars %u\n",
        model->screen,
        g->state,
        g->error,
        g->service_count,
        g->char_count);
    for(uint8_t i = 0; i < g->service_count; i++) {
        ble_uuid_format(uuid, sizeof(uuid), g->services[i].uuid, g->services[i].uuid_len);
        ble_scan_log_line(
            file, " svc %04X-%04X %s\n", g->services[i].start, g->services[i].end, uuid);
    }
    for(uint8_t i = 0; i < g->char_count; i++) {
        ble_uuid_format(uuid, sizeof(uuid), g->chars[i].uuid, g->chars[i].uuid_len);
        ble_scan_log_line(
            file, " chr %04X p%02X %s\n", g->chars[i].value_handle, g->chars[i].props, uuid);
    }
    ble_scan_log_line(file, " val %04X #%lu:", g->value_handle, g->value_updates);
    for(uint8_t i = 0; i < g->value_len; i++) ble_scan_log_line(file, " %02X", g->value[i]);
    ble_scan_log_line(file, "\n");
}

bool ble_scan_log_write(Storage* storage, const BleViewModel* model) {
    File* file = storage_file_alloc(storage);
    bool ok = storage_file_open(file, BLE_SCAN_LOG_PATH, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    if(ok) {
        char line[128];
        int len = snprintf(
            line,
            sizeof(line),
            "tick %lu status 0x%02X reports %lu devices %u ref %d screen %d sel %u\n",
            furi_get_tick(),
            model->scan_status,
            model->reports,
            model->count,
            model->ref,
            model->screen,
            model->selected);
        storage_file_write(file, line, len);

        for(size_t i = 0; i < model->count; i++) {
            const BleDevice* dev = &model->devices[i];
            char addr[18];
            ble_view_format_addr(addr, dev->addr);
            char label[BLE_SCAN_NAME_LEN];
            ble_vendor_display_name(label, sizeof(label), dev);
            len = snprintf(
                line,
                sizeof(line),
                "%s %s%s%s rssi %d avg %.1f dist %.2fm co %04X at %02X am %04X n %lu [%s]\n",
                addr,
                dev->addr_type ? "rnd" : "pub",
                dev->connectable ? " C" : "",
                dev->scan_rsp ? " R" : "",
                dev->rssi,
                (double)dev->rssi_avg,
                (double)ble_distance_m(dev->rssi_avg, model->ref),
                dev->company,
                dev->apple_type,
                dev->apple_model,
                dev->seen_count,
                label);
            storage_file_write(file, line, MIN(len, (int)sizeof(line) - 1));
        }
        if(model->screen >= BleScreenServices) ble_scan_log_gatt(file, model);
    }
    storage_file_close(file);
    storage_file_free(file);
    return ok;
}
