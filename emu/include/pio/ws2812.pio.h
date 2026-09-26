/* ws2812.pio.h — host program model for bsp/leds/ws2812.pio.
 *
 * On hardware pioasm generates this header. Here the "program" binds the
 * state machine to the emulated WS2812 chain: every 32-bit word pushed into
 * the TX FIFO is a left-justified GRB pixel, exactly as the real program
 * shifts it out on the data pin. */
#ifndef FW2EMU_WS2812_PIO_H
#define FW2EMU_WS2812_PIO_H
#include "hardware/pio.h"
#ifdef __cplusplus
extern "C" {
#endif
static const uint16_t ws2812_program_instructions[] = { 0x6221, 0x1123, 0x1400, 0xa442 };
static const pio_program_t ws2812_program = {
    .instructions = ws2812_program_instructions, .length = 4, .origin = -1,
};
void emu_ws2812_program_bind(PIO pio, uint sm, uint pin, float freq, bool rgbw);
static inline void ws2812_program_init(PIO pio, uint sm, uint offset, uint pin, float freq, bool rgbw) {
    (void)offset;
    pio_gpio_init(pio, pin);
    emu_ws2812_program_bind(pio, sm, pin, freq, rgbw);
    pio_sm_set_enabled(pio, sm, true);
}
#ifdef __cplusplus
}
#endif
#endif
