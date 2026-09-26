/* hardware/psram.h — host shim for SDK 2.3.0's hardware_psram. The emulator
 * maps 8 MB of host memory at PSRAM_BASE (0x11000000) where the host allows. */
#ifndef FW2EMU_HARDWARE_PSRAM_H
#define FW2EMU_HARDWARE_PSRAM_H
#include "pico.h"
#ifndef PICO_DEFAULT_PSRAM_MAX_FREQ
#define PICO_DEFAULT_PSRAM_MAX_FREQ 109000000u
#endif
#ifndef PICO_DEFAULT_PSRAM_MAX_SELECT
#define PICO_DEFAULT_PSRAM_MAX_SELECT 8000u
#endif
#ifndef PICO_DEFAULT_PSRAM_MIN_DESELECT
#define PICO_DEFAULT_PSRAM_MIN_DESELECT 50u
#endif
bool   psram_is_available(void);
size_t psram_get_size(void);
static inline void psram_configure_params(uint32_t f, uint32_t sel, uint32_t desel) { (void)f; (void)sel; (void)desel; }
static inline void psram_reinitialize(void) {}
#endif
