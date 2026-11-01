#include "gatt_i.h"

#include <string.h>

#define HCI_EVT_DISCONN_COMPLETE 0x05
#define HCI_EVT_LE_META          0x3E
#define HCI_EVT_VENDOR           0xFF

#define LE_CONN_COMPLETE      0x01
#define LE_CONN_UPDATE        0x03
#define LE_DATA_LEN_CHANGE    0x07
#define LE_ENH_CONN_COMPLETE  0x0A
#define LE_PHY_UPDATE         0x0C
#define LE_ROLE_CENTRAL       0x00

#define ACI_FIND_INFO_RESP          0x0C04
#define ACI_READ_BY_TYPE_RESP       0x0C06
#define ACI_READ_RESP               0x0C07
#define ACI_READ_BLOB_RESP          0x0C08
#define ACI_READ_BY_GROUP_TYPE_RESP 0x0C0A
#define ACI_INDICATION              0x0C0E
#define ACI_NOTIFICATION            0x0C0F
#define ACI_PROC_COMPLETE           0x0C10
#define ACI_ERROR_RESP              0x0C11
#define ACI_PAIRING_COMPLETE        0x0401
#define ACI_PASS_KEY_REQ            0x0402
#define ACI_AUTHORIZATION_REQ       0x0403
#define ACI_BOND_LOST               0x0405
#define ACI_NUMERIC_COMPARISON      0x0409
#define ATT_ERR_INVALID_OFFSET      0x07
#define ATT_ERR_ATTR_NOT_FOUND      0x0A
#define FIND_INFO_FORMAT_UUID16     0x01
#define UUID_CHAR_DECLARATION       0x2803
#define UUID_USER_DESC              0x2901
#define UUID_CCCD                   0x2902
#define UUID_DEVICE_NAME            0x2A00

static uint16_t u16(const uint8_t* p) {
    return p[0] | (p[1] << 8);
}

static void ble_gatt_copy_uuid(uint8_t* dst, uint8_t* dst_len, const uint8_t* src, uint8_t len) {
    *dst_len = (len == 2 || len == 16) ? len : 0;
    memcpy(dst, src, *dst_len);
}

// d: status, handle, role, ... (same prefix for legacy and enhanced variant)
static BleEventAckStatus ble_gatt_on_le_meta(BleGatt* gatt, const uint8_t* d, uint8_t len) {
    uint8_t subevent = d[0];
    d++;
    if(subevent == LE_CONN_COMPLETE || subevent == LE_ENH_CONN_COMPLETE) {
        if(len < 4) return BleEventNotAck;
        // failures only come from our create connection (flipper never uses directed adv).
        // gap.c ignores status and would take them for a phone connection, claim them all
        if(d[0] != 0) {
            if(gatt->info.state == BleGattStateConnecting) {
                gatt->info.state = BleGattStateFailed;
                gatt->info.error = d[0];
            }
            return BleEventAckFlowEnable;
        }
        // peripheral links belong to the phone/bt service
        if(d[3] != LE_ROLE_CENTRAL) return BleEventNotAck;
        gatt->conn_handle = u16(&d[1]);
        gatt->hanging_up = false;
        // idle: user gave up while connecting, ble_gatt_tick hangs up
        if(gatt->info.state != BleGattStateIdle) {
            gatt->info.state = BleGattStateReady;
            gatt->info.error = 0;
        }
        return BleEventAckFlowEnable;
    }
    // link maintenance events of our connection, keep them away from gap.c
    uint16_t handle = BLE_GATT_NO_HANDLE;
    if(subevent == LE_DATA_LEN_CHANGE && len >= 3) handle = u16(&d[0]);
    if((subevent == LE_CONN_UPDATE || subevent == LE_PHY_UPDATE) && len >= 4) handle = u16(&d[1]);
    return (handle != BLE_GATT_NO_HANDLE && handle == gatt->conn_handle) ? BleEventAckFlowEnable :
                                                                            BleEventNotAck;
}

static void ble_gatt_on_services(BleGattInfo* info, const uint8_t* d, uint8_t len) {
    uint8_t entry_len = d[2];
    uint8_t data_len = MIN(d[3], len - 4);
    if(entry_len < 6) return;
    for(uint8_t i = 0; i + entry_len <= data_len; i += entry_len) {
        if(info->service_count >= BLE_GATT_MAX_SERVICES) break;
        const uint8_t* e = &d[4 + i];
        BleGattService* s = &info->services[info->service_count++];
        s->start = u16(&e[0]);
        s->end = u16(&e[2]);
        ble_gatt_copy_uuid(s->uuid, &s->uuid_len, &e[4], entry_len - 4);
    }
}

// entry: declaration handle, properties, value handle, uuid
static void ble_gatt_on_chars(BleGattInfo* info, const uint8_t* d, uint8_t len) {
    uint8_t entry_len = d[2];
    uint8_t data_len = MIN(d[3], len - 4);
    if(entry_len < 7) return;
    for(uint8_t i = 0; i + entry_len <= data_len; i += entry_len) {
        if(info->char_count >= BLE_GATT_MAX_CHARS) break;
        const uint8_t* e = &d[4 + i];
        BleGattChar* c = &info->chars[info->char_count++];
        c->props = e[2];
        c->value_handle = u16(&e[3]);
        ble_gatt_copy_uuid(c->uuid, &c->uuid_len, &e[5], entry_len - 5);
    }
}

// handle + 16-bit uuid pairs. 128-bit ones are vendor descriptors, never the cccd
static void ble_gatt_on_descs(BleGatt* gatt, const uint8_t* d, uint8_t len) {
    if(d[2] != FIND_INFO_FORMAT_UUID16) return;
    uint8_t data_len = MIN(d[3], len - 4);
    for(uint8_t i = 0; i + 4 <= data_len; i += 4) {
        uint16_t handle = u16(&d[4 + i]);
        uint16_t uuid = u16(&d[6 + i]);
        if(handle > gatt->descs_end) break;
        // next characteristic starts here when the char list was cut short
        if(uuid == UUID_CHAR_DECLARATION) gatt->descs_end = handle - 1;
        if(uuid == UUID_CCCD && !gatt->info.cccd) gatt->info.cccd = handle;
        if(uuid == UUID_USER_DESC && !gatt->desc_handle) gatt->desc_handle = handle;
    }
}

static void ble_gatt_set_value(BleGattInfo* info, const uint8_t* value, uint8_t len) {
    info->value_len = MIN(len, BLE_GATT_VALUE_LEN);
    memcpy(info->value, value, info->value_len);
    info->value_updates++;
    // a read that fills the chunk may have stopped at the mtu, read long gets the rest
    info->truncated = len >= BLE_GATT_READ_CHUNK;
}

// read blob responses of a read long continue where the plain read stopped
static void ble_gatt_append_value(BleGattInfo* info, const uint8_t* value, uint8_t len) {
    uint8_t room = BLE_GATT_VALUE_LEN - info->value_len;
    uint8_t n = MIN(len, room);
    memcpy(&info->value[info->value_len], value, n);
    info->value_len += n;
    info->value_updates++;
}

// the read that just landed was 0x2A00: keep it as the device's real name
static void ble_gatt_remember_name(BleGattInfo* info) {
    for(uint8_t i = 0; i < info->char_count; i++) {
        const BleGattChar* c = &info->chars[i];
        if(c->uuid_len != 2 || c->value_handle != info->value_handle) continue;
        if((c->uuid[0] | (c->uuid[1] << 8)) != UUID_DEVICE_NAME) continue;
        uint8_t len = MIN(info->value_len, BLE_SCAN_NAME_LEN - 1);
        for(uint8_t j = 0; j < len; j++) {
            char ch = info->value[j];
            info->peer_name[j] = (ch >= 0x20 && ch < 0x7F) ? ch : '?';
        }
        info->peer_name[len] = '\0';
        info->peer_name_set = len > 0;
        return;
    }
}

// descriptor names are remote bytes too, font only has ascii
static void ble_gatt_set_desc(BleGattInfo* info, const uint8_t* value, uint8_t len) {
    len = MIN(len, BLE_GATT_NAME_LEN - 1);
    for(uint8_t i = 0; i < len; i++) {
        char c = value[i];
        info->desc[i] = (c >= 0x20 && c < 0x7F) ? c : '?';
    }
    info->desc[len] = '\0';
}

// d: ecode, then connection handle first for all att/gatt client events
static BleEventAckStatus ble_gatt_on_vendor(BleGatt* gatt, const uint8_t* d, uint8_t len) {
    if(len < 4 || gatt->conn_handle == BLE_GATT_NO_HANDLE) return BleEventNotAck;
    uint16_t ecode = u16(d);
    d += 2;
    len -= 2;
    if(u16(d) != gatt->conn_handle) return BleEventNotAck;

    BleGattInfo* info = &gatt->info;
    switch(ecode) {
    case ACI_FIND_INFO_RESP:
        if(len >= 4) ble_gatt_on_descs(gatt, d, len);
        break;
    case ACI_READ_BY_GROUP_TYPE_RESP:
        if(len >= 4) ble_gatt_on_services(info, d, len);
        break;
    case ACI_READ_BY_TYPE_RESP:
        if(len >= 4) ble_gatt_on_chars(info, d, len);
        break;
    case ACI_READ_RESP:
        if(gatt->reading_desc) {
            if(len >= 3) ble_gatt_set_desc(info, &d[3], MIN(d[2], len - 3));
            gatt->reading_desc = false;
            info->desc_done = true;
        } else if(len >= 3) {
            ble_gatt_set_value(info, &d[3], MIN(d[2], len - 3));
            ble_gatt_remember_name(info);
        }
        break;
    case ACI_READ_BLOB_RESP:
        // same payload as a read, only the meaning depends on the procedure
        if(gatt->reading_long && len >= 3) {
            ble_gatt_append_value(info, &d[3], MIN(d[2], len - 3));
        }
        break;
    case ACI_INDICATION:
        if(len >= 5 && u16(&d[2]) == info->value_handle) {
            ble_gatt_set_value(info, &d[5], MIN(d[4], len - 5));
            // confirmation goes out on the app thread, see ble_gatt_tick
            gatt->confirm_pending = true;
        }
        break;
    case ACI_NOTIFICATION:
        if(len >= 5 && u16(&d[2]) == info->value_handle) {
            ble_gatt_set_value(info, &d[5], MIN(d[4], len - 5));
        }
        break;
    case ACI_PROC_COMPLETE:
        if(info->state == BleGattStateBusy) info->state = BleGattStateReady;
        if(len >= 3 && d[2]) info->error = d[2];
        if(gatt->reading_desc) {
            // no read response came, the error code above says why
            gatt->reading_desc = false;
            info->desc_done = true;
        }
        // the procedure behind a read long or a write is done, either way
        gatt->reading_long = false;
        info->long_done = info->long_done || info->truncated;
        info->truncated = false;
        if(info->write_pending) {
            info->write_pending = false;
            info->write_done = true;
        }
        break;
    case ACI_ERROR_RESP:
        // attribute not found just ends a discovery
        if(len >= 6 && d[5] != ATT_ERR_ATTR_NOT_FOUND) info->error = d[5];
        if(gatt->reading_desc) {
            gatt->reading_desc = false;
            info->desc_done = true;
        }
        if(gatt->reading_long) {
            // invalid offset: the value ended exactly at the read chunk, not an error
            if(len >= 6 && d[5] == ATT_ERR_INVALID_OFFSET) info->error = 0;
            gatt->reading_long = false;
            info->long_done = true;
            info->truncated = false;
        }
        if(info->write_pending) {
            info->write_pending = false;
            info->write_done = true;
        }
        break;
    case ACI_PAIRING_COMPLETE:
        if(len >= 4) {
            info->pair_confirm = false;
            info->pair_state = d[2] ? 3 : 2;
            info->pair_reason = d[2] ? d[3] : 0;
            if(!d[2]) {
                // the error that started the pairing is history, redo the read it blocked
                info->error = 0;
                info->resume_read = true;
            }
        }
        break;
    case ACI_NUMERIC_COMPARISON:
        // the peer shows the same six digits, we show them and wait for OK
        if(len >= 6) {
            info->pair_numeric = d[2] | (d[3] << 8) | (d[4] << 16) | ((uint32_t)d[5] << 24);
            info->pair_state = 1;
            info->pair_confirm = true;
        }
        break;
    case ACI_PASS_KEY_REQ:
        // no keypad: a wrong key fails the pairing at once instead of a 30s timeout
        info->pair_state = 3;
        info->pair_reason = BLE_GATT_ERR_PASSKEY;
        gatt->passkey_pending = true;
        break;
    case ACI_AUTHORIZATION_REQ:
        gatt->authorize_pending = true;
        break;
    case ACI_BOND_LOST:
        gatt->rebond_pending = true;
        break;
    default:
        break;
    }
    return BleEventAckFlowEnable;
}

BleEventAckStatus ble_gatt_event_handler(void* event, void* context) {
    BleGatt* gatt = context;
    // hci_uart_pckt: type, evt code, param len, params
    const uint8_t* pckt = event;
    const uint8_t* d = &pckt[3];
    uint8_t len = pckt[2];
    BleEventAckStatus status = BleEventNotAck;

    // must not drop own events (gap.c would mistake them for the phone link). app never
    // holds the lock across an hci command, so waiting here cannot deadlock
    furi_check(furi_mutex_acquire(gatt->mutex, FuriWaitForever) == FuriStatusOk);
    if(pckt[1] == HCI_EVT_DISCONN_COMPLETE) {
        if(len >= 4 && gatt->conn_handle != BLE_GATT_NO_HANDLE && u16(&d[1]) == gatt->conn_handle) {
            gatt->conn_handle = BLE_GATT_NO_HANDLE;
            gatt->hanging_up = false;
            // idle = we hung up ourselves, anything else is a lost link
            if(gatt->info.state != BleGattStateIdle) {
                gatt->info.state = BleGattStateFailed;
                gatt->info.error = d[3];
            }
            gatt->info.notifying = false;
            gatt->reading_desc = false;
            gatt->confirm_pending = false;
            status = BleEventAckFlowEnable;
        }
    } else if(pckt[1] == HCI_EVT_LE_META && len >= 1) {
        status = ble_gatt_on_le_meta(gatt, d, len - 1);
    } else if(pckt[1] == HCI_EVT_VENDOR) {
        status = ble_gatt_on_vendor(gatt, d, len);
    }
    furi_mutex_release(gatt->mutex);
    return status;
}
