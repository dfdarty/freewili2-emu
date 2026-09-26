/* hardware/pwm.h — host shim. PWM slices keep their settings (e.g. the audio
 * MCLK divider) but produce no waveform. */
#ifndef FW2EMU_HARDWARE_PWM_H
#define FW2EMU_HARDWARE_PWM_H
#include "pico.h"
#include "hardware/gpio.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint32_t csr, div, top; } pwm_config;
enum pwm_chan { PWM_CHAN_A = 0, PWM_CHAN_B = 1 };
static inline uint pwm_gpio_to_slice_num(uint gpio) { return (gpio >> 1u) & 11u; }
static inline uint pwm_gpio_to_channel(uint gpio) { return gpio & 1u; }
void pwm_set_wrap(uint slice, uint16_t wrap);
void pwm_set_gpio_level(uint gpio, uint16_t level);
void pwm_set_chan_level(uint slice, uint chan, uint16_t level);
void pwm_set_enabled(uint slice, bool enabled);
void pwm_set_clkdiv(uint slice, float div);
void pwm_set_clkdiv_int_frac(uint slice, uint8_t integer, uint8_t fract);
static inline pwm_config pwm_get_default_config(void) { pwm_config c = { 0, 1u << 4, 0xffff }; return c; }
static inline void pwm_config_set_wrap(pwm_config *c, uint16_t wrap) { c->top = wrap; }
static inline void pwm_config_set_clkdiv(pwm_config *c, float div) { c->div = (uint32_t)(div * 16.0f); }
static inline void pwm_config_set_clkdiv_int(pwm_config *c, uint div) { c->div = div << 4; }
static inline void pwm_init(uint slice, pwm_config *c, bool start) { pwm_set_wrap(slice, (uint16_t)c->top); pwm_set_enabled(slice, start); }
static inline void pwm_set_mask_enabled(uint32_t mask) { for (uint i = 0; i < 12; i++) pwm_set_enabled(i, (mask >> i) & 1u); }
uint16_t emu_pwm_wrap(uint slice);
bool emu_pwm_enabled(uint slice);
#ifdef __cplusplus
}
#endif
#endif
