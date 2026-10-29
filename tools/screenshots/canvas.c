// flipper canvas on host u8g2, same fonts and alignment math as
// applications/services/gui/canvas.c so pixels match the device
#include <gui/canvas.h>
#include "u8g2.h"

#include <stdlib.h>

struct Canvas {
    u8g2_t fb;
    uint8_t buf[1024];
};

static const u8x8_display_info_t display_info = {
    .tile_width = 16,
    .tile_height = 8,
    .pixel_width = 128,
    .pixel_height = 64,
};

static uint8_t host_display_cb(u8x8_t* u8x8, uint8_t msg, uint8_t arg_int, void* arg_ptr) {
    (void)arg_int;
    (void)arg_ptr;
    if(msg == U8X8_MSG_DISPLAY_SETUP_MEMORY) u8x8_d_helper_display_setup_memory(u8x8, &display_info);
    return 1;
}

Canvas* canvas_host_alloc(void) {
    Canvas* canvas = calloc(1, sizeof(Canvas));
    u8g2_SetupDisplay(&canvas->fb, host_display_cb, u8x8_cad_empty, u8x8_byte_empty, u8x8_dummy_cb);
    u8g2_SetupBuffer(&canvas->fb, canvas->buf, 8, u8g2_ll_hvline_vertical_top_lsb, U8G2_R0);
    return canvas;
}

const uint8_t* canvas_host_buffer(Canvas* canvas) {
    return canvas->buf;
}

void canvas_clear(Canvas* canvas) {
    u8g2_ClearBuffer(&canvas->fb);
}

void canvas_set_color(Canvas* canvas, Color color) {
    u8g2_SetDrawColor(&canvas->fb, color);
}

void canvas_set_font(Canvas* canvas, Font font) {
    u8g2_SetFontMode(&canvas->fb, 1);
    u8g2_SetFont(
        &canvas->fb, font == FontPrimary ? u8g2_font_helvB08_tr : u8g2_font_haxrcorp4089_tr);
}

void canvas_draw_str(Canvas* canvas, int32_t x, int32_t y, const char* str) {
    if(str) u8g2_DrawUTF8(&canvas->fb, x, y, str);
}

void canvas_draw_str_aligned(
    Canvas* canvas,
    int32_t x,
    int32_t y,
    Align horizontal,
    Align vertical,
    const char* str) {
    if(!str) return;
    if(horizontal == AlignRight) x -= u8g2_GetUTF8Width(&canvas->fb, str);
    if(horizontal == AlignCenter) x -= u8g2_GetUTF8Width(&canvas->fb, str) / 2;
    if(vertical == AlignTop) y += u8g2_GetAscent(&canvas->fb);
    if(vertical == AlignCenter) y += u8g2_GetAscent(&canvas->fb) / 2;
    u8g2_DrawUTF8(&canvas->fb, x, y, str);
}

void canvas_draw_line(Canvas* canvas, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    u8g2_DrawLine(&canvas->fb, x1, y1, x2, y2);
}

void canvas_draw_box(Canvas* canvas, int32_t x, int32_t y, size_t width, size_t height) {
    u8g2_DrawBox(&canvas->fb, x, y, width, height);
}
