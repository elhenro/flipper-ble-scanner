#include "view.h"

#include <stdio.h>

typedef struct {
    const char* title;
    const char* lines[5];
} BleInfoPage;

static const BleInfoPage pages[BLE_INFO_PAGES] = {
    {"Distance is a guess",
     {"Estimated from RSSI.",
      "Walls, bodies, pockets,",
      "antenna angle, TX power",
      "skew it easily 2x.",
      "Calibrate: hold OK at 1m"}},
    {"Keys",
     {"OK  details / connect",
      "</>  ref dB (in details)",
      "Hold OK  set 1m ref",
      "C connectable R answered",
      "Back  up / exit"}},
};

void ble_view_draw_info(Canvas* canvas, const BleViewModel* model) {
    const BleInfoPage* page = &pages[model->info_page % BLE_INFO_PAGES];
    char counter[8];
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 0, 10, page->title);
    canvas_set_font(canvas, FontSecondary);
    snprintf(counter, sizeof(counter), "%u/%u", model->info_page + 1, BLE_INFO_PAGES);
    canvas_draw_str_aligned(canvas, 127, 10, AlignRight, AlignBottom, counter);
    for(size_t i = 0; i < COUNT_OF(page->lines); i++) {
        canvas_draw_str(canvas, 0, 22 + i * 10, page->lines[i]);
    }
}
