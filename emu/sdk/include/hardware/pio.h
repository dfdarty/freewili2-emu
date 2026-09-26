/* hardware/pio.h — host shim.
 *
 * PIO programs are not interpreted. Instead each .pio program the BSP uses
 * gets a host "program model" header (see emu/include/<name>.pio.h) whose
 * *_program_init() binds the state machine to an emulated device — e.g. the
 * WS2812 LED chain receives every word pushed into the TX FIFO. */
#ifndef FW2EMU_HARDWARE_PIO_H
#define FW2EMU_HARDWARE_PIO_H
#include "pico.h"
#include "hardware/gpio.h"
#include "hardware/address_mapped.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    volatile uint32_t clkdiv, execctrl, shiftctrl, addr, instr, pinctrl;
} pio_sm_hw_t;
typedef struct pio_hw {
    volatile uint32_t ctrl, fstat, fdebug, flevel;
    volatile uint32_t txf[4];
    volatile uint32_t rxf[4];
    volatile uint32_t irq, irq_force;
    pio_sm_hw_t sm[4];
    /* emulator bookkeeping */
    unsigned index;
    uint8_t  sm_claimed;
    uint32_t instr_used;
} pio_hw_t;
typedef pio_hw_t *PIO;
extern pio_hw_t emu_pio_inst[3];
#define pio0 (&emu_pio_inst[0])
#define pio1 (&emu_pio_inst[1])
#define pio2 (&emu_pio_inst[2])
#define NUM_PIOS 3
#define NUM_PIO_STATE_MACHINES 4
typedef struct pio_program {
    const uint16_t *instructions;
    uint8_t length;
    int8_t  origin;
    uint8_t pio_version;
    uint8_t used_gpio_ranges;
    const char *emu_model;      /* host program model to bind (see emu/include/pio) */
} pio_program_t;

#define PIO_SM0_SHIFTCTRL_AUTOPUSH_BITS 0x00010000u
#define PIO_SM0_SHIFTCTRL_AUTOPULL_BITS 0x00020000u
#define PIO_SM0_SHIFTCTRL_FJOIN_TX_BITS 0x40000000u
#define PIO_SM0_SHIFTCTRL_FJOIN_RX_BITS 0x80000000u
typedef struct { uint32_t clkdiv, execctrl, shiftctrl, pinctrl; } pio_sm_config;
enum pio_fifo_join { PIO_FIFO_JOIN_NONE = 0, PIO_FIFO_JOIN_TX = 1, PIO_FIFO_JOIN_RX = 2 };
static inline uint pio_get_index(PIO pio) { return pio->index; }
static inline uint pio_get_dreq(PIO pio, uint sm, bool is_tx) { return (is_tx ? 0u : 8u) + pio->index * 16u + sm; }
bool pio_can_add_program(PIO pio, const pio_program_t *program);
int  pio_add_program(PIO pio, const pio_program_t *program);
int  pio_add_program_at_offset(PIO pio, const pio_program_t *program, uint offset);
void pio_remove_program(PIO pio, const pio_program_t *program, uint loaded_offset);
static inline void pio_clear_instruction_memory(PIO pio) { pio->instr_used = 0; }
int  pio_claim_unused_sm(PIO pio, bool required);
void pio_sm_claim(PIO pio, uint sm);
void pio_sm_unclaim(PIO pio, uint sm);
bool pio_sm_is_claimed(PIO pio, uint sm);
static inline void pio_gpio_init(PIO pio, uint pin) { gpio_set_function(pin, (gpio_function_t)(GPIO_FUNC_PIO0 + pio->index)); }
int  pio_sm_init(PIO pio, uint sm, uint initial_pc, const pio_sm_config *config);
void pio_sm_set_enabled(PIO pio, uint sm, bool enabled);
static inline void pio_set_sm_mask_enabled(PIO pio, uint32_t mask, bool en) { for (uint i = 0; i < 4; i++) if (mask & (1u << i)) pio_sm_set_enabled(pio, i, en); }
void pio_sm_put(PIO pio, uint sm, uint32_t data);
void pio_sm_put_blocking(PIO pio, uint sm, uint32_t data);
static inline uint32_t pio_sm_get(PIO pio, uint sm) { (void)pio; (void)sm; return 0; }
static inline uint32_t pio_sm_get_blocking(PIO pio, uint sm) { (void)pio; (void)sm; return 0; }
bool pio_sm_is_tx_fifo_empty(PIO pio, uint sm);
bool pio_sm_is_tx_fifo_full(PIO pio, uint sm);
static inline bool pio_sm_is_rx_fifo_empty(PIO pio, uint sm) { (void)pio; (void)sm; return true; }
static inline bool pio_sm_is_rx_fifo_full(PIO pio, uint sm) { (void)pio; (void)sm; return false; }
uint pio_sm_get_tx_fifo_level(PIO pio, uint sm);
static inline uint pio_sm_get_rx_fifo_level(PIO pio, uint sm) { (void)pio; (void)sm; return 0; }
void pio_sm_clear_fifos(PIO pio, uint sm);
static inline void pio_sm_drain_tx_fifo(PIO pio, uint sm) { pio_sm_clear_fifos(pio, sm); }
void pio_sm_restart(PIO pio, uint sm);
static inline void pio_sm_clkdiv_restart(PIO pio, uint sm) { (void)pio; (void)sm; }
static inline void pio_sm_exec(PIO pio, uint sm, uint instr) { (void)pio; (void)sm; (void)instr; }
static inline void pio_sm_exec_wait_blocking(PIO pio, uint sm, uint instr) { (void)pio; (void)sm; (void)instr; }
static inline void pio_sm_set_clkdiv_int_frac(PIO pio, uint sm, uint16_t i, uint8_t f) { pio->sm[sm].clkdiv = ((uint32_t)i << 16) | ((uint32_t)f << 8); }
static inline void pio_sm_set_clkdiv(PIO pio, uint sm, float div) { uint16_t i = (uint16_t)div; pio_sm_set_clkdiv_int_frac(pio, sm, i, (uint8_t)((div - i) * 256.0f)); }
static inline int  pio_sm_set_consecutive_pindirs(PIO pio, uint sm, uint base, uint count, bool out) { (void)pio; (void)sm; (void)base; (void)count; (void)out; return 0; }
static inline void pio_sm_set_pins_with_mask(PIO pio, uint sm, uint32_t v, uint32_t m) { (void)pio; (void)sm; (void)v; (void)m; }
static inline void pio_sm_set_pindirs_with_mask(PIO pio, uint sm, uint32_t v, uint32_t m) { (void)pio; (void)sm; (void)v; (void)m; }
static inline pio_sm_config pio_get_default_sm_config(void) { pio_sm_config c = { 1u << 16, 0, 0, 0 }; return c; }
static inline void sm_config_set_out_pins(pio_sm_config *c, uint base, uint count) { (void)c; (void)base; (void)count; }
static inline void sm_config_set_in_pins(pio_sm_config *c, uint base) { (void)c; (void)base; }
static inline void sm_config_set_set_pins(pio_sm_config *c, uint base, uint count) { (void)c; (void)base; (void)count; }
static inline void sm_config_set_sideset_pins(pio_sm_config *c, uint base) { (void)c; (void)base; }
static inline void sm_config_set_sideset(pio_sm_config *c, uint bits, bool opt, bool pindirs) { (void)c; (void)bits; (void)opt; (void)pindirs; }
static inline void sm_config_set_jmp_pin(pio_sm_config *c, uint pin) { (void)c; (void)pin; }
static inline void sm_config_set_wrap(pio_sm_config *c, uint wt, uint w) { (void)c; (void)wt; (void)w; }
static inline void sm_config_set_clkdiv(pio_sm_config *c, float d) { uint16_t i = (uint16_t)d; c->clkdiv = ((uint32_t)i << 16) | ((uint32_t)((d - i) * 256.0f) << 8); }
static inline void sm_config_set_clkdiv_int_frac(pio_sm_config *c, uint16_t i, uint8_t f) { c->clkdiv = ((uint32_t)i << 16) | ((uint32_t)f << 8); }
static inline void sm_config_set_out_shift(pio_sm_config *c, bool r, bool a, uint t) { (void)r; (void)t; c->shiftctrl = a ? (c->shiftctrl | PIO_SM0_SHIFTCTRL_AUTOPULL_BITS) : (c->shiftctrl & ~PIO_SM0_SHIFTCTRL_AUTOPULL_BITS); }
static inline void sm_config_set_in_shift(pio_sm_config *c, bool r, bool a, uint t) { (void)r; (void)t; c->shiftctrl = a ? (c->shiftctrl | PIO_SM0_SHIFTCTRL_AUTOPUSH_BITS) : (c->shiftctrl & ~PIO_SM0_SHIFTCTRL_AUTOPUSH_BITS); }
static inline void sm_config_set_fifo_join(pio_sm_config *c, enum pio_fifo_join j) { (void)c; (void)j; }
static inline void sm_config_set_out_special(pio_sm_config *c, bool s, bool e, uint i) { (void)c; (void)s; (void)e; (void)i; }
static inline void sm_config_set_mov_status(pio_sm_config *c, uint s, uint n) { (void)c; (void)s; (void)n; }
#ifdef __cplusplus
}
#endif
#endif
