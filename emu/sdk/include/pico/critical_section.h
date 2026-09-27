/* pico/critical_section.h — host shim: a spin lock plus the calling core's
 * interrupts off, as in the SDK. */
#ifndef FW2EMU_PICO_CRITICAL_SECTION_H
#define FW2EMU_PICO_CRITICAL_SECTION_H
#include "pico/lock_core.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct critical_section {
    spin_lock_t *spin_lock;
    uint32_t save;
} critical_section_t;
void critical_section_init(critical_section_t *crit_sec);
void critical_section_init_with_lock_num(critical_section_t *crit_sec, uint lock_num);
static inline void critical_section_enter_blocking(critical_section_t *crit_sec) {
    crit_sec->save = spin_lock_blocking(crit_sec->spin_lock);
}
static inline void critical_section_exit(critical_section_t *crit_sec) {
    spin_unlock(crit_sec->spin_lock, crit_sec->save);
}
void critical_section_deinit(critical_section_t *crit_sec);
static inline bool critical_section_is_initialized(critical_section_t *crit_sec) { return crit_sec->spin_lock != 0; }
#ifdef __cplusplus
}
#endif
#endif
