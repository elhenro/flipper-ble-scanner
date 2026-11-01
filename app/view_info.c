#include "view.h"

#include <stdio.h>

typedef struct {
    const char* title;
    const char* lines[5];
} BleInfoPage;

// lines must stay under 22 chars: FontSecondary is 6 px wide
static const BleInfoPage pages[BLE_INFO_PAGES] = {
    {"Signal strength",
     {"RSSI in dBm:",
      "0 loud, -100 faint",
      "Row: averaged value",
      "Details: now + avg",
      "Closer is stronger."}},
    {"Reading it",
     {"Walls, bodies and",
      "pockets cost 10-20",
      "dB each. Compare",
      "devices, not the",
      "absolute number."}},
    {"Keys",
     {"OK  details/connect",
      "Up/Dn  select, page",
      "Left  info screen",
      "C  connectable",
      "Back  up / exit"}},
    {"GATT keys",
     {"OK  read value",
      "Right  notify",
      "Hold OK  dump",
      "gatt.log over usb",
      "Back  up / disconnect"}},
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
