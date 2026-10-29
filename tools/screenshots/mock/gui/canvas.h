#pragma once

// subset of flipper's gui/canvas.h the app draws with, backed by u8g2 in canvas.c

#include <stddef.h>
#include <stdint.h>

typedef struct Canvas Canvas;

typedef enum {
    FontPrimary,
    FontSecondary,
} Font;

typedef enum {
    AlignLeft,
    AlignRight,
    AlignTop,
    AlignBottom,
    AlignCenter,
} Align;

typedef enum {
    ColorWhite = 0x00,
    ColorBlack = 0x01,
} Color;

void canvas_clear(Canvas* canvas);
void canvas_set_color(Canvas* canvas, Color color);
void canvas_set_font(Canvas* canvas, Font font);
void canvas_draw_str(Canvas* canvas, int32_t x, int32_t y, const char* str);
void canvas_draw_str_aligned(
    Canvas* canvas,
    int32_t x,
    int32_t y,
    Align horizontal,
    Align vertical,
    const char* str);
void canvas_draw_line(Canvas* canvas, int32_t x1, int32_t y1, int32_t x2, int32_t y2);
void canvas_draw_box(Canvas* canvas, int32_t x, int32_t y, size_t width, size_t height);

// host only: 128x64 framebuffer, 8 pages of 128 bytes, lsb = top pixel
Canvas* canvas_host_alloc(void);
const uint8_t* canvas_host_buffer(Canvas* canvas);
