/* sdk_sync.c — the SDK's spin-lock claiming, mutexes, semaphores, critical
 * sections and pico/util/queue, on the host.
 *
 * Both cores share one host thread (see multicore.c), so nothing runs
 * between a lock's test and its set; waiting is always through
 * tight_loop_contents() or emu_sleep_us(), which hand the thread to the
 * other core. The structures and owner ids are the SDK's. */
#include "emu/emu.h"
#include "pico.h"
#include "hardware/sync.h"
#include "pico/mutex.h"
#include "pico/sem.h"
#include "pico/critical_section.h"
#include "pico/util/queue.h"

#include <stdlib.h>
#include <string.h>

spin_lock_t emu_spin_locks[NUM_SPIN_LOCKS];
static uint32_t s_claimed;

spin_lock_t *spin_lock_init(uint n) {
    spin_lock_t *l = spin_lock_instance(n);
    spin_unlock_unsafe(l);
    return l;
}
void spin_locks_reset(void) { for (uint i = 0; i < NUM_SPIN_LOCKS; i++) spin_unlock_unsafe(&emu_spin_locks[i]); }
uint next_striped_spin_lock_num(void) {
    static uint next = PICO_SPINLOCK_ID_STRIPED_FIRST;
    uint n = next;
    next = next == PICO_SPINLOCK_ID_STRIPED_LAST ? PICO_SPINLOCK_ID_STRIPED_FIRST : next + 1;
    return n;
}
void spin_lock_claim(uint n) {
    if (s_claimed & (1u << n)) emu_fatal("spin lock %u already claimed", n);
    s_claimed |= 1u << n;
}
void spin_lock_claim_mask(uint32_t mask) { for (uint n = 0; n < NUM_SPIN_LOCKS; n++) if (mask & (1u << n)) spin_lock_claim(n); }
void spin_lock_unclaim(uint n) { spin_unlock_unsafe(&emu_spin_locks[n]); s_claimed &= ~(1u << n); }
int spin_lock_claim_unused(bool required) {
    for (uint n = PICO_SPINLOCK_ID_CLAIM_FREE_FIRST; n <= PICO_SPINLOCK_ID_CLAIM_FREE_LAST; n++)
        if (!(s_claimed & (1u << n))) { s_claimed |= 1u << n; return (int)n; }
    if (required) emu_fatal("no unclaimed spin locks left");
    return -1;
}
bool spin_lock_is_claimed(uint n) { return (s_claimed >> n) & 1u; }

void lock_init(lock_core_t *core, uint n) { core->spin_lock = spin_lock_init(n); }

/* auto_init_mutex() leaves a marker for the SDK's pre-main initialiser; the
 * first use here does that job. */
static void lock_ready(lock_core_t *core) {
    if (core->spin_lock == EMU_LOCK_AUTO || !core->spin_lock) lock_init(core, next_striped_spin_lock_num());
}

/* ------------------------------------------------------------------ mutex */
void mutex_init(mutex_t *m) { lock_init(&m->core, next_striped_spin_lock_num()); m->owner = LOCK_INVALID_OWNER_ID; }
void recursive_mutex_init(recursive_mutex_t *m) {
    lock_init(&m->core, next_striped_spin_lock_num());
    m->owner = LOCK_INVALID_OWNER_ID;
    m->enter_count = 0;
}

bool mutex_try_enter(mutex_t *m, uint32_t *owner_out) {
    lock_ready(&m->core);
    if (m->owner == LOCK_INVALID_OWNER_ID) { m->owner = lock_get_caller_owner_id(); return true; }
    if (owner_out) *owner_out = (uint32_t)(int32_t)m->owner;
    return false;
}

static void warn_self_deadlock(const void *m) {
    static const void *warned;
    if (warned == m) return;
    warned = m;
    emu_log("mutex %p: core %u is waiting for a mutex it already owns. On the board this waits "
            "forever (use a recursive_mutex_t to re-enter)", m, get_core_num());
}

bool mutex_enter_block_until(mutex_t *m, absolute_time_t until) {
    for (;;) {
        if (mutex_try_enter(m, NULL)) return true;
        if (m->owner == lock_get_caller_owner_id()) warn_self_deadlock(m);
        if (until != at_the_end_of_time && time_reached(until)) return false;
        tight_loop_contents();
    }
}
void mutex_enter_blocking(mutex_t *m) { mutex_enter_block_until(m, at_the_end_of_time); }
bool mutex_try_enter_block_until(mutex_t *m, absolute_time_t until) {
    if (mutex_try_enter(m, NULL)) return true;
    return mutex_enter_block_until(m, until);
}
bool mutex_enter_timeout_ms(mutex_t *m, uint32_t ms) { return mutex_enter_block_until(m, make_timeout_time_ms(ms)); }
bool mutex_enter_timeout_us(mutex_t *m, uint32_t us) { return mutex_enter_block_until(m, make_timeout_time_us(us)); }
void mutex_exit(mutex_t *m) {
    if (m->owner == LOCK_INVALID_OWNER_ID) emu_log("mutex_exit on a mutex nobody owns");
    m->owner = LOCK_INVALID_OWNER_ID;
}

bool recursive_mutex_try_enter(recursive_mutex_t *m, uint32_t *owner_out) {
    lock_ready(&m->core);
    lock_owner_id_t me = lock_get_caller_owner_id();
    if (m->owner == LOCK_INVALID_OWNER_ID || m->owner == me) {
        if (m->enter_count == 0xff) emu_fatal("recursive mutex entered 255 times");
        m->owner = me;
        m->enter_count++;
        return true;
    }
    if (owner_out) *owner_out = (uint32_t)(int32_t)m->owner;
    return false;
}
bool recursive_mutex_enter_block_until(recursive_mutex_t *m, absolute_time_t until) {
    for (;;) {
        if (recursive_mutex_try_enter(m, NULL)) return true;
        if (until != at_the_end_of_time && time_reached(until)) return false;
        tight_loop_contents();
    }
}
void recursive_mutex_enter_blocking(recursive_mutex_t *m) { recursive_mutex_enter_block_until(m, at_the_end_of_time); }
bool recursive_mutex_enter_timeout_ms(recursive_mutex_t *m, uint32_t ms) { return recursive_mutex_enter_block_until(m, make_timeout_time_ms(ms)); }
bool recursive_mutex_enter_timeout_us(recursive_mutex_t *m, uint32_t us) { return recursive_mutex_enter_block_until(m, make_timeout_time_us(us)); }
void recursive_mutex_exit(recursive_mutex_t *m) {
    if (!m->enter_count) { emu_log("recursive_mutex_exit on a mutex nobody owns"); return; }
    if (--m->enter_count == 0) m->owner = LOCK_INVALID_OWNER_ID;
}

/* -------------------------------------------------------------- semaphore */
void sem_init(semaphore_t *s, int16_t initial, int16_t max) {
    lock_init(&s->core, next_striped_spin_lock_num());
    s->permits = initial;
    s->max_permits = max;
}
int sem_available(semaphore_t *s) { return s->permits; }
bool sem_release(semaphore_t *s) {
    if (s->permits >= s->max_permits) return false;
    s->permits++;
    return true;
}
void sem_reset(semaphore_t *s, int16_t permits) { s->permits = permits; }
bool sem_try_acquire(semaphore_t *s) {
    if (s->permits <= 0) return false;
    s->permits--;
    return true;
}
bool sem_acquire_block_until(semaphore_t *s, absolute_time_t until) {
    for (;;) {
        if (sem_try_acquire(s)) return true;
        if (until != at_the_end_of_time && time_reached(until)) return false;
        tight_loop_contents();
    }
}
void sem_acquire_blocking(semaphore_t *s) { sem_acquire_block_until(s, at_the_end_of_time); }
bool sem_acquire_timeout_ms(semaphore_t *s, uint32_t ms) { return sem_acquire_block_until(s, make_timeout_time_ms(ms)); }
bool sem_acquire_timeout_us(semaphore_t *s, uint32_t us) { return sem_acquire_block_until(s, make_timeout_time_us(us)); }

/* -------------------------------------------------------- critical section */
void critical_section_init(critical_section_t *cs) {
    critical_section_init_with_lock_num(cs, next_striped_spin_lock_num());
}
void critical_section_init_with_lock_num(critical_section_t *cs, uint n) {
    cs->spin_lock = spin_lock_instance(n);
    cs->save = 0;
}
void critical_section_deinit(critical_section_t *cs) { cs->spin_lock = NULL; }

/* ------------------------------------------------------------------ queue */
/* element_count + 1 slots: one stays empty so full and empty differ. */
bool queue_init_with_spinlock(queue_t *q, uint element_size, uint element_count, uint spinlock_num) {
    lock_init(&q->core, spinlock_num);
    q->data = (uint8_t *)calloc(element_count + 1u, element_size);
    q->element_count = (uint16_t)element_count;
    q->element_size = (uint16_t)element_size;
    q->wptr = q->rptr = 0;
    q->max_level = 0;
    return q->data != NULL;
}
void queue_free(queue_t *q) { free(q->data); q->data = NULL; }

static uint16_t inc(queue_t *q, uint16_t i) { return (uint16_t)(i + 1u > q->element_count ? 0u : i + 1u); }

bool queue_try_add(queue_t *q, const void *data) {
    if (queue_get_level_unsafe(q) == q->element_count) return false;
    memcpy(q->data + (size_t)q->wptr * q->element_size, data, q->element_size);
    q->wptr = inc(q, q->wptr);
    uint level = queue_get_level_unsafe(q);
    if (level > q->max_level) q->max_level = (uint16_t)level;
    return true;
}
static bool take(queue_t *q, void *data, bool remove) {
    if (queue_get_level_unsafe(q) == 0) return false;
    if (data) memcpy(data, q->data + (size_t)q->rptr * q->element_size, q->element_size);
    if (remove) q->rptr = inc(q, q->rptr);
    return true;
}
bool queue_try_remove(queue_t *q, void *data) { return take(q, data, true); }
bool queue_try_peek(queue_t *q, void *data) { return take(q, data, false); }
void queue_add_blocking(queue_t *q, const void *data) { while (!queue_try_add(q, data)) tight_loop_contents(); }
void queue_remove_blocking(queue_t *q, void *data) { while (!take(q, data, true)) tight_loop_contents(); }
void queue_peek_blocking(queue_t *q, void *data) { while (!take(q, data, false)) tight_loop_contents(); }
