/* hardware/gpio.h — host shim. Pin state lives in the emulator; device
 * models watch output pins (chip selects, D/C, backlight) and drive inputs. */
#ifndef FW2EMU_HARDWARE_GPIO_H
#define FW2EMU_HARDWARE_GPIO_H

#include "pico.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum gpio_function_rp2350 {
    GPIO_FUNC_HSTX = 0,
    GPIO_FUNC_SPI = 1,
    GPIO_FUNC_UART = 2,
    GPIO_FUNC_I2C = 3,
    GPIO_FUNC_PWM = 4,
    GPIO_FUNC_SIO = 5,
    GPIO_FUNC_PIO0 = 6,
    GPIO_FUNC_PIO1 = 7,
    GPIO_FUNC_PIO2 = 8,
    GPIO_FUNC_GPCK = 9,
    GPIO_FUNC_XIP_CS1 = 9,
    GPIO_FUNC_CORESIGHT_TRACE = 9,
    GPIO_FUNC_USB = 10,
    GPIO_FUNC_UART_AUX = 11,
    GPIO_FUNC_NULL = 0x1f,
} gpio_function_t;

#define GPIO_OUT 1
#define GPIO_IN  0

enum gpio_irq_level {
    GPIO_IRQ_LEVEL_LOW = 0x1u,
    GPIO_IRQ_LEVEL_HIGH = 0x2u,
    GPIO_IRQ_EDGE_FALL = 0x4u,
    GPIO_IRQ_EDGE_RISE = 0x8u,
};

enum gpio_override {
    GPIO_OVERRIDE_NORMAL = 0,
    GPIO_OVERRIDE_INVERT = 1,
    GPIO_OVERRIDE_LOW = 2,
    GPIO_OVERRIDE_HIGH = 3,
};

enum gpio_slew_rate { GPIO_SLEW_RATE_SLOW = 0, GPIO_SLEW_RATE_FAST = 1 };

enum gpio_drive_strength {
    GPIO_DRIVE_STRENGTH_2MA = 0,
    GPIO_DRIVE_STRENGTH_4MA = 1,
    GPIO_DRIVE_STRENGTH_8MA = 2,
    GPIO_DRIVE_STRENGTH_12MA = 3,
};

typedef void (*gpio_irq_callback_t)(uint gpio, uint32_t event_mask);

void gpio_init(uint gpio);
void gpio_deinit(uint gpio);
void gpio_init_mask(uint64_t mask);
void gpio_set_function(uint gpio, gpio_function_t fn);
gpio_function_t gpio_get_function(uint gpio);
void gpio_set_dir(uint gpio, bool out);
bool gpio_is_dir_out(uint gpio);
void gpio_put(uint gpio, bool value);
bool gpio_get(uint gpio);
bool gpio_get_out_level(uint gpio);
uint64_t gpio_get_all64(void);
static inline uint32_t gpio_get_all(void) { return (uint32_t)gpio_get_all64(); }
void gpio_set_mask64(uint64_t mask);
void gpio_clr_mask64(uint64_t mask);
void gpio_put_masked64(uint64_t mask, uint64_t value);
void gpio_set_dir_masked64(uint64_t mask, uint64_t value);
static inline void gpio_set_mask(uint32_t m) { gpio_set_mask64(m); }
static inline void gpio_clr_mask(uint32_t m) { gpio_clr_mask64(m); }
static inline void gpio_put_masked(uint32_t m, uint32_t v) { gpio_put_masked64(m, v); }
static inline void gpio_set_dir_masked(uint32_t m, uint32_t v) { gpio_set_dir_masked64(m, v); }
static inline void gpio_set_dir_out_masked(uint32_t m) { gpio_set_dir_masked64(m, m); }
static inline void gpio_set_dir_in_masked(uint32_t m) { gpio_set_dir_masked64(m, 0); }
static inline void gpio_xor_mask(uint32_t m) {
    for (uint i = 0; i < 32; i++) if (m & (1u << i)) gpio_put(i, !gpio_get_out_level(i));
}

void gpio_set_pulls(uint gpio, bool up, bool down);
static inline void gpio_pull_up(uint gpio) { gpio_set_pulls(gpio, true, false); }
static inline void gpio_pull_down(uint gpio) { gpio_set_pulls(gpio, false, true); }
static inline void gpio_disable_pulls(uint gpio) { gpio_set_pulls(gpio, false, false); }
bool gpio_is_pulled_up(uint gpio);
bool gpio_is_pulled_down(uint gpio);

void gpio_set_outover(uint gpio, uint value);
void gpio_set_inover(uint gpio, uint value);
void gpio_set_oeover(uint gpio, uint value);
static inline void gpio_set_input_enabled(uint gpio, bool en) { (void)gpio; (void)en; }
static inline void gpio_set_input_hysteresis_enabled(uint gpio, bool en) { (void)gpio; (void)en; }
static inline void gpio_set_slew_rate(uint gpio, enum gpio_slew_rate s) { (void)gpio; (void)s; }
static inline void gpio_set_drive_strength(uint gpio, enum gpio_drive_strength d) { (void)gpio; (void)d; }

static inline void gpio_set_irq_enabled(uint gpio, uint32_t events, bool en) { (void)gpio; (void)events; (void)en; }
static inline void gpio_set_irq_enabled_with_callback(uint gpio, uint32_t events, bool en, gpio_irq_callback_t cb) {
    (void)gpio; (void)events; (void)en; (void)cb;
}
static inline void gpio_acknowledge_irq(uint gpio, uint32_t events) { (void)gpio; (void)events; }

#ifdef __cplusplus
}
#endif

#endif
