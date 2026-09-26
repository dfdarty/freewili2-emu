#ifndef FW2EMU_HARDWARE_SYNC_H
#define FW2EMU_HARDWARE_SYNC_H
#include "pico.h"
#ifdef __cplusplus
extern "C" {
#endif
void emu_poll(void);
static inline void __wfi(void) { tight_loop_contents(); }
static inline void __wfe(void) { tight_loop_contents(); }
static inline void __sev(void) {}
static inline void __sev_all(void) {}
static inline void __dmb(void) { __sync_synchronize(); }
static inline void __dsb(void) { __sync_synchronize(); }
static inline void __isb(void) { __sync_synchronize(); }
static inline void __mem_fence_acquire(void) { __sync_synchronize(); }
static inline void __mem_fence_release(void) { __sync_synchronize(); }
static inline void __compiler_memory_barrier(void) { __asm__ volatile("" ::: "memory"); }
static inline uint32_t save_and_disable_interrupts(void) { return 0; }
static inline void restore_interrupts(uint32_t s) { (void)s; }
static inline void restore_interrupts_from_disabled(uint32_t s) { (void)s; }
#ifdef __cplusplus
}
#endif
#endif
