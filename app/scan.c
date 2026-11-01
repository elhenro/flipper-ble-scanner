#include "scan.h"
#include "adv.h"

#include <furi_hal_bt.h>
#include <furi_ble/event_dispatcher.h>
#include <string.h>

#define HCI_EVT_LE_META       0x3E
#define HCI_SUBEVT_ADV_REPORT 0x02
// report event types: adv_ind, adv_direct_ind accept connections; 0x04 = scan response
#define ADV_EVT_DIRECT_IND    0x01
#define ADV_EVT_SCAN_RSP      0x04
#define RSSI_UNAVAILABLE      127

// 60ms interval, 30ms window: half duty leaves airtime for own adv/connection
#define SCAN_INTERVAL 96
#define SCAN_WINDOW   48
// ema weight of new rssi sample, smooths multipath jitter
#define RSSI_ALPHA 0.25f
// fixed part of one adv report: event_type, addr_type, addr[6], data_len, rssi
#define REPORT_FIXED_LEN 10

struct BleScan {
    FuriMutex* mutex;
    GapSvcEventHandler* handler;
    bool active;
    BleDevice devices[BLE_SCAN_MAX_DEVICES];
    size_t count;
    uint32_t reports;
};

static BleDevice* ble_scan_slot(BleScan* scan, uint8_t addr_type, const uint8_t* addr) {
    BleDevice* oldest = &scan->devices[0];
    for(size_t i = 0; i < scan->count; i++) {
        BleDevice* dev = &scan->devices[i];
        if(dev->addr_type == addr_type && memcmp(dev->addr, addr, 6) == 0) return dev;
        if(dev->last_seen < oldest->last_seen) oldest = dev;
    }
    // table full: recycle least recently seen
    BleDevice* dev = scan->count < BLE_SCAN_MAX_DEVICES ? &scan->devices[scan->count++] : oldest;
    memset(dev, 0, sizeof(BleDevice));
    dev->addr_type = addr_type;
    memcpy(dev->addr, addr, 6);
    dev->company = BLE_SCAN_NO_COMPANY;
    return dev;
}

static void ble_scan_on_report(BleScan* scan, const uint8_t* report) {
    uint8_t data_len = report[8];
    int8_t rssi = (int8_t)report[9 + data_len];
    if(rssi == RSSI_UNAVAILABLE) return;

    BleDevice* dev = ble_scan_slot(scan, report[1], &report[2]);
    ble_adv_parse(dev, &report[9], data_len);
    if(report[0] <= ADV_EVT_DIRECT_IND) dev->connectable = true;
    if(report[0] == ADV_EVT_SCAN_RSP) dev->scan_rsp = true;
    dev->rssi = rssi;
    dev->rssi_avg = dev->seen_count ? dev->rssi_avg + RSSI_ALPHA * (rssi - dev->rssi_avg) : rssi;
    dev->seen_count++;
    dev->last_seen = furi_get_tick();
}

// runs on ble event thread, keep it short
static BleEventAckStatus ble_scan_event_handler(void* event, void* context) {
    BleScan* scan = context;
    // hci_uart_pckt: type, evt code, param len, params (subevent first)
    const uint8_t* pckt = event;
    if(pckt[1] != HCI_EVT_LE_META || pckt[3] != HCI_SUBEVT_ADV_REPORT) return BleEventNotAck;

    const uint8_t* end = &pckt[3] + pckt[2];
    uint8_t num_reports = pckt[4];
    const uint8_t* p = &pckt[5];

    // drop sample instead of stalling ble thread when ui holds the lock
    if(furi_mutex_acquire(scan->mutex, 5) != FuriStatusOk) return BleEventAckFlowEnable;
    for(uint8_t i = 0; i < num_reports && p + REPORT_FIXED_LEN <= end; i++) {
        size_t report_len = REPORT_FIXED_LEN + p[8];
        if(p + report_len > end) break;
        ble_scan_on_report(scan, p);
        p += report_len;
    }
    scan->reports++;
    furi_mutex_release(scan->mutex);
    return BleEventAckFlowEnable;
}

BleScan* ble_scan_alloc(void) {
    // zeroed by furi malloc
    BleScan* scan = malloc(sizeof(BleScan));
    scan->mutex = furi_mutex_alloc(FuriMutexTypeNormal);
    return scan;
}

void ble_scan_free(BleScan* scan) {
    ble_scan_stop(scan);
    if(scan->handler) {
        // let queued reports drain before unhooking, dispatcher list is not locked
        furi_delay_ms(100);
        ble_event_dispatcher_unregister_svc_handler(scan->handler);
    }
    furi_mutex_free(scan->mutex);
    free(scan);
}

bool ble_scan_is_supported(void) {
    return furi_hal_bt_is_alive() && furi_hal_bt_get_radio_stack() == FuriHalBtStackFull;
}

uint8_t ble_scan_start(BleScan* scan) {
    if(scan->active) return 0;
    if(!ble_scan_is_supported()) return BLE_SCAN_UNSUPPORTED;

    // registered once for the app's lifetime: unhooking under traffic races the ble thread.
    // newest handler runs first and gatt's is older, so reports never reach it
    if(!scan->handler) {
        scan->handler = ble_event_dispatcher_register_svc_handler(ble_scan_event_handler, scan);
    }
    // active: names mostly live in scan responses
    uint8_t status = furi_hal_bt_start_scan(SCAN_INTERVAL, SCAN_WINDOW, true);
    scan->active = status == 0;
    return status;
}

void ble_scan_stop(BleScan* scan) {
    if(!scan->active) return;
    furi_hal_bt_stop_scan();
    scan->active = false;
}

/** keeps a name read over gatt on the device record, returns false if it is gone */
bool ble_scan_set_gatt_name(BleScan* scan, const uint8_t* addr, const char* name) {
    bool found = false;
    furi_check(furi_mutex_acquire(scan->mutex, FuriWaitForever) == FuriStatusOk);
    for(size_t i = 0; i < scan->count; i++) {
        BleDevice* dev = &scan->devices[i];
        if(memcmp(dev->addr, addr, 6) != 0) continue;
        strlcpy(dev->gatt_name, name, sizeof(dev->gatt_name));
        found = true;
        break;
    }
    furi_mutex_release(scan->mutex);
    return found;
}

size_t ble_scan_snapshot(BleScan* scan, BleDevice* out, size_t max, uint32_t max_age_ms) {
    uint32_t now = furi_get_tick();
    uint32_t max_age = furi_ms_to_ticks(max_age_ms);
    size_t n = 0;
    furi_check(furi_mutex_acquire(scan->mutex, FuriWaitForever) == FuriStatusOk);
    for(size_t i = 0; i < scan->count && n < max; i++) {
        const BleDevice* dev = &scan->devices[i];
        if(now - dev->last_seen > max_age) continue;
        // insertion sort by smoothed rssi, qsort is not exported to faps
        size_t j = n++;
        while(j > 0 && out[j - 1].rssi_avg < dev->rssi_avg) {
            out[j] = out[j - 1];
            j--;
        }
        out[j] = *dev;
    }
    furi_mutex_release(scan->mutex);
    return n;
}

uint32_t ble_scan_report_count(BleScan* scan) {
    furi_check(furi_mutex_acquire(scan->mutex, FuriWaitForever) == FuriStatusOk);
    uint32_t reports = scan->reports;
    furi_mutex_release(scan->mutex);
    return reports;
}
