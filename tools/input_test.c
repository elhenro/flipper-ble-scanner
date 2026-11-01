// host test for the key handling: value editor, pairing prompt, read blob.
// links app/input.c against recording stubs, so the real handler runs.
#include "app_i.h"

#include <stdio.h>
#include <string.h>

#define CHECK(cond)                                             \
    do {                                                        \
        checks++;                                               \
        if(!(cond)) {                                           \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); \
            fails++;                                            \
        }                                                       \
    } while(0)

// ---- stubs: what input.c is allowed to call ----

static struct {
    int read, read_long, write, notify, notify_off, pair, pair_confirm;
    uint8_t write_buf[BLE_GATT_VALUE_LEN];
    uint8_t write_len;
    bool confirm_arg;
    bool notify_arg;
} calls;

bool ble_gatt_read(BleGatt* gatt) {
    UNUSED(gatt);
    calls.read++;
    return true;
}
bool ble_gatt_read_long(BleGatt* gatt) {
    UNUSED(gatt);
    calls.read_long++;
    return true;
}
bool ble_gatt_write(BleGatt* gatt, const uint8_t* data, uint8_t len) {
    UNUSED(gatt);
    calls.write++;
    calls.write_len = len;
    memcpy(calls.write_buf, data, len);
    return true;
}
bool ble_gatt_set_notify(BleGatt* gatt, bool on) {
    UNUSED(gatt);
    if(on) calls.notify++;
    else calls.notify_off++;
    calls.notify_arg = on;
    return true;
}
bool ble_gatt_pair(BleGatt* gatt) {
    UNUSED(gatt);
    calls.pair++;
    return true;
}
bool ble_gatt_pair_confirm(BleGatt* gatt, bool yes) {
    UNUSED(gatt);
    calls.pair_confirm++;
    calls.confirm_arg = yes;
    return true;
}
// the list and services screens are not what this test covers
bool ble_gatt_connect(BleGatt* gatt, const BleDevice* dev) {
    UNUSED(gatt);
    UNUSED(dev);
    return true;
}
void ble_gatt_disconnect(BleGatt* gatt) {
    UNUSED(gatt);
}
bool ble_gatt_discover_services(BleGatt* gatt) {
    UNUSED(gatt);
    return true;
}
bool ble_gatt_discover_chars(BleGatt* gatt, uint8_t service) {
    UNUSED(gatt);
    UNUSED(service);
    return true;
}
void ble_gatt_select(BleGatt* gatt, uint8_t index) {
    UNUSED(gatt);
    UNUSED(index);
}
void ble_gatt_snapshot(BleGatt* gatt, BleGattInfo* info) {
    UNUSED(gatt);
    UNUSED(info);
}
void ble_scan_stop(BleScan* scan) {
    UNUSED(scan);
}
void ble_scanner_dump_cancel(BleScannerApp* app) {
    UNUSED(app);
}
void ble_scanner_dump_start(BleScannerApp* app) {
    UNUSED(app);
}
void ble_scanner_scan_resume(BleScannerApp* app) {
    UNUSED(app);
}
void ble_scanner_select(BleScannerApp* app, uint8_t index) {
    UNUSED(app);
    UNUSED(index);
}

// ---- harness ----

static BleScannerApp app;
// gatt is opaque to the app side, the stubs never look inside it
static int gatt_storage;

static void ev(InputKey key, InputType type) {
    InputEvent e = {.key = key, .type = type};
    ble_scanner_handle_input(&app, &e);
}

static void reset(void) {
    memset(&calls, 0, sizeof(calls));
    memset(&app, 0, sizeof(app));
    app.gatt = (BleGatt*)&gatt_storage;
    app.model.screen = BleScreenValue;
    app.model.gatt.state = BleGattStateReady;
    app.model.gatt.char_count = 1;
    app.model.gatt.chars[0].value_handle = 0x0016;
    app.model.gatt.chars[0].props = BLE_GATT_PROP_READ | BLE_GATT_PROP_WRITE;
}

int main(void) {
    int checks = 0, fails = 0;

    // editor opens on hold OK of a writable char, seeded with the shown value
    reset();
    app.model.gatt.value_len = 4;
    memcpy(app.model.gatt.value, "\x01\xAB\x10\xFF", 4);
    ev(InputKeyOk, InputTypeLong);
    CHECK(app.model.editing);
    CHECK(app.model.edit_len == 4);
    CHECK(app.model.edit_pos == 0);
    CHECK(memcmp(app.model.edit_buf, "\x01\xAB\x10\xFF", 4) == 0);

    // an empty value still gets one byte to edit
    reset();
    ev(InputKeyOk, InputTypeLong);
    CHECK(app.model.editing && app.model.edit_len == 1 && app.model.edit_buf[0] == 0);

    // nibble stepping: up on the high nibble, down on the low one
    reset();
    app.model.editing = true;
    app.model.edit_len = 2;
    app.model.edit_buf[0] = 0x9F;
    ev(InputKeyUp, InputTypeShort); // pos 0, high nibble 9 -> A
    CHECK(app.model.edit_buf[0] == 0xAF);
    ev(InputKeyRight, InputTypeShort); // pos 1, low nibble F -> 0
    ev(InputKeyUp, InputTypeShort);
    CHECK(app.model.edit_buf[0] == 0xA0);
    ev(InputKeyDown, InputTypeShort); // low nibble 0 -> F, high untouched
    CHECK(app.model.edit_buf[0] == 0xAF);
    ev(InputKeyDown, InputTypeShort); // and wraps to E, never borrows
    CHECK(app.model.edit_buf[0] == 0xAE);

    // cursor wraps over both nibbles of both bytes
    reset();
    app.model.editing = true;
    app.model.edit_len = 2;
    ev(InputKeyLeft, InputTypeShort);
    CHECK(app.model.edit_pos == 3);
    CHECK(app.model.screen == BleScreenValue); // not the info screen
    ev(InputKeyRight, InputTypeShort);
    CHECK(app.model.edit_pos == 0);

    // OK sends the buffer and leaves the editor, a second OK must not re-send
    reset();
    app.model.editing = true;
    app.model.edit_len = 2;
    app.model.edit_buf[0] = 0x41;
    app.model.edit_buf[1] = 0x42;
    ev(InputKeyOk, InputTypeShort);
    CHECK(calls.write == 1 && calls.write_len == 2);
    CHECK(calls.write_buf[0] == 0x41 && calls.write_buf[1] == 0x42);
    CHECK(!app.model.editing);
    ev(InputKeyOk, InputTypeShort); // now a plain read, not a write
    CHECK(calls.write == 1 && calls.read == 1);

    // Back abandons the edit without writing, stays on the value screen
    reset();
    app.model.editing = true;
    app.model.edit_len = 1;
    ev(InputKeyBack, InputTypeShort);
    CHECK(!app.model.editing && calls.write == 0);
    CHECK(app.model.screen == BleScreenValue);

    // hold OK on a read-only char does nothing
    reset();
    app.model.gatt.chars[0].props = BLE_GATT_PROP_READ;
    ev(InputKeyOk, InputTypeLong);
    CHECK(!app.model.editing);

    // a link that dropped takes the editor with it, no write goes out
    reset();
    app.model.editing = true;
    app.model.edit_len = 1;
    app.model.gatt.state = BleGattStateIdle;
    ev(InputKeyOk, InputTypeShort);
    CHECK(!app.model.editing && calls.write == 0);

    // truncated value: OK asks for the rest instead of re-reading the head
    reset();
    app.model.gatt.truncated = true;
    ev(InputKeyOk, InputTypeShort);
    CHECK(calls.read_long == 1 && calls.read == 0);

    // pairing prompt answers yes on OK and no on Back, other keys are ignored
    reset();
    app.model.gatt.pair_confirm = true;
    ev(InputKeyUp, InputTypeShort);
    CHECK(calls.pair_confirm == 0);
    ev(InputKeyOk, InputTypeShort);
    CHECK(calls.pair_confirm == 1 && calls.confirm_arg);
    ev(InputKeyBack, InputTypeShort);
    CHECK(calls.pair_confirm == 2 && !calls.confirm_arg);

    // Back on a notifying char turns notifications off before leaving
    reset();
    app.model.gatt.notifying = true;
    ev(InputKeyBack, InputTypeShort);
    CHECK(calls.notify_off == 1 && !calls.notify_arg);
    CHECK(app.model.screen == BleScreenChars);

    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
