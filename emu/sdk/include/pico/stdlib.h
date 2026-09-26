/* pico/stdlib.h — host shim. Mirrors what the SDK header pulls in. */
#ifndef FW2EMU_PICO_STDLIB_H
#define FW2EMU_PICO_STDLIB_H
#include <stdio.h>
#include "pico.h"
#include "pico/time.h"
#include "hardware/gpio.h"
#include "hardware/uart.h"
#include "hardware/sync.h"
#ifdef __cplusplus
extern "C" {
#endif
static inline bool stdio_init_all(void) { return true; }
static inline void setup_default_uart(void) {}
#ifdef __cplusplus
}
#endif
#endif
