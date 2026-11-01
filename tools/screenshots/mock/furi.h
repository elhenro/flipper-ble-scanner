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
