/* pdm_capture.pio.h — host program model for bsp/pdm/pdm_capture.pio.
 *
 * pio_sm_init() sees .emu_model and binds the state machine to the
 * emulated 4-mic PDM array (emu/src/dev_pdm.c). The program runs 6 PIO
 * cycles per mic clock and packs 4 bits per clock (C, A, D, B from the
 * MSB), autopushing one word every 8 clocks — the model produces exactly
 * that stream at clk_sys / clkdiv / 6. */
#ifndef FW2EMU_PDM_CAPTURE_PIO_H
#define FW2EMU_PDM_CAPTURE_PIO_H
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "platform/board.h"
#ifdef __cplusplus
extern "C" {
#endif
#define pdm_capture_wrap_target 0
#define pdm_capture_wrap 3
#define PDM_CAPTURE_CYCLES_PER_CLOCK 6u
static const uint16_t pdm_capture_program_instructions[4] = { 0xb142, 0x5002, 0xa142, 0x4002 };
static const pio_program_t pdm_capture_program = {
    .instructions = pdm_capture_program_instructions, .length = 4, .origin = -1,
    .emu_model = "pdm_capture",
};
static inline pio_sm_config pdm_capture_program_get_default_config(uint offset) {
    (void)offset;
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_sideset(&c, 1, false, false);
    return c;
}
static inline void pdm_capture_program_init(PIO pio, uint sm, uint offset, uint clk_pin, uint in_base) {
    pio_sm_config c = pdm_capture_program_get_default_config(offset);
    sm_config_set_sideset_pins(&c, clk_pin);
    sm_config_set_in_pins(&c, in_base);
    sm_config_set_in_shift(&c, false, true, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    pio_gpio_init(pio, clk_pin);
    pio_gpio_init(pio, in_base);
    pio_gpio_init(pio, in_base + 1);
    float div = (float)clock_get_hz(clk_sys) / (float)(PDM_CLK_HZ * PDM_CAPTURE_CYCLES_PER_CLOCK);
    sm_config_set_clkdiv(&c, div);
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);
}
#ifdef __cplusplus
}
#endif
#endif
