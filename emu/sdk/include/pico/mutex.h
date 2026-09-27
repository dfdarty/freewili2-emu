/* pico/mutex.h — host shim. Owner ids are core numbers, as in the SDK
 * without an RTOS. A core that blocks on a mutex lets the other core run;
 * one that blocks on a (non-recursive) mutex it already owns hangs, as it
 * would on the board, and the emulator says so once. */
#ifndef FW2EMU_PICO_MUTEX_H
#define FW2EMU_PICO_MUTEX_H
#include "pico/lock_core.h"
#include "pico/time.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    lock_core_t core;
    lock_owner_id_t owner;
    uint8_t enter_count;
} recursive_mutex_t;
typedef struct mutex {
    lock_core_t core;
    lock_owner_id_t owner;
} mutex_t;

void mutex_init(mutex_t *mtx);
void recursive_mutex_init(recursive_mutex_t *mtx);
void mutex_enter_blocking(mutex_t *mtx);
void recursive_mutex_enter_blocking(recursive_mutex_t *mtx);
bool mutex_try_enter(mutex_t *mtx, uint32_t *owner_out);
bool mutex_try_enter_block_until(mutex_t *mtx, absolute_time_t until);
bool recursive_mutex_try_enter(recursive_mutex_t *mtx, uint32_t *owner_out);
bool mutex_enter_timeout_ms(mutex_t *mtx, uint32_t timeout_ms);
bool recursive_mutex_enter_timeout_ms(recursive_mutex_t *mtx, uint32_t timeout_ms);
bool mutex_enter_timeout_us(mutex_t *mtx, uint32_t timeout_us);
bool recursive_mutex_enter_timeout_us(recursive_mutex_t *mtx, uint32_t timeout_us);
bool mutex_enter_block_until(mutex_t *mtx, absolute_time_t until);
bool recursive_mutex_enter_block_until(recursive_mutex_t *mtx, absolute_time_t until);
void mutex_exit(mutex_t *mtx);
void recursive_mutex_exit(recursive_mutex_t *mtx);
static inline bool mutex_is_initialized(mutex_t *mtx) { return mtx->core.spin_lock != 0; }
static inline bool recursive_mutex_is_initialized(recursive_mutex_t *mtx) { return mtx->core.spin_lock != 0; }

/* The SDK initialises these before main(); here the first use does. */
#define EMU_LOCK_AUTO ((spin_lock_t *)1)
#define auto_init_mutex(name) static mutex_t name = { .core = { .spin_lock = EMU_LOCK_AUTO }, .owner = LOCK_INVALID_OWNER_ID }
#define auto_init_recursive_mutex(name) static recursive_mutex_t name = { .core = { .spin_lock = EMU_LOCK_AUTO }, .owner = LOCK_INVALID_OWNER_ID, .enter_count = 0 }
#ifdef __cplusplus
}
#endif
#endif
