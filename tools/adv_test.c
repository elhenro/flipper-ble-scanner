// host check for advertisement parsing and vendor labels, no firmware needed:
// cc -I tools/screenshots/mock -I app tools/adv_test.c app/adv.c app/vendor.c
//    -o /tmp/adv_test && /tmp/adv_test
#include "adv.h"
#include "vendor.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint8_t apple_adv[] = {
    0x02, 0x01, 0x06, // flags
    0x06, 0x09, 'H', 'e', 'l', 'l', 'o', // full name
    0x08, 0xFF, 0x4C, 0x00, 0x07, 0x03, 0x01, 0x0E, 0x20, // apple proximity pair
    0x05, 0x16, 0x9F, 0xFE, 0x00, 0x01, // fast pair service data
    0x05, 0x03, 0x0F, 0x18, 0x0A, 0x18, // battery + device info
};

static void init(BleDevice* dev) {
    memset(dev, 0, sizeof(*dev));
    dev->company = BLE_SCAN_NO_COMPANY;
}

static void test_apple(void) {
    BleDevice dev;
    init(&dev);
    ble_adv_parse(&dev, apple_adv, sizeof(apple_adv));
    // a scan response repeats fields, nothing may double up
    ble_adv_parse(&dev, apple_adv, sizeof(apple_adv));

    assert(strcmp(dev.name, "Hello") == 0);
    assert(dev.company == BLE_COMPANY_APPLE);
    assert(dev.apple_type == APPLE_TYPE_PROXIMITY_PAIR);
    assert(dev.apple_model == 0x0E20);
    assert(dev.mf_len == 5 && dev.mf[0] == 0x07 && dev.mf[4] == 0x20);
    assert(dev.sd_uuid == 0xFE9F && dev.sd_len == 2 && dev.sd[0] == 0x00);
    assert(dev.uuid_count == 2 && dev.uuids[0] == 0x180F && dev.uuids[1] == 0x180A);
    assert(strcmp(ble_vendor_label(&dev), "AirPods Pro") == 0);

    char label[BLE_SCAN_NAME_LEN];
    ble_vendor_display_name(label, sizeof(label), &dev);
    assert(strcmp(label, "Hello") == 0);
}

static void test_company(void) {
    static const uint8_t adv[] = {0x06, 0xFF, 0x75, 0x00, 0x01, 0x02, 0x03};
    BleDevice dev;
    init(&dev);
    ble_adv_parse(&dev, adv, sizeof(adv));
    assert(dev.company == 0x0075 && dev.mf_len == 3);
    assert(strcmp(ble_vendor_label(&dev), "Samsung") == 0);

    char label[BLE_SCAN_NAME_LEN];
    ble_vendor_display_name(label, sizeof(label), &dev);
    assert(strcmp(label, "Samsung") == 0);
}

static void test_service_only(void) {
    // no name, no company: the advertised service still says what it is
    static const uint8_t adv[] = {0x03, 0x03, 0x9F, 0xFE};
    BleDevice dev;
    init(&dev);
    ble_adv_parse(&dev, adv, sizeof(adv));
    assert(dev.company == BLE_SCAN_NO_COMPANY);
    assert(strcmp(ble_vendor_label(&dev), "Fast Pair") == 0);

    static const uint8_t sd[] = {0x04, 0x16, 0xAA, 0xFE, 0x10};
    init(&dev);
    ble_adv_parse(&dev, sd, sizeof(sd));
    assert(strcmp(ble_vendor_label(&dev), "Eddystone") == 0);
}

static void test_truncated(void) {
    // cut the payload at every length, nothing may overflow or lose its terminator
    for(size_t len = 0; len <= sizeof(apple_adv); len++) {
        BleDevice dev;
        init(&dev);
        ble_adv_parse(&dev, apple_adv, len);
        assert(dev.name[BLE_SCAN_NAME_LEN - 1] == '\0');
        assert(dev.mf_len <= BLE_SCAN_MF_LEN);
        assert(dev.sd_len <= BLE_SCAN_SD_LEN);
        assert(dev.uuid_count <= BLE_SCAN_UUID_MAX);
    }
}

int main(void) {
    test_apple();
    test_company();
    test_service_only();
    test_truncated();
    printf("adv_test ok\n");
    return 0;
}
