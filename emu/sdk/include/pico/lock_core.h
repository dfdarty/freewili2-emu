/* pico/lock_core.h — host shim (see pico/mutex.h). */
#ifndef FW2EMU_PICO_LOCK_CORE_H
#define FW2EMU_PICO_LOCK_CORE_H
#include "pico.h"
#include "hardware/sync.h"
#ifdef __cplusplus
extern "C" {
#endif
struct lock_core { spin_lock_t *spin_lock; };
typedef struct lock_core lock_core_t;
typedef int8_t lock_owner_id_t;
#define LOCK_INVALID_OWNER_ID ((lock_owner_id_t)-1)
#define lock_get_caller_owner_id() ((lock_owner_id_t)get_core_num())
void lock_init(lock_core_t *core, uint lock_num);
#ifdef __cplusplus
}
#endif
#endif
