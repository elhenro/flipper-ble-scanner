#include "scan_log.h"
#include "distance.h"
#include "vendor.h"
#include "uuid.h"

#include <furi_hal_power.h>

// gatt session state, lets the browser be tested over usb without the screen
static void ble_scan_log_gatt(FuriString* out, const BleViewModel* model) {
    const BleGattInfo* g = &model->gatt;
    char uuid[24];
    furi_string_cat_printf(
        out,
        "gatt screen %d state %d err 0x%02X services %u chars %u cccd %04X\n",
        model->screen,
        g->state,
        g->error,
        g->service_count,
        g->char_count,
        g->cccd);
    for(uint8_t i = 0; i < g->service_count; i++) {
        ble_uuid_format(uuid, sizeof(uuid), g->services[i].uuid, g->services[i].uuid_len);
        furi_string_cat_printf(
            out, " svc %04X-%04X %s\n", g->services[i].start, g->services[i].end, uuid);
    }
    for(uint8_t i = 0; i < g->char_count; i++) {
        ble_uuid_format(uuid, sizeof(uuid), g->chars[i].uuid, g->chars[i].uuid_len);
        furi_string_cat_printf(
            out, " chr %04X p%02X %s\n", g->chars[i].value_handle, g->chars[i].props, uuid);
    }
    furi_string_cat_printf(out, " val %04X #%lu:", g->value_handle, g->value_updates);
    for(uint8_t i = 0; i < g->value_len; i++) furi_string_cat_printf(out, " %02X", g->value[i]);
    furi_string_push_back(out, '\n');
}

bool ble_scan_log_enabled(void) {
    // 0 unless the charger sees good vbus
    return furi_hal_power_get_usb_voltage() > 0.0f;
}

void ble_scan_log_format(FuriString* out, const BleViewModel* model) {
    furi_string_printf(
        out,
        "tick %lu status 0x%02X reports %lu devices %u ref %d screen %d sel %u\n",
        furi_get_tick(),
        model->scan_status,
        model->reports,
        model->count,
        model->ref,
        model->screen,
        model->selected);
    for(size_t i = 0; i < model->count; i++) {
        const BleDevice* dev = &model->devices[i];
        char addr[18];
        ble_view_format_addr(addr, dev->addr);
        char label[BLE_SCAN_NAME_LEN];
        ble_vendor_display_name(label, sizeof(label), dev);
        furi_string_cat_printf(
            out,
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
        furi_string_cat_printf(out, " mf");
        for(uint8_t j = 0; j < dev->mf_len; j++) {
            furi_string_cat_printf(out, " %02X", dev->mf[j]);
        }
        furi_string_cat_printf(out, " sd %04X", dev->sd_uuid);
        for(uint8_t j = 0; j < dev->sd_len; j++) {
            furi_string_cat_printf(out, " %02X", dev->sd[j]);
        }
        furi_string_cat_printf(out, " uu");
        for(uint8_t j = 0; j < dev->uuid_count; j++) {
            furi_string_cat_printf(out, " %04X", dev->uuids[j]);
        }
        furi_string_push_back(out, '\n');
    }
    if(model->screen >= BleScreenServices) ble_scan_log_gatt(out, model);
}

static bool ble_scan_log_write(Storage* storage, const char* path, const FuriString* text) {
    File* file = storage_file_alloc(storage);
    size_t size = furi_string_size(text);
    bool ok = storage_file_open(file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS) &&
              storage_file_write(file, furi_string_get_cstr(text), size) == size;
    storage_file_close(file);
    storage_file_free(file);
    return ok;
}

bool ble_scan_log_save(Storage* storage, const FuriString* text) {
    return ble_scan_log_write(storage, BLE_SCAN_LOG_PATH, text);
}

bool ble_scan_log_save_gatt(Storage* storage, const FuriString* text) {
    return ble_scan_log_write(storage, BLE_GATT_LOG_PATH, text);
}
