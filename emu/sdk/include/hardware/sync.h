/* hardware/sync.h — host shim: barriers, interrupt masking and spin locks.
 *
 * Interrupt masking is per core and real: while a core has interrupts
 * disabled, IRQs raised for it are held pending and run when it restores
 * them, as with PRIMASK on the chip. Spin locks are the SIO's 32, with the
 * RP2350's lock-number layout (software spin locks, the SDK default because
 * of erratum RP2350-E2). A core waiting on a lock lets the other core run.
 */
#ifndef FW2EMU_HARDWARE_SYNC_H
#define FW2EMU_HARDWARE_SYNC_H
#include "pico.h"
#ifdef __cplusplus
extern "C" {
#endif
void emu_poll(void);

static inline void __nop(void) {}
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

/* PRIMASK of the calling core: 1 = interrupts disabled. */
uint32_t save_and_disable_interrupts(void);
void     restore_interrupts(uint32_t status);
static inline void restore_interrupts_from_disabled(uint32_t status) { restore_interrupts(status); }
static inline void disable_interrupts(void) { (void)save_and_disable_interrupts(); }
static inline void enable_interrupts(void) { restore_interrupts(0); }

/* ---- spin locks (the SDK's hardware/sync/spin_lock.h) ---- */
typedef volatile uint32_t spin_lock_t;

#define PICO_SPINLOCK_ID_IRQ 9
#define PICO_SPINLOCK_ID_TIMER 10
#define PICO_SPINLOCK_ID_HARDWARE_CLAIM 11
#define PICO_SPINLOCK_ID_RAND 12
#define PICO_SPINLOCK_ID_ATOMIC 13
#define PICO_SPINLOCK_ID_OS1 14
#define PICO_SPINLOCK_ID_OS2 15
#define PICO_SPINLOCK_ID_STRIPED_FIRST 16
#define PICO_SPINLOCK_ID_STRIPED_LAST 23
#define PICO_SPINLOCK_ID_CLAIM_FREE_FIRST 24
#define PICO_SPINLOCK_ID_CLAIM_FREE_LAST 31
#define NUM_SPIN_LOCKS 32

extern spin_lock_t emu_spin_locks[NUM_SPIN_LOCKS];

static inline spin_lock_t *spin_lock_instance(uint lock_num) { return &emu_spin_locks[lock_num]; }
static inline uint spin_lock_get_num(spin_lock_t *lock) { return (uint)(lock - emu_spin_locks); }
static inline bool spin_try_lock_unsafe(spin_lock_t *lock) {
    if (*lock) return false;
    *lock = 1u;                    /* one host thread: nothing runs between the test and the set */
    __sync_synchronize();
    return true;
}
static inline void spin_lock_unsafe_blocking(spin_lock_t *lock) {
    while (!spin_try_lock_unsafe(lock)) tight_loop_contents();   /* lets the holder run */
}
static inline void spin_unlock_unsafe(spin_lock_t *lock) { __sync_synchronize(); *lock = 0u; }
static inline uint32_t spin_lock_blocking(spin_lock_t *lock) {
    uint32_t save = save_and_disable_interrupts();
    spin_lock_unsafe_blocking(lock);
    return save;
}
static inline void spin_unlock(spin_lock_t *lock, uint32_t saved_irq) {
    spin_unlock_unsafe(lock);
    restore_interrupts_from_disabled(saved_irq);
}
static inline bool is_spin_locked(spin_lock_t *lock) { return *lock != 0u; }

spin_lock_t *spin_lock_init(uint lock_num);
void spin_locks_reset(void);
uint next_striped_spin_lock_num(void);
void spin_lock_claim(uint lock_num);
void spin_lock_claim_mask(uint32_t lock_num_mask);
void spin_lock_unclaim(uint lock_num);
int  spin_lock_claim_unused(bool required);
bool spin_lock_is_claimed(uint lock_num);

#ifdef __cplusplus
}
#endif
#endif
