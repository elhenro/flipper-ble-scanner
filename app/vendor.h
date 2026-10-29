#pragma once

#include "scan.h"

/** best human label from adv content (apple type/model, company), NULL if unknown */
const char* ble_vendor_label(const BleDevice* dev);

/** device label for lists: name, else vendor label, else short address */
void ble_vendor_display_name(char* buf, size_t size, const BleDevice* dev);
