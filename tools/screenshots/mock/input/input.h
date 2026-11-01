#pragma once

// subset of flipper's input types the input handler switches on

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    InputKeyUp,
    InputKeyDown,
    InputKeyRight,
    InputKeyLeft,
    InputKeyOk,
    InputKeyBack,
    InputKeyMAX,
} InputKey;

typedef enum {
    InputTypePress,
    InputTypeRelease,
    InputTypeShort,
    InputTypeLong,
    InputTypeRepeat,
} InputType;

typedef struct {
    InputKey key;
    InputType type;
} InputEvent;
