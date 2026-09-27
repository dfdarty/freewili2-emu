/* pico/util/queue.h — host shim: the SDK's multi-core safe fixed-size queue. */
#ifndef FW2EMU_PICO_UTIL_QUEUE_H
#define FW2EMU_PICO_UTIL_QUEUE_H
#include "pico/lock_core.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    lock_core_t core;
    uint8_t *data;
    uint16_t wptr;
    uint16_t rptr;
    uint16_t element_size;
    uint16_t element_count;
    uint16_t max_level;
} queue_t;
bool queue_init_with_spinlock(queue_t *q, uint element_size, uint element_count, uint spinlock_num);
static inline bool queue_init(queue_t *q, uint element_size, uint element_count) {
    return queue_init_with_spinlock(q, element_size, element_count, next_striped_spin_lock_num());
}
void queue_free(queue_t *q);
static inline uint queue_get_level_unsafe(queue_t *q) {
    int32_t rc = (int32_t)q->wptr - (int32_t)q->rptr;
    if (rc < 0) rc += q->element_count + 1;
    return (uint)rc;
}
static inline uint queue_get_level(queue_t *q) {
    uint32_t save = spin_lock_blocking(q->core.spin_lock);
    uint level = queue_get_level_unsafe(q);
    spin_unlock(q->core.spin_lock, save);
    return level;
}
static inline uint queue_get_max_level(queue_t *q) { return q->max_level; }
static inline void queue_reset_max_level(queue_t *q) { q->max_level = 0; }
static inline bool queue_is_empty(queue_t *q) { return queue_get_level(q) == 0; }
static inline bool queue_is_full(queue_t *q) { return queue_get_level(q) == q->element_count; }
bool queue_try_add(queue_t *q, const void *data);
bool queue_try_remove(queue_t *q, void *data);
bool queue_try_peek(queue_t *q, void *data);
void queue_add_blocking(queue_t *q, const void *data);
void queue_remove_blocking(queue_t *q, void *data);
void queue_peek_blocking(queue_t *q, void *data);
#ifdef __cplusplus
}
#endif
#endif
