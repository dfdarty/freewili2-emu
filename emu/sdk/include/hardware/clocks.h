#ifndef FW2EMU_HARDWARE_CLOCKS_H
#define FW2EMU_HARDWARE_CLOCKS_H
#include "pico.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum clock_num_rp2350 {
    clk_gpout0 = 0, clk_gpout1, clk_gpout2, clk_gpout3,
    clk_ref, clk_sys, clk_peri, clk_hstx, clk_usb, clk_adc,
    CLK_COUNT
} clock_num_t;
typedef clock_num_t clock_handle_t;
#define CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS 0u
#define CLOCKS_CLK_HSTX_CTRL_AUXSRC_VALUE_CLK_SYS 0u
#define CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX 1u
#define CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS 0u
#define KHZ 1000u
#define MHZ 1000000u
uint32_t clock_get_hz(clock_handle_t clock);
bool clock_configure(clock_handle_t clock, uint32_t src, uint32_t auxsrc, uint32_t src_freq, uint32_t freq);
bool clock_configure_undivided(clock_handle_t clock, uint32_t src, uint32_t auxsrc, uint32_t src_freq);
bool set_sys_clock_khz(uint32_t freq_khz, bool required);
static inline void clock_stop(clock_handle_t c) { (void)c; }
#ifdef __cplusplus
}
#endif
#endif
