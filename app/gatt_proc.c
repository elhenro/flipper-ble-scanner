// gatt client procedures on the connected link, results arrive in gatt_event.c
#include "gatt_i.h"

#define OP_DISC_ALL_SERVICES 0xFD12
#define OP_DISC_ALL_CHARS    0xFD15
#define OP_DISC_ALL_DESCS    0xFD17
#define OP_READ_CHAR_VALUE   0xFD18
#define OP_READ_LONG_VALUE   0xFD1A
#define OP_WRITE_CHAR_VALUE  0xFD1B
#define OP_WRITE_NO_RESP     0xFD23
#define OP_WRITE_CHAR_DESC   0xFD21
#define OP_READ_CHAR_DESC    0xFD22
#define OP_SEND_PAIRING_REQ  0xFC9F
#define OP_CONFIRM_YESNO     0xFCA5

bool ble_gatt_discover_services(BleGatt* gatt) {
    uint8_t p[2];
    ble_gatt_lock(gatt);
    gatt->info.service_count = 0;
    gatt->info.services_done = true;
    ble_gatt_put16(p, gatt->conn_handle);
    bool ok = ble_gatt_command(gatt, BleGattStateBusy, OP_DISC_ALL_SERVICES, p, 2);
    if(!ok) {
        // refused, no procedure will run: let the next tick try again
        ble_gatt_lock(gatt);
        gatt->info.services_done = false;
        ble_gatt_unlock(gatt);
    }
    return ok;
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
        info->truncated = false;
        info->long_done = false;
        info->write_done = false;
        gatt->desc_handle = 0;
        gatt->reading_desc = false;
        gatt->reading_long = false;
    }
    ble_gatt_unlock(gatt);
}

bool ble_gatt_read(BleGatt* gatt) {
    uint8_t p[4];
    ble_gatt_lock(gatt);
    BleGattInfo* info = &gatt->info;
    // a fresh read starts over: no leftover tail, no stale write result
    info->truncated = false;
    info->long_done = false;
    info->write_done = false;
    info->resume_read = false;
    gatt->reading_long = false;
    ble_gatt_put16(&p[0], gatt->conn_handle);
    ble_gatt_put16(&p[2], info->value_handle);
    return ble_gatt_command(gatt, BleGattStateBusy, OP_READ_CHAR_VALUE, p, 4);
}

bool ble_gatt_read_long(BleGatt* gatt) {
    uint8_t p[6];
    ble_gatt_lock(gatt);
    BleGattInfo* info = &gatt->info;
    // one attempt per value: a short value would answer invalid offset
    info->truncated = false;
    info->long_done = true;
    gatt->reading_long = true;
    ble_gatt_put16(&p[0], gatt->conn_handle);
    ble_gatt_put16(&p[2], info->value_handle);
    ble_gatt_put16(&p[4], info->value_len); // read blob continues where the read stopped
    bool ok = ble_gatt_command(gatt, BleGattStateBusy, OP_READ_LONG_VALUE, p, 6);
    if(!ok) {
        ble_gatt_lock(gatt);
        gatt->reading_long = false;
        ble_gatt_unlock(gatt);
    }
    return ok;
}

bool ble_gatt_write(BleGatt* gatt, const uint8_t* data, uint8_t len) {
    uint8_t p[5 + BLE_GATT_VALUE_LEN];
    ble_gatt_lock(gatt);
    BleGattInfo* info = &gatt->info;
    if(gatt->value_char >= info->char_count || len > BLE_GATT_VALUE_LEN) {
        ble_gatt_unlock(gatt);
        return false;
    }
    // without response when that is all the char offers: no procedure, no error
    bool with_resp = info->chars[gatt->value_char].props & BLE_GATT_PROP_WRITE;
    ble_gatt_put16(&p[0], gatt->conn_handle);
    ble_gatt_put16(&p[2], info->value_handle);
    p[4] = len;
    memcpy(&p[5], data, len);
    info->write_done = false;
    info->write_len = len;
    info->write_pending = with_resp;
    bool ok = ble_gatt_command(
        gatt, with_resp ? BleGattStateBusy : BleGattStateReady, with_resp ? OP_WRITE_CHAR_VALUE : OP_WRITE_NO_RESP, p, 5 + len);
    if(!ok) {
        ble_gatt_lock(gatt);
        info->write_pending = false;
        ble_gatt_unlock(gatt);
    } else if(!with_resp) {
        // nothing will report back, the write left with the command
        ble_gatt_lock(gatt);
        info->write_done = true;
        ble_gatt_unlock(gatt);
    }
    return ok;
}

bool ble_gatt_pair(BleGatt* gatt) {
    uint8_t p[3];
    ble_gatt_lock(gatt);
    BleGattInfo* info = &gatt->info;
    info->pair_state = 1;
    info->pair_reason = 0;
    info->pair_confirm = false;
    ble_gatt_put16(&p[0], gatt->conn_handle);
    p[2] = 0; // reuse stored keys, rebond only when the peer insists
    return ble_gatt_command(gatt, BleGattStateReady, OP_SEND_PAIRING_REQ, p, 3);
}

bool ble_gatt_pair_confirm(BleGatt* gatt, bool yes) {
    uint8_t p[3];
    ble_gatt_lock(gatt);
    gatt->info.pair_confirm = false;
    ble_gatt_put16(&p[0], gatt->conn_handle);
    p[2] = yes ? 1 : 0;
    return ble_gatt_command(gatt, BleGattStateReady, OP_CONFIRM_YESNO, p, 3);
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
    bool ok = ble_gatt_command(gatt, BleGattStateBusy, OP_DISC_ALL_DESCS, p, 6);
    if(!ok) {
        // refused, no procedure will run: let the next tick try again
        ble_gatt_lock(gatt);
        info->descs_done = false;
        ble_gatt_unlock(gatt);
    }
    return ok;
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
        info->desc_done = false;
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
