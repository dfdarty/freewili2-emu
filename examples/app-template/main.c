/* my_app — a starting point: counts OK presses on the screen, and GREEN /
 * RED colour the 16 LEDs. HOME held for 5 s exits (WiliBSP's rule for every
 * app). Replace the body of the loop with your own app. */
#include "fw2.h"
#include "hardware/pio.h"
#include "platform/diag.h"
#include <stdio.h>

// The LCD driver takes RGB565 colours in wire (big-endian) byte order.
static inline uint16_t be16(uint16_t c) { return (uint16_t)((c >> 8) | (c << 8)); }

int main(void) {
    board_init();
    fw2_app_recovery_init();          // HOME held 5 s exits; also starts the button link
    st7796_init();
    fw2_app_about_use_lcd();
    st7796_fill_screen(be16(0x0000));
    board_backlight_set(1);

    ws2812_init(pio1, 0, PIN_LED_DATA);
    ws2812_set_brightness(64);

    unsigned count = 0;
    rgb_t colour = { .r = 0, .g = 0, .b = 255 };
    bool redraw = true;
    DIAG("my_app: ready\n");

    for (;;) {
        fw2_app_recovery_task();

        uartkbd_event_t ev;
        while (uartkbd_next_event(&ev)) {
            if (!ev.pressed) continue;
            if (ev.btn == UARTKBD_BTN_OK) { count++; redraw = true; }
            if (ev.btn == UARTKBD_BTN_GREEN) { colour = (rgb_t){ .r = 0, .g = 255, .b = 0 }; redraw = true; }
            if (ev.btn == UARTKBD_BTN_RED)   { colour = (rgb_t){ .r = 255, .g = 0, .b = 0 }; redraw = true; }
        }

        if (redraw) {
            char line[32];
            snprintf(line, sizeof line, "OK PRESSED %u TIMES", count);
            st7796_fill_rect(0, 140, 480, 40, be16(0x0000));
            st7796_draw_text(40, 150, 3, be16(0xFFE0), be16(0x0000), line);
            ws2812_fill(colour);
            ws2812_show();
            DIAG("count=%u\n", count);
            redraw = false;
        }
        tight_loop_contents();
    }
}
