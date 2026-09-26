/* i2s_duplex.pio.h — host program model for bsp/audio/i2s_duplex.pio.
 *
 * On hardware pioasm generates this header. Here pio_sm_init() sees
 * .emu_model and binds the state machine to the emulated I2S bus
 * (emu/src/dev_audio.c): one 32-bit [L16|R16] frame per LRCK period, TX
 * words go to the NAU88C10 DAC model, RX words carry its ADC. The program
 * runs 4 PIO cycles per bit, 128 per frame, so fs = clk_sys / clkdiv / 128
 * exactly as on hardware. */
#ifndef FW2EMU_I2S_DUPLEX_PIO_H
#define FW2EMU_I2S_DUPLEX_PIO_H
#include "hardware/pio.h"
#ifdef __cplusplus
extern "C" {
#endif
#define i2s_duplex_wrap_target 0
#define i2s_duplex_wrap 11
#define I2S_DUPLEX_CYCLES_PER_FRAME 128u
static const uint16_t i2s_duplex_program_instructions[12] = {
    0xe02e, 0x6101, 0x5001, 0x1041, 0x6101, 0x5001, 0xe82e, 0x6901, 0x5801, 0x1847, 0x6901, 0x5801,
};
static const pio_program_t i2s_duplex_program = {
    .instructions = i2s_duplex_program_instructions, .length = 12, .origin = -1,
    .emu_model = "i2s_duplex",
};
static inline pio_sm_config i2s_duplex_program_get_default_config(uint offset) {
    (void)offset;
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_sideset(&c, 2, false, false);
    return c;
}
#ifdef __cplusplus
}
#endif
#endif
