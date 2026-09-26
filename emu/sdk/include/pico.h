/* pico.h — host shim for the Raspberry Pi Pico SDK core header.
 *
 * freewili2-emu compiles unmodified WiliBSP drivers against this shim. It
 * provides the SDK types, attributes and helpers those drivers use, and
 * pulls in the FREE-WILi 2 board header exactly like the real SDK does.
 */
#ifndef FW2EMU_PICO_H
#define FW2EMU_PICO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

typedef unsigned int uint;

#ifndef PICO_BUILD
#define PICO_BUILD 1      /* the SDK defines this for every target build */
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Board header uses these CMake-visible markers; they are no-ops in C. */
#define pico_board_cmake_set(key, value)
#define pico_board_cmake_set_default(key, value)
#define pico_cmake_set(key, value)
#define pico_cmake_set_default(key, value)

#include "boards/freewili2.h"

#ifndef NUM_BANK0_GPIOS
#define NUM_BANK0_GPIOS 48
#endif

/* ---- attributes / section placement (all normal memory on the host) ---- */
#define __not_in_flash(group)
#define __not_in_flash_func(func_name) func_name
#define __no_inline_not_in_flash_func(func_name) __attribute__((noinline)) func_name
#define __time_critical_func(func_name) func_name
#define __in_flash(group)
#define __scratch_x(group)
#define __scratch_y(group)
#define __uninitialized_ram(name) name
#define __uninitialized_psram(name)
#define __psram_load(name)
#ifndef __aligned
#define __aligned(x) __attribute__((aligned(x)))
#endif
#ifndef __packed
#define __packed __attribute__((packed))
#endif
#ifndef __unused
#define __unused __attribute__((unused))
#endif
#ifndef __used
#define __used __attribute__((used))
#endif
#define __force_inline inline __attribute__((always_inline))
#define __noinline __attribute__((noinline))
#define __isr

#ifndef count_of
#define count_of(a) (sizeof(a) / sizeof((a)[0]))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif

enum pico_error_codes {
    PICO_OK = 0,
    PICO_ERROR_NONE = 0,
    PICO_ERROR_TIMEOUT = -1,
    PICO_ERROR_GENERIC = -2,
    PICO_ERROR_NO_DATA = -3,
    PICO_ERROR_NOT_PERMITTED = -4,
    PICO_ERROR_INVALID_ARG = -5,
    PICO_ERROR_IO = -6,
    PICO_ERROR_BADAUTH = -7,
    PICO_ERROR_CONNECT_FAILED = -8,
    PICO_ERROR_INSUFFICIENT_RESOURCES = -9,
};

/* Busy-wait hint. On the host this also services the emulator (display,
 * input, coprocessor model) so spin loops stay responsive. */
void tight_loop_contents(void);

void panic(const char *fmt, ...) __attribute__((noreturn));
void panic_unsupported(void) __attribute__((noreturn));
#define hard_assert(x) do { if (!(x)) panic("hard_assert failed: %s", #x); } while (0)
#define invalid_params_if(x, test) do { if (test) panic("invalid params: " #test); } while (0)
#define valid_params_if(x, test) ((void)0)

static inline uint get_core_num(void) { return 0; }

#ifdef __cplusplus
}
#endif

#endif
