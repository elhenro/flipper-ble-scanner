#pragma once

// host stand-in for the bits of furi the view code uses

// arm newlib has uint32_t = unsigned long, the app prints it with "%lu".
// match that on 64-bit hosts (macos guard name), else varargs read garbage
#ifndef _UINT32_T
#define _UINT32_T
typedef unsigned long uint32_t;
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#define COUNT_OF(x) (sizeof(x) / sizeof(x[0]))
#define UNUSED(x)   (void)(x)
