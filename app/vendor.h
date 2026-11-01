#pragma once

#include "scan.h"

/** best human label from adv content (apple type/model, company, service uuid), NULL if unknown */
const char* ble_vendor_label(const BleDevice* dev);

/** bluetooth sig company name for an id, NULL if unknown */
const char* ble_vendor_company_name(uint16_t id);

/** device label for lists: name, else vendor label, else short address */
void ble_vendor_display_name(char* buf, size_t size, const BleDevice* dev);
