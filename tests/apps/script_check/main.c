/* script_check — measures the inputs a script produces, so the test can tell
 * a 400 ms press from a 0 ms one. Logs:
 *   check: btn N held M ms          (board-manager button events)
 *   check: touch held M ms at X,Y   (FT6336, first and last point)
 *   check: temp C centi-C           (SHT40, once a second) */
#include "fw2.h"
#include "platform/diag.h"
#include "pico/stdlib.h"

int main(void) {
    board_init();
    fw2_app_recovery_init();
    st7796_init();
    fw2_app_about_use_lcd();
    st7796_fill_screen(0x0000);
    board_backlight_set(1);
    ft6336_init();
    sht40_init();
    DIAG("script_check: ready\n");

    uint32_t down_ms[UARTKBD_BTN_COUNT] = { 0 };
    bool touching = false;
    uint32_t touch_ms = 0;
    uint16_t tx0 = 0, ty0 = 0, tx = 0, ty = 0;
    uint32_t next_temp = 0;
    for (;;) {
        fw2_app_recovery_task();
        uint32_t now = to_ms_since_boot(get_absolute_time());
        uartkbd_event_t ev;
        while (uartkbd_next_event(&ev)) {
            if (ev.btn >= UARTKBD_BTN_COUNT) continue;
            if (ev.pressed) down_ms[ev.btn] = now;
            else DIAG("check: btn %d held %u ms\n", (int)ev.btn, (unsigned)(now - down_ms[ev.btn]));
        }
        uint16_t x, y;
        bool t = ft6336_poll(&x, &y);
        if (t && !touching) { touching = true; touch_ms = now; tx0 = x; ty0 = y; }
        if (t) { tx = x; ty = y; }
        if (!t && touching) {
            touching = false;
            DIAG("check: touch held %u ms from %u,%u to %u,%u\n", (unsigned)(now - touch_ms),
                 (unsigned)tx0, (unsigned)ty0, (unsigned)tx, (unsigned)ty);
        }
        if (now >= next_temp) {
            next_temp = now + 250;
            sht40_reading_t r;
            if (sht40_read(&r)) DIAG("check: temp %d centi-C\n", (int)(r.temp_c * 100.0f + 0.5f));
        }
        sleep_ms(2);
    }
}
