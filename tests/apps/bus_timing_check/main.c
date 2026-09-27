/* bus_timing_check — times real WiliBSP calls against what the wire allows.
 *
 * WiliBSP asks for a 100 MHz LCD clock from a 250 MHz clk_peri; the PL022's
 * dividers give 62.5 MHz, so a full 480x320 RGB565 screen (307 200 bytes)
 * takes 39.3 ms on the wire. I2C1 runs at 400 kHz.
 *
 * Logs:
 *   bt: spi N Hz                                 (spi_get_baudrate)
 *   bt: fill_us N                                (st7796_fill_screen, blocking)
 *   bt: flush_return_us N flush_done_us N        (st7796_flush_async, DMA)
 *   bt: i2c_us N                                 (100 x 1-byte write + 6-byte read)
 *   bt: PASS | bt: FAIL <what>
 * The CPU may run up to 0.3 ms ahead of the wire, which the lower bounds
 * allow for; they are what prove the timing. The upper bounds are loose so
 * a slow CI machine or a sanitizer build doesn't fail it. */
#include "fw2.h"
#include "platform/diag.h"
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/spi.h"

static uint16_t s_frame[480 * 320];

static uint32_t us_since(absolute_time_t t0) { return (uint32_t)absolute_time_diff_us(t0, get_absolute_time()); }

int main(void) {
    board_init();
    fw2_app_recovery_init();
    st7796_init();
    board_backlight_set(1);
    bool ok = true;

    uint32_t hz = spi_get_baudrate(spi1);
    DIAG("bt: spi %u Hz\n", (unsigned)hz);
    if (hz != 62500000u) { DIAG("bt: FAIL spi clock\n"); ok = false; }

    st7796_fill_screen(0x0000);                        /* settle */
    absolute_time_t t0 = get_absolute_time();
    for (int i = 0; i < 5; i++) st7796_fill_screen(i & 1 ? 0xFFFF : 0x001F);
    uint32_t fill = us_since(t0) / 5u;
    DIAG("bt: fill_us %u\n", (unsigned)fill);
    if (fill < 38800u || fill > 90000u) { DIAG("bt: FAIL fill\n"); ok = false; }

    for (unsigned i = 0; i < 480u * 320u; i++) s_frame[i] = (uint16_t)i;
    t0 = get_absolute_time();
    st7796_flush_async(0, 0, 479, 319, s_frame, NULL);
    uint32_t ret = us_since(t0);
    st7796_flush_wait();
    uint32_t done = us_since(t0);
    DIAG("bt: flush_return_us %u flush_done_us %u\n", (unsigned)ret, (unsigned)done);
    if (ret > 20000u) { DIAG("bt: FAIL flush blocked the CPU\n"); ok = false; }
    if (done < 39000u || done > 90000u) { DIAG("bt: FAIL flush\n"); ok = false; }

    /* FT6336 at 0x38: register pointer, then 6 bytes. Per transfer:
     * (2*9+2) + (7*9+2) = 85 SCL clocks = 212.5 us at 400 kHz. */
    uint8_t reg = 0x02, buf[6];
    t0 = get_absolute_time();
    for (int i = 0; i < 100; i++) {
        i2c_write_blocking(i2c1, 0x38, &reg, 1, true);
        i2c_read_blocking(i2c1, 0x38, buf, sizeof buf, false);
    }
    uint32_t i2c = us_since(t0);
    DIAG("bt: i2c_us %u\n", (unsigned)i2c);
    if (i2c < 20600u || i2c > 60000u) { DIAG("bt: FAIL i2c\n"); ok = false; }

    DIAG(ok ? "bt: PASS\n" : "bt: FAIL\n");
    for (;;) { fw2_app_recovery_task(); tight_loop_contents(); }
}
