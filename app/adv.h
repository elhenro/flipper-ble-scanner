#pragma once

#include "scan.h"

#define BLE_COMPANY_APPLE 0x004C

#define APPLE_TYPE_IBEACON        0x02
#define APPLE_TYPE_AIRDROP        0x05
#define APPLE_TYPE_PROXIMITY_PAIR 0x07
#define APPLE_TYPE_AIRPLAY_TARGET 0x09
#define APPLE_TYPE_HANDOFF        0x0C
#define APPLE_TYPE_HOTSPOT        0x0E
#define APPLE_TYPE_NEARBY_ACTION  0x0F
#define APPLE_TYPE_NEARBY_INFO    0x10
#define APPLE_TYPE_FIND_MY        0x12

/** merge AD structures (adv data or scan response) into dev */
void ble_adv_parse(BleDevice* dev, const uint8_t* data, uint8_t len);
