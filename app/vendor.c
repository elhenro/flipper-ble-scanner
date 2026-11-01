#include "vendor.h"
#include "adv.h"

#include <stdio.h>

typedef struct {
    uint16_t id;
    const char* label;
} BleVendorEntry;

// proximity pairing models, ids from momentum ble_spam continuity table
static const BleVendorEntry apple_models[] = {
    {0x0220, "AirPods"},
    {0x0F20, "AirPods 2"},
    {0x1320, "AirPods 3"},
    {0x0E20, "AirPods Pro"},
    {0x1420, "AirPodsPro2"},
    {0x0A20, "AirPods Max"},
    {0x0055, "AirTag"},
    {0x0030, "AirTag"},
    {0x1020, "Beats Flex"},
    {0x0620, "Beats Solo3"},
    {0x0320, "Powerbeats3"},
    {0x0B20, "PowerbtsPro"},
    {0x0C20, "BeatsSoloPr"},
    {0x1120, "Beats Buds"},
    {0x0520, "Beats X"},
    {0x0920, "Beats Stu3"},
    {0x1720, "BeatsStuPro"},
    {0x1220, "BeatsFitPro"},
    {0x1620, "Beats Buds+"},
};

static const BleVendorEntry apple_types[] = {
    {APPLE_TYPE_PROXIMITY_PAIR, "AirPods/Bts"},
    {APPLE_TYPE_FIND_MY, "FindMy tag"},
    {APPLE_TYPE_AIRPLAY_TARGET, "AirPlay"},
    {APPLE_TYPE_IBEACON, "iBeacon"},
    {APPLE_TYPE_NEARBY_INFO, "Apple dev"},
    {APPLE_TYPE_NEARBY_ACTION, "Apple act"},
    {APPLE_TYPE_HANDOFF, "Handoff"},
    {APPLE_TYPE_AIRDROP, "AirDrop"},
    {APPLE_TYPE_HOTSPOT, "Hotspot"},
};

// bluetooth sig assigned company ids, common consumer vendors only
static const BleVendorEntry companies[] = {
    {0x004C, "Apple"},
    {0x0006, "Microsoft"},
    {0x0075, "Samsung"},
    {0x00E0, "Google"},
    {0x0087, "Garmin"},
    {0x009E, "Bose"},
    {0x012D, "Sony"},
    {0x0171, "Amazon"},
    {0x027D, "Huawei"},
    {0x038F, "Xiaomi"},
    {0x0157, "Huami"},
    {0x006B, "Polar"},
    {0x0078, "Nike"},
    {0x000A, "Qualcomm"},
    {0x0008, "Motorola"},
    {0x0002, "Intel"},
    {0x000F, "Broadcom"},
    {0x0001, "Nokia"},
    {0x0059, "Nordic"},
    {0x02E5, "Espressif"},
    {0x000D, "TI"},
};

// vendor specific service uuids that identify what an unnamed device is
static const BleVendorEntry services[] = {
    {0xFE9F, "Fast Pair"},
    {0xFEAA, "Eddystone"},
    {0xFD6F, "Trace tag"},
    {0xFD5A, "Garmin"},
    {0xFE95, "MiBeacon"},
    {0xFE59, "Nordic DFU"},
};

static const char* ble_vendor_find(const BleVendorEntry* table, size_t count, uint16_t id) {
    for(size_t i = 0; i < count; i++) {
        if(table[i].id == id) return table[i].label;
    }
    return NULL;
}

const char* ble_vendor_company_name(uint16_t id) {
    return ble_vendor_find(companies, COUNT_OF(companies), id);
}

const char* ble_vendor_label(const BleDevice* dev) {
    const char* label = NULL;
    if(dev->company == BLE_COMPANY_APPLE) {
        if(dev->apple_type == APPLE_TYPE_PROXIMITY_PAIR) {
            label = ble_vendor_find(apple_models, COUNT_OF(apple_models), dev->apple_model);
        }
        if(!label) label = ble_vendor_find(apple_types, COUNT_OF(apple_types), dev->apple_type);
    }
    if(!label && dev->company != BLE_SCAN_NO_COMPANY) {
        label = ble_vendor_find(companies, COUNT_OF(companies), dev->company);
    }
    // unnamed, unknown vendor: the advertised service can still say what it is
    for(uint8_t i = 0; !label && i < dev->uuid_count; i++) {
        label = ble_vendor_find(services, COUNT_OF(services), dev->uuids[i]);
    }
    if(!label && dev->sd_uuid) {
        label = ble_vendor_find(services, COUNT_OF(services), dev->sd_uuid);
    }
    return label;
}

void ble_vendor_display_name(char* buf, size_t size, const BleDevice* dev) {
    const char* label = ble_vendor_label(dev);
    if(dev->name[0]) {
        // adv names are untrusted bytes, font only has ascii
        size_t i = 0;
        for(; dev->name[i] && i < size - 1; i++) {
            char c = dev->name[i];
            buf[i] = (c >= 0x20 && c < 0x7F) ? c : '?';
        }
        buf[i] = '\0';
    } else if(label) {
        snprintf(buf, size, "%s", label);
    } else {
        snprintf(buf, size, "%02X:%02X:%02X", dev->addr[2], dev->addr[1], dev->addr[0]);
    }
}
