#pragma once

#include <stddef.h>
#include <stdint.h>

/** known name for 16-bit sig uuid or 128-bit sig base uuid, else hex ("0x2A19", "6E400001..") */
void ble_uuid_format(char* buf, size_t size, const uint8_t* uuid, uint8_t uuid_len);
