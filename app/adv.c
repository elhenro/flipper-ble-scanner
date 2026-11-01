#include "adv.h"

#include <string.h>

#define AD_TYPE_UUID16       0x02
#define AD_TYPE_UUID16_MORE  0x03
#define AD_TYPE_SHORT_NAME   0x08
#define AD_TYPE_FULL_NAME    0x09
#define AD_TYPE_SERVICE_DATA 0x16
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

static void ble_adv_add_uuid(BleDevice* dev, uint16_t uuid) {
    for(uint8_t i = 0; i < dev->uuid_count; i++) {
        if(dev->uuids[i] == uuid) return;
    }
    if(dev->uuid_count < BLE_SCAN_UUID_MAX) dev->uuids[dev->uuid_count++] = uuid;
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
            size_t n = MIN(value_len - 2, BLE_SCAN_MF_LEN);
            memcpy(dev->mf, value + 2, n);
            dev->mf_len = n;
            if(dev->company == BLE_COMPANY_APPLE) {
                ble_adv_parse_apple(dev, value + 2, value_len - 2);
            }
        } else if(type == AD_TYPE_SERVICE_DATA && value_len >= 2) {
            dev->sd_uuid = value[0] | (value[1] << 8);
            size_t n = MIN(value_len - 2, BLE_SCAN_SD_LEN);
            memcpy(dev->sd, value + 2, n);
            dev->sd_len = n;
        } else if(type == AD_TYPE_UUID16 || type == AD_TYPE_UUID16_MORE) {
            // adv and scan response repeat the same list, so dedupe
            for(uint8_t j = 0; j + 1 < value_len; j += 2) {
                ble_adv_add_uuid(dev, value[j] | (value[j + 1] << 8));
            }
        }
        i += field_len + 1;
    }
}
