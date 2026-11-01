#pragma once

// host stand-in for the bits of furi the view code uses

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// arm newlib has uint32_t = unsigned long, the app prints it with "%lu".
// match that on 64-bit hosts, else varargs read garbage. a macro, not a typedef:
// libc headers above already declared it (glibc unconditionally)
#define uint32_t unsigned long

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#define COUNT_OF(x) (sizeof(x) / sizeof(x[0]))
#define UNUSED(x)   (void)(x)

// handles the app struct holds, never touched by the view or input code on host
typedef struct FuriMutex FuriMutex;
typedef struct FuriMessageQueue FuriMessageQueue;
typedef struct FuriString FuriString;

// the event test compiles gatt_event.c, which takes the gatt lock per event
typedef enum {
    FuriStatusOk,
} FuriStatus;

#define FuriWaitForever 0xFFFFFFFFu
#define furi_check(x)   ((void)(x))

// provided by the test harness
FuriStatus furi_mutex_acquire(FuriMutex* mutex, uint32_t timeout);
void furi_mutex_release(FuriMutex* mutex);
