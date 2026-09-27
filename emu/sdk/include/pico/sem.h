/* pico/sem.h — host shim. */
#ifndef FW2EMU_PICO_SEM_H
#define FW2EMU_PICO_SEM_H
#include "pico/lock_core.h"
#include "pico/time.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct semaphore {
    struct lock_core core;
    int16_t permits;
    int16_t max_permits;
} semaphore_t;
void sem_init(semaphore_t *sem, int16_t initial_permits, int16_t max_permits);
int  sem_available(semaphore_t *sem);
bool sem_release(semaphore_t *sem);
void sem_reset(semaphore_t *sem, int16_t permits);
void sem_acquire_blocking(semaphore_t *sem);
bool sem_acquire_timeout_ms(semaphore_t *sem, uint32_t timeout_ms);
bool sem_acquire_timeout_us(semaphore_t *sem, uint32_t timeout_us);
bool sem_acquire_block_until(semaphore_t *sem, absolute_time_t until);
bool sem_try_acquire(semaphore_t *sem);
#ifdef __cplusplus
}
#endif
#endif
