/* pico/multicore.h — host shim: core 1, the inter-core FIFOs, lockout and
 * doorbells (RP2350).
 *
 * The two cores run in turn on one host thread and switch whenever a core
 * waits (sleep, tight_loop_contents, a blocking FIFO / lock / mutex call) or
 * has run for 1 ms and calls into the SDK. A loop that spins on a plain
 * variable with no SDK call in it never lets the other core run: add
 * tight_loop_contents() to the loop (harmless on the board). */
#ifndef FW2EMU_PICO_MULTICORE_H
#define FW2EMU_PICO_MULTICORE_H
#include "pico.h"
#include "pico/time.h"
#include "hardware/sync.h"
#ifdef __cplusplus
extern "C" {
#endif
#define SIO_IRQ_FIFO 25
#define SIO_IRQ_BELL 26
#define SIO_FIFO_IRQ_NUM(core) SIO_IRQ_FIFO
#define SIO_FIFO_ST_VLD_BITS 0x00000001u
#define SIO_FIFO_ST_RDY_BITS 0x00000002u
#define SIO_FIFO_ST_WOF_BITS 0x00000004u
#define SIO_FIFO_ST_ROE_BITS 0x00000008u
#define NUM_DOORBELLS 8
#ifndef PICO_CORE1_STACK_SIZE
#define PICO_CORE1_STACK_SIZE 0x800
#endif

void multicore_reset_core1(void);
void multicore_launch_core1(void (*entry)(void));
void multicore_launch_core1_with_stack(void (*entry)(void), uint32_t *stack_bottom, size_t stack_size_bytes);
void multicore_launch_core1_raw(void (*entry)(void), uint32_t *sp, uint32_t vector_table);

bool     multicore_fifo_rvalid(void);
bool     multicore_fifo_wready(void);
void     multicore_fifo_push_blocking(uint32_t data);
bool     multicore_fifo_push_timeout_us(uint32_t data, uint64_t timeout_us);
uint32_t multicore_fifo_pop_blocking(void);
bool     multicore_fifo_pop_timeout_us(uint64_t timeout_us, uint32_t *out);
void     multicore_fifo_drain(void);
void     multicore_fifo_clear_irq(void);
uint32_t multicore_fifo_get_status(void);
static inline void multicore_fifo_push_blocking_inline(uint32_t data) { multicore_fifo_push_blocking(data); }
static inline uint32_t multicore_fifo_pop_blocking_inline(void) { return multicore_fifo_pop_blocking(); }

void multicore_lockout_victim_init(void);
void multicore_lockout_victim_deinit(void);
bool multicore_lockout_victim_is_initialized(uint core_num);
void multicore_lockout_start_blocking(void);
bool multicore_lockout_start_timeout_us(uint64_t timeout_us);
void multicore_lockout_end_blocking(void);
bool multicore_lockout_end_timeout_us(uint64_t timeout_us);

void multicore_doorbell_claim(uint doorbell_num, uint core_mask);
int  multicore_doorbell_claim_unused(uint core_mask, bool required);
void multicore_doorbell_unclaim(uint doorbell_num, uint core_mask);
void multicore_doorbell_set_other_core(uint doorbell_num);
void multicore_doorbell_clear_other_core(uint doorbell_num);
void multicore_doorbell_set_current_core(uint doorbell_num);
void multicore_doorbell_clear_current_core(uint doorbell_num);
bool multicore_doorbell_is_set_current_core(uint doorbell_num);
bool multicore_doorbell_is_set_other_core(uint doorbell_num);
static inline uint multicore_doorbell_irq_num(uint doorbell_num) { (void)doorbell_num; return SIO_IRQ_BELL; }
#ifdef __cplusplus
}
#endif
#endif
