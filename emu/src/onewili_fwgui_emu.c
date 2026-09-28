/* onewili_fwgui_emu.c — OneWili's FwGUI transport (libs/onewili/wilibsp/
 * src/onewili_fwgui.c, unmodified) as the emulator builds it.
 *
 * The transport's UART interrupt handler pops received bytes by reading the
 * PL011's DR register in a loop; a register in host memory can't pop, so the
 * emulator uses the file's documented host mode, OWFW_NO_IRQ, which drains
 * the same ring with uart_is_readable()/uart_getc() instead.
 *
 * Host mode also swaps the peer-stream clock for g_owfw_host_ms, a static
 * that OneWili's own host tests step by hand. Left alone it stays 0, and a
 * stream link never sends its keepalive HELLO, so MAIN closes it after 3 s.
 * Here it reads the board's millisecond clock, as the device build's
 * owfw_now_ms() does. (tests/apps/stream_check fails if this stops working.) */
#include <stdint.h>
#include "pico/time.h"

#define OWFW_NO_IRQ 1
static uint32_t emu_owfw_now_ms(void);
#define g_owfw_host_ms emu_owfw_now_ms()
#include "onewili_fwgui.c"
#undef g_owfw_host_ms

static uint32_t emu_owfw_now_ms(void) { return to_ms_since_boot(get_absolute_time()); }
