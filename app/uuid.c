#include "uuid.h"

#include <core/common_defines.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint16_t id;
    const char* name;
} BleUuidName;

// bluetooth sig assigned numbers, common services and characteristics
static const BleUuidName names[] = {
    {0x1800, "Generic Access"},
    {0x1801, "Generic Attribute"},
    {0x1805, "Current Time"},
    {0x180A, "Device Info"},
    {0x180D, "Heart Rate"},
    {0x180F, "Battery"},
    {0x1812, "HID"},
    {0x1816, "Cycling Speed"},
    {0x1818, "Cycling Power"},
    {0x181A, "Environment"},
    {0x1826, "Fitness Machine"},
    {0x2A00, "Device Name"},
    {0x2A01, "Appearance"},
    {0x2A04, "Conn Params"},
    {0x2A05, "Service Changed"},
    {0x2A19, "Battery Level"},
    {0x2A23, "System ID"},
    {0x2A24, "Model Number"},
    {0x2A25, "Serial Number"},
    {0x2A26, "Firmware Rev"},
    {0x2A27, "Hardware Rev"},
    {0x2A28, "Software Rev"},
    {0x2A29, "Manufacturer"},
    {0x2A2B, "Current Time"},
    {0x2A37, "Heart Rate"},
    {0x2A38, "Sensor Location"},
    {0x2A50, "PnP ID"},
    {0x2A6E, "Temperature"},
    {0x2A6F, "Humidity"},
    {0x2AA6, "Addr Resolution"},
    {0x2B29, "Client Features"},
    {0x2B2A, "Database Hash"},
};

// 0000xxxx-0000-1000-8000-00805f9b34fb, little-endian on air
static const uint8_t sig_base[12] =
    {0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00};

void ble_uuid_format(char* buf, size_t size, const uint8_t* uuid, uint8_t uuid_len) {
    // 16-bit id, plain or inside the sig base
    bool sig = uuid_len == 16 && memcmp(uuid, sig_base, sizeof(sig_base)) == 0;
    const uint8_t* id = uuid_len == 2 ? uuid : sig ? &uuid[12] : NULL;

    if(id) {
        uint16_t short_id = id[0] | (id[1] << 8);
        for(size_t i = 0; i < COUNT_OF(names); i++) {
            if(names[i].id == short_id) {
                snprintf(buf, size, "%s", names[i].name);
                return;
            }
        }
        snprintf(buf, size, "0x%04X", short_id);
    } else if(uuid_len == 16) {
        // most significant bytes first, enough to recognise vendor services
        snprintf(buf, size, "%02X%02X%02X%02X..", uuid[15], uuid[14], uuid[13], uuid[12]);
    } else {
        snprintf(buf, size, "?");
    }
}
