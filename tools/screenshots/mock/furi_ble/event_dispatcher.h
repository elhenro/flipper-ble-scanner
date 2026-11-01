#pragma once

// host stand-in for the two dispatcher types gatt_event.c names

typedef enum {
    BleEventNotAck,
    BleEventAckFlowEnable,
} BleEventAckStatus;

typedef void* GapSvcEventHandler;
