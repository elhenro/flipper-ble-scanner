// renders every app screen with made up devices: no real addresses end up in the docs
#include "view.h"
#include "adv.h"

#include <stdio.h>

#define UUID16(id) {(id) & 0xFF, (id) >> 8}, 2

static const BleDevice devices[] = {
    {.addr = {0x42, 0x30, 0x00, 0x7E, 0xAA, 0xC0}, .rssi = -41, .rssi_avg = -41.6f,
     .seen_count = 87, .company = 0x02E5, .connectable = true, .name = "Toaster 3000"},
    {.addr = {0x1D, 0xE5, 0x77, 0x02, 0x9B, 0x6A}, .addr_type = 1, .rssi = -47,
     .rssi_avg = -48.2f, .seen_count = 63, .company = BLE_COMPANY_APPLE,
     .apple_type = APPLE_TYPE_PROXIMITY_PAIR, .apple_model = 0x0E20, .connectable = true,
     .mf_len = 12, .mf = {0x07, 0x19, 0x01, 0x0E, 0x20, 0x0C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
     .sd_len = 4, .sd_uuid = 0xFE9F, .sd = {0x00, 0x00, 0x01, 0x02},
     .uuid_count = 3, .uuids = {0xFE9F, 0xFEAA, 0x180F}},
    {.addr = {0x0F, 0xEE, 0xC0, 0x11, 0xD7, 0xA0}, .rssi = -55, .rssi_avg = -54.9f,
     .seen_count = 41, .company = 0x006B, .connectable = true, .name = "Polar H10"},
    {.addr = {0x99, 0x12, 0x5C, 0x33, 0x8A, 0xF4}, .addr_type = 1, .rssi = -63,
     .rssi_avg = -62.7f, .seen_count = 12, .company = BLE_COMPANY_APPLE,
     .apple_type = APPLE_TYPE_FIND_MY},
    {.addr = {0x7A, 0x41, 0x2C, 0x9E, 0x0B, 0x04}, .rssi = -66, .rssi_avg = -67.3f,
     .seen_count = 29, .company = 0x009E, .connectable = true, .name = "Bose QC45"},
    {.addr = {0x55, 0x01, 0xBE, 0xEF, 0x13, 0x5E}, .addr_type = 1, .rssi = -73,
     .rssi_avg = -72.1f, .seen_count = 33, .company = BLE_COMPANY_APPLE,
     .apple_type = APPLE_TYPE_NEARBY_INFO, .connectable = true},
    {.addr = {0x10, 0x20, 0x30, 0x40, 0x50, 0x60}, .rssi = -80, .rssi_avg = -79.4f,
     .seen_count = 7, .company = 0x0075, .name = "[TV] Samsung"},
};

static const BleGattService services[] = {
    {0x0001, 0x0007, UUID16(0x1800)},
    {0x0008, 0x000B, UUID16(0x1801)},
    {0x000C, 0x0018, UUID16(0x180A)},
    {0x0019, 0x001C, UUID16(0x180F)},
    // nordic uart 6E400001-B5A3-F393-E0A9-E50E24DCCA9E
    {0x001D, 0x0024, {0x9E, 0xCA, 0xDC, 0x24, 0x0E, 0xE5, 0xA9, 0xE0,
                      0x93, 0xF3, 0xA3, 0xB5, 0x01, 0x00, 0x40, 0x6E}, 16},
};

static const BleGattChar chars[] = {
    {0x000E, 0x02, UUID16(0x2A29)},
    {0x0010, 0x02, UUID16(0x2A24)},
    {0x0012, 0x02, UUID16(0x2A25)},
    {0x0014, 0x02, UUID16(0x2A26)},
    {0x0016, 0x12, UUID16(0x2A19)},
};

static void shot(Canvas* canvas, const BleViewModel* model, const char* dir, const char* name) {
    ble_view_draw(canvas, model);
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.bin", dir, name);
    FILE* f = fopen(path, "wb");
    fwrite(canvas_host_buffer(canvas), 1, 1024, f);
    fclose(f);
}

int main(int argc, char** argv) {
    if(argc < 2) return 1;
    Canvas* canvas = canvas_host_alloc();
    static BleViewModel m;
    m.supported = m.scanning = true;
    m.count = COUNT_OF(devices);
    memcpy(m.devices, devices, sizeof(devices));
    shot(canvas, &m, argv[1], "list");

    m.screen = BleScreenInfo;
    shot(canvas, &m, argv[1], "info");

    m.peer = devices[0];
    m.screen = BleScreenDetail;
    shot(canvas, &m, argv[1], "detail");

    m.peer = devices[1];
    m.detail_page = 1;
    shot(canvas, &m, argv[1], "vendor");
    m.detail_page = 0;

    m.gatt.state = BleGattStateReady;
    m.gatt.services_done = true;
    m.gatt.service_count = COUNT_OF(services);
    memcpy(m.gatt.services, services, sizeof(services));
    m.service_sel = 2;
    m.screen = BleScreenServices;
    shot(canvas, &m, argv[1], "services");

    m.gatt.char_count = COUNT_OF(chars);
    memcpy(m.gatt.chars, chars, sizeof(chars));
    m.char_sel = 1;
    m.screen = BleScreenChars;
    shot(canvas, &m, argv[1], "chars");

    const char model_number[] = "TOASTR-3000 crispy";
    m.gatt.value_handle = chars[1].value_handle;
    m.gatt.value_len = sizeof(model_number) - 1;
    memcpy(m.gatt.value, model_number, m.gatt.value_len);
    m.gatt.value_updates = 1;
    strcpy(m.gatt.desc, "Model Number");
    m.screen = BleScreenValue;
    shot(canvas, &m, argv[1], "value");
    return 0;
}
