/* dev_leds.c — WS2812 RGB LED chain model (16 pixels, display CPU GPIO 21,
 * driven by a pio1 state machine).
 *
 * Each word pushed to the state machine is a left-justified 24-bit GRB
 * pixel. A gap of more than 50 us between words latches the frame, as the
 * real LEDs do after their reset time. Powered by zone 10 (RGB_LEDS). */
#include "emu/emu.h"
#include "hardware/pio.h"

#include <string.h>

static struct {
    uint32_t shift[EMU_NUM_LEDS];   /* being received */
    uint32_t shown[EMU_NUM_LEDS];   /* latched, what the LEDs emit (0xRRGGBB) */
    int      idx;
    uint64_t last_us;
} D;

static void latch(void) {
    memcpy(D.shown, D.shift, sizeof D.shown);
    D.idx = 0;
}

static void led_put(emu_pio_sm_device_t *d, uint32_t word) {
    (void)d;
    uint64_t now = emu_time_us();
    if (D.idx > 0 && now - D.last_us > 50) latch();
    D.last_us = now;
    uint32_t grb = word >> 8;
    uint32_t g = (grb >> 16) & 0xFF, r = (grb >> 8) & 0xFF, b = grb & 0xFF;
    if (D.idx < EMU_NUM_LEDS) D.shift[D.idx] = (r << 16) | (g << 8) | b;
    D.idx++;
    if (D.idx >= EMU_NUM_LEDS) latch();   /* full frame: show it now */
}

static emu_pio_sm_device_t s_dev = { .name = "ws2812", .put = led_put };

void emu_ws2812_program_bind(PIO pio, uint sm, uint pin, float freq, bool rgbw) {
    (void)freq;
    if (rgbw) emu_log("ws2812: RGBW mode not modelled");
    if (emu_verbose) emu_log("ws2812: pio%u sm%u on GPIO %u", pio->index, sm, pin);
    emu_pio_bind(pio->index, sm, &s_dev);
}

void emu_leds_init(void) { memset(&D, 0, sizeof D); }

void emu_leds_get(uint32_t out[EMU_NUM_LEDS]) {
    bool on = emu_rail_on(10);
    for (int i = 0; i < EMU_NUM_LEDS; i++) out[i] = on ? D.shown[i] : 0;
}
