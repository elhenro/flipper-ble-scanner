#include "adv.h"

#include <string.h>

#define AD_TYPE_SHORT_NAME   0x08
#define AD_TYPE_FULL_NAME    0x09
#define AD_TYPE_MANUFACTURER 0xFF

// most telling continuity type wins when a packet carries several
static uint8_t ble_adv_apple_rank(uint8_t type) {
    switch(type) {
    case APPLE_TYPE_PROXIMITY_PAIR:
        return 6;
    case APPLE_TYPE_FIND_MY:
        return 5;
    case APPLE_TYPE_AIRPLAY_TARGET:
        return 4;
    case APPLE_TYPE_IBEACON:
        return 3;
    case APPLE_TYPE_NEARBY_INFO:
        return 2;
    case 0:
        return 0;
    default:
        return 1;
    }
}

// continuity payload after company id: [type][len][data...] repeated
static void ble_adv_parse_apple(BleDevice* dev, const uint8_t* data, uint8_t len) {
    uint8_t i = 0;
    while(i + 2 <= len) {
        uint8_t type = data[i];
        uint8_t field_len = data[i + 1];
        if(i + 2 + field_len > len) break;
        if(ble_adv_apple_rank(type) > ble_adv_apple_rank(dev->apple_type)) {
            dev->apple_type = type;
        }
        // proximity pairing: [prefix][model hi][model lo]...
        if(type == APPLE_TYPE_PROXIMITY_PAIR && field_len >= 3) {
            dev->apple_model = (data[i + 3] << 8) | data[i + 4];
        }
        i += 2 + field_len;
    }
}

void ble_adv_parse(BleDevice* dev, const uint8_t* data, uint8_t len) {
    uint8_t i = 0;
    while(i + 1 < len) {
        uint8_t field_len = data[i];
        if(field_len == 0 || i + 1 + field_len > len) break;
        uint8_t type = data[i + 1];
        const uint8_t* value = &data[i + 2];
        uint8_t value_len = field_len - 1;

        if(type == AD_TYPE_FULL_NAME || (type == AD_TYPE_SHORT_NAME && !dev->name[0])) {
            size_t n = MIN(value_len, BLE_SCAN_NAME_LEN - 1);
            memcpy(dev->name, value, n);
            dev->name[n] = '\0';
        } else if(type == AD_TYPE_MANUFACTURER && value_len >= 2) {
            dev->company = value[0] | (value[1] << 8);
            if(dev->company == BLE_COMPANY_APPLE) {
                ble_adv_parse_apple(dev, value + 2, value_len - 2);
            }
        }
        i += field_len + 1;
    }
}
