// host test for the event side: read blob, write completion, pairing.
// compiles the real gatt_event.c against stub locks, no radio involved.
#include "gatt_i.h"

#include <stdio.h>
#include <string.h>

#define CHECK(cond)                                              \
    do {                                                         \
        checks++;                                                \
        if(!(cond)) {                                            \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); \
            fails++;                                             \
        }                                                        \
    } while(0)

static int locks, unlocks;

FuriStatus furi_mutex_acquire(FuriMutex* mutex, uint32_t timeout) {
    UNUSED(mutex);
    UNUSED(timeout);
    locks++;
    return FuriStatusOk;
}
void furi_mutex_release(FuriMutex* mutex) {
    UNUSED(mutex);
    unlocks++;
}

static BleGatt gatt;

static void reset(void) {
    memset(&gatt, 0, sizeof(gatt));
    gatt.conn_handle = 0x0040;
    gatt.info.state = BleGattStateReady;
    gatt.info.value_handle = 0x0016;
    locks = unlocks = 0;
}

// vendor event: hci packet type, event code, param len, then ecode, conn handle, payload
static BleEventAckStatus vendor_on(
    uint16_t conn,
    uint16_t ecode,
    const uint8_t* payload,
    uint8_t payload_len) {
    uint8_t pckt[64] = {0x04, 0xFF, 0, 0, 0, conn & 0xFF, conn >> 8};
    pckt[2] = (uint8_t)(4 + payload_len);
    pckt[3] = ecode & 0xFF;
    pckt[4] = ecode >> 8;
    if(payload_len) memcpy(&pckt[7], payload, payload_len);
    return ble_gatt_event_handler(pckt, &gatt);
}

static BleEventAckStatus vendor(uint16_t ecode, const uint8_t* payload, uint8_t payload_len) {
    return vendor_on(0x0040, ecode, payload, payload_len);
}

int main(void) {
    int checks = 0, fails = 0;
    uint8_t v[32];

    // a read that fills the mtu marks the value as truncated
    reset();
    memset(v, 'A', sizeof(v));
    vendor(0x0C07, (const uint8_t[]){20, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
                                     'N', 'O', 'P', 'Q', 'R', 'S', 'T'},
           21);
    CHECK(gatt.info.value_len == 20);
    CHECK(gatt.info.truncated);
    CHECK(!gatt.info.long_done);
    CHECK(locks == 1 && unlocks == 1); // one balanced lock per event

    // a short read is a complete value
    reset();
    vendor(0x0C07, (const uint8_t[]){3, 'x', 'y', 'z'}, 4);
    CHECK(gatt.info.value_len == 3 && !gatt.info.truncated);

    // read blob appends where the plain read stopped
    reset();
    gatt.reading_long = true;
    gatt.info.value_len = 20;
    memset(gatt.info.value, 'a', 20);
    vendor(0x0C08, (const uint8_t[]){4, 'b', 'b', 'b', 'b'}, 5);
    CHECK(gatt.info.value_len == 24);
    CHECK(gatt.info.value[19] == 'a' && gatt.info.value[20] == 'b');

    // ...and stops at the value buffer instead of overrunning it
    reset();
    gatt.reading_long = true;
    gatt.info.value_len = BLE_GATT_VALUE_LEN - 2;
    vendor(0x0C08, (const uint8_t[]){8, 1, 2, 3, 4, 5, 6, 7, 8}, 9);
    CHECK(gatt.info.value_len == BLE_GATT_VALUE_LEN);

    // procedure complete ends the read long
    reset();
    gatt.reading_long = true;
    gatt.info.truncated = true;
    gatt.info.state = BleGattStateBusy;
    vendor(0x0C10, (const uint8_t[]){0x00}, 1);
    CHECK(!gatt.reading_long);
    CHECK(gatt.info.long_done && !gatt.info.truncated);
    CHECK(gatt.info.state == BleGattStateReady);

    // invalid offset while reading long is the end of the value, not an error
    reset();
    gatt.reading_long = true;
    gatt.info.state = BleGattStateBusy;
    vendor(0x0C11, (const uint8_t[]){0x1A, 0x18, 0x00, 0x07}, 4);
    CHECK(!gatt.reading_long && gatt.info.long_done);
    CHECK(gatt.info.error == 0);

    // a real error during read long still shows
    reset();
    gatt.reading_long = true;
    vendor(0x0C11, (const uint8_t[]){0x1A, 0x18, 0x00, 0x05}, 4);
    CHECK(gatt.info.error == 0x05 && gatt.info.long_done);

    // a write with response reports back on procedure complete
    reset();
    gatt.info.write_pending = true;
    gatt.info.state = BleGattStateBusy;
    vendor(0x0C10, (const uint8_t[]){0x00}, 1);
    CHECK(!gatt.info.write_pending && gatt.info.write_done);
    CHECK(gatt.info.state == BleGattStateReady);

    // a refused write reports back on error response
    reset();
    gatt.info.write_pending = true;
    vendor(0x0C11, (const uint8_t[]){0x1B, 0x18, 0x00, 0x03}, 4);
    CHECK(!gatt.info.write_pending && gatt.info.write_done);
    CHECK(gatt.info.error == 0x03);

    // numeric comparison: six digits, little endian, prompt waits for the user
    reset();
    vendor(0x0409, (const uint8_t[]){0x7E, 0x5A, 0x07, 0x00}, 4);
    CHECK(gatt.info.pair_confirm);
    CHECK(gatt.info.pair_numeric == 481918);
    CHECK(gatt.info.pair_state == 1);

    // answering clears the prompt and the result lands on pairing complete
    reset();
    gatt.info.pair_confirm = true;
    vendor(0x0401, (const uint8_t[]){0x00, 0x00}, 2);
    CHECK(!gatt.info.pair_confirm);
    CHECK(gatt.info.pair_state == 2 && gatt.info.pair_reason == 0);

    // a successful pairing clears the error that started it and asks for the read again
    reset();
    gatt.info.error = 0x05;
    vendor(0x0401, (const uint8_t[]){0x00, 0x00}, 2);
    CHECK(gatt.info.error == 0 && gatt.info.resume_read);

    // a refused pairing keeps the reason code
    reset();
    vendor(0x0401, (const uint8_t[]){0x05, 0x04}, 2);
    CHECK(gatt.info.pair_state == 3 && gatt.info.pair_reason == 0x04);

    // a peer that wants a keypad is answered with a wrong key from the tick
    reset();
    vendor(0x0402, NULL, 0);
    CHECK(gatt.passkey_pending);
    CHECK(gatt.info.pair_state == 3 && gatt.info.pair_reason == BLE_GATT_ERR_PASSKEY);

    // authorization and bond-lost are deferred to the app thread too
    reset();
    vendor(0x0403, NULL, 0);
    CHECK(gatt.authorize_pending);
    reset();
    vendor(0x0405, NULL, 0);
    CHECK(gatt.rebond_pending);

    // events of another link (the phone's) are left alone
    reset();
    vendor_on(0x0099, 0x0409, (const uint8_t[]){1, 2, 3, 4}, 4);
    CHECK(!gatt.info.pair_confirm);

    // disconnect ends the link and keeps the reason
    reset();
    gatt.info.notifying = true;
    {
        uint8_t pckt[] = {0x04, 0x05, 4, 0x00, 0x40, 0x00, 0x13};
        CHECK(ble_gatt_event_handler(pckt, &gatt) == BleEventAckFlowEnable);
    }
    CHECK(gatt.conn_handle == BLE_GATT_NO_HANDLE);
    CHECK(gatt.info.state == BleGattStateFailed);
    CHECK(gatt.info.error == 0x13);
    CHECK(!gatt.info.notifying);

    // our own hang-up is not an error
    reset();
    gatt.info.state = BleGattStateIdle;
    {
        uint8_t pckt[] = {0x04, 0x05, 4, 0x00, 0x40, 0x00, 0x16};
        ble_gatt_event_handler(pckt, &gatt);
    }
    CHECK(gatt.info.state == BleGattStateIdle && gatt.info.error == 0);

    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
