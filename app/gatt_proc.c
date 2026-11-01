// gatt client procedures on the connected link, results arrive in gatt_event.c
#include "gatt_i.h"

#define OP_DISC_ALL_SERVICES 0xFD12
#define OP_DISC_ALL_CHARS    0xFD15
#define OP_DISC_ALL_DESCS    0xFD17
#define OP_READ_CHAR_VALUE   0xFD18
#define OP_WRITE_CHAR_DESC   0xFD21
#define OP_READ_CHAR_DESC    0xFD22

bool ble_gatt_discover_services(BleGatt* gatt) {
    uint8_t p[2];
    ble_gatt_lock(gatt);
    gatt->info.service_count = 0;
    gatt->info.services_done = true;
    ble_gatt_put16(p, gatt->conn_handle);
    return ble_gatt_command(gatt, BleGattStateBusy, OP_DISC_ALL_SERVICES, p, 2);
}

bool ble_gatt_discover_chars(BleGatt* gatt, uint8_t service_index) {
    uint8_t p[6];
    ble_gatt_lock(gatt);
    const BleGattService* s = &gatt->info.services[service_index];
    ble_gatt_put16(&p[0], gatt->conn_handle);
    ble_gatt_put16(&p[2], s->start);
    ble_gatt_put16(&p[4], s->end);
    gatt->chars_end = s->end;
    gatt->info.char_count = 0;
    return ble_gatt_command(gatt, BleGattStateBusy, OP_DISC_ALL_CHARS, p, 6);
}

void ble_gatt_select(BleGatt* gatt, uint8_t char_index) {
    ble_gatt_lock(gatt);
    BleGattInfo* info = &gatt->info;
    uint16_t value_handle = info->chars[char_index].value_handle;
    gatt->value_char = char_index;
    // value, counter and cccd belong to one characteristic
    if(info->value_handle != value_handle) {
        info->value_handle = value_handle;
        info->value_len = 0;
        info->value_updates = 0;
        info->cccd = 0;
        info->descs_done = false;
        info->notifying = false;
        info->desc[0] = '\0';
        info->desc_done = false;
        gatt->desc_handle = 0;
        gatt->reading_desc = false;
    }
    ble_gatt_unlock(gatt);
}

bool ble_gatt_read(BleGatt* gatt) {
    uint8_t p[4];
    ble_gatt_lock(gatt);
    ble_gatt_put16(&p[0], gatt->conn_handle);
    ble_gatt_put16(&p[2], gatt->info.value_handle);
    return ble_gatt_command(gatt, BleGattStateBusy, OP_READ_CHAR_VALUE, p, 4);
}

bool ble_gatt_discover_descs(BleGatt* gatt) {
    uint8_t p[6];
    ble_gatt_lock(gatt);
    BleGattInfo* info = &gatt->info;
    uint8_t next = gatt->value_char + 1;
    // the stack searches from Char_Handle + 1, an empty range is invalid params (0x92)
    int32_t start = info->value_handle;
    // descriptors sit between value and next declaration, which is next value handle - 1
    int32_t end = next < info->char_count ? info->chars[next].value_handle - 2 : gatt->chars_end;
    info->descs_done = true;
    gatt->descs_end = end;
    if(end <= start) {
        ble_gatt_unlock(gatt);
        return false;
    }
    ble_gatt_put16(&p[0], gatt->conn_handle);
    ble_gatt_put16(&p[2], start);
    ble_gatt_put16(&p[4], end);
    return ble_gatt_command(gatt, BleGattStateBusy, OP_DISC_ALL_DESCS, p, 6);
}

bool ble_gatt_read_desc(BleGatt* gatt) {
    uint8_t p[4];
    ble_gatt_lock(gatt);
    BleGattInfo* info = &gatt->info;
    // one attempt per characteristic, a missing descriptor has no handle to read
    info->desc_done = true;
    info->desc[0] = '\0';
    if(!gatt->desc_handle) {
        ble_gatt_unlock(gatt);
        return false;
    }
    gatt->reading_desc = true;
    ble_gatt_put16(&p[0], gatt->conn_handle);
    ble_gatt_put16(&p[2], gatt->desc_handle);
    bool ok = ble_gatt_command(gatt, BleGattStateBusy, OP_READ_CHAR_DESC, p, 4);
    if(!ok) {
        // no response will come, don't let it eat the next one
        ble_gatt_lock(gatt);
        gatt->reading_desc = false;
        ble_gatt_unlock(gatt);
    }
    return ok;
}

bool ble_gatt_set_notify(BleGatt* gatt, bool enable) {
    uint8_t p[7];
    ble_gatt_lock(gatt);
    if(!gatt->info.cccd) {
        ble_gatt_unlock(gatt);
        return false;
    }
    gatt->info.notifying = enable;
    // 0x0002 when the char only indicates, the server rejects the other bit
    uint8_t props = gatt->info.chars[gatt->value_char].props;
    bool indicate = (props & BLE_GATT_PROP_INDICATE) && !(props & BLE_GATT_PROP_NOTIFY);
    ble_gatt_put16(&p[0], gatt->conn_handle);
    ble_gatt_put16(&p[2], gatt->info.cccd);
    p[4] = 2;
    ble_gatt_put16(&p[5], enable ? (indicate ? 0x0002 : 0x0001) : 0x0000);
    return ble_gatt_command(gatt, BleGattStateBusy, OP_WRITE_CHAR_DESC, p, 7);
}
