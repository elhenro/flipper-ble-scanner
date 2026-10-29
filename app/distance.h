#pragma once

#include <math.h>
#include <stdint.h>

// log-distance path loss: d = 10^((ref - rssi) / (10 * n))
// ref = rssi at 1m, n = 2 free space, 2.5-4 indoor with walls/bodies
#define BLE_DISTANCE_N   2.5f
#define BLE_DISTANCE_REF -60

static inline float ble_distance_m(float rssi, int8_t ref) {
    return powf(10.0f, (ref - rssi) / (10.0f * BLE_DISTANCE_N));
}
