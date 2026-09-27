/* pico/time.h + hardware/timer.h — host shim. Time is real (monotonic) time.
 * Every wait services the emulator so the display and inputs stay live. */
#ifndef FW2EMU_PICO_TIME_H
#define FW2EMU_PICO_TIME_H

#include "pico.h"

#ifdef __cplusplus
extern "C" {
#endif

uint64_t emu_time_us(void);
void     emu_poll(void);
void     emu_sleep_us(uint64_t us);

typedef uint64_t absolute_time_t;

static inline uint64_t to_us_since_boot(absolute_time_t t) { return t; }
static inline void update_us_since_boot(absolute_time_t *t, uint64_t us) { *t = us; }
static inline absolute_time_t from_us_since_boot(uint64_t us) { return us; }

static inline uint64_t time_us_64(void) { emu_poll(); return emu_time_us(); }
static inline uint32_t time_us_32(void) { return (uint32_t)time_us_64(); }

static inline absolute_time_t get_absolute_time(void) { return time_us_64(); }
static inline uint32_t to_ms_since_boot(absolute_time_t t) { return (uint32_t)(t / 1000u); }

static inline absolute_time_t delayed_by_us(absolute_time_t t, uint64_t us) { return t + us; }
static inline absolute_time_t delayed_by_ms(absolute_time_t t, uint32_t ms) { return t + (uint64_t)ms * 1000u; }
static inline absolute_time_t make_timeout_time_us(uint64_t us) { return get_absolute_time() + us; }
static inline absolute_time_t make_timeout_time_ms(uint32_t ms) { return get_absolute_time() + (uint64_t)ms * 1000u; }

static inline int64_t absolute_time_diff_us(absolute_time_t from, absolute_time_t to) {
    return (int64_t)(to - from);
}
static inline absolute_time_t absolute_time_min(absolute_time_t a, absolute_time_t b) { return a < b ? a : b; }

#define at_the_end_of_time ((absolute_time_t)0x7fffffffffffffffull)
#define nil_time ((absolute_time_t)0)
static inline bool is_at_the_end_of_time(absolute_time_t t) { return t == at_the_end_of_time; }
static inline bool is_nil_time(absolute_time_t t) { return t == 0; }

static inline bool time_reached(absolute_time_t t) { return get_absolute_time() >= t; }

static inline void busy_wait_us_32(uint32_t us) { emu_sleep_us(us); }
static inline void busy_wait_us(uint64_t us) { emu_sleep_us(us); }
static inline void busy_wait_ms(uint32_t ms) { emu_sleep_us((uint64_t)ms * 1000u); }
static inline void busy_wait_until(absolute_time_t t) {
    uint64_t now = emu_time_us();
    if (t > now) emu_sleep_us(t - now);
}

static inline void sleep_us(uint64_t us) { emu_sleep_us(us); }
static inline void sleep_ms(uint32_t ms) { emu_sleep_us((uint64_t)ms * 1000u); }
static inline void sleep_until(absolute_time_t t) { busy_wait_until(t); }
static inline bool best_effort_wfe_or_timeout(absolute_time_t t) {
    emu_sleep_us(50);
    return time_reached(t);
}

/* Alarms and repeating timers. Callbacks run from the emulator's service
 * loop (emu_poll, up to 1 kHz), the host's stand-in for the timer IRQ. */
typedef int32_t alarm_id_t;
typedef struct alarm_pool alarm_pool_t;
typedef int64_t (*alarm_callback_t)(alarm_id_t id, void *user_data);
typedef struct repeating_timer repeating_timer_t;
typedef bool (*repeating_timer_callback_t)(repeating_timer_t *rt);
struct repeating_timer {
    int64_t delay_us;
    alarm_pool_t *pool;
    alarm_id_t alarm_id;
    repeating_timer_callback_t callback;
    void *user_data;
};

alarm_pool_t *alarm_pool_get_default(void);
alarm_id_t alarm_pool_add_alarm_in_us(alarm_pool_t *pool, uint64_t us, alarm_callback_t cb, void *user_data, bool fire_if_past);
bool alarm_pool_cancel_alarm(alarm_pool_t *pool, alarm_id_t id);
bool alarm_pool_add_repeating_timer_us(alarm_pool_t *pool, int64_t delay_us, repeating_timer_callback_t cb, void *user_data, repeating_timer_t *out);
static inline alarm_id_t add_alarm_in_us(uint64_t us, alarm_callback_t cb, void *user_data, bool fire_if_past) {
    return alarm_pool_add_alarm_in_us(alarm_pool_get_default(), us, cb, user_data, fire_if_past);
}
static inline alarm_id_t add_alarm_in_ms(uint32_t ms, alarm_callback_t cb, void *user_data, bool fire_if_past) {
    return add_alarm_in_us((uint64_t)ms * 1000u, cb, user_data, fire_if_past);
}
static inline alarm_id_t add_alarm_at(absolute_time_t t, alarm_callback_t cb, void *user_data, bool fire_if_past) {
    uint64_t now = get_absolute_time();
    return add_alarm_in_us(t > now ? t - now : 0, cb, user_data, fire_if_past);
}
static inline bool cancel_alarm(alarm_id_t id) { return alarm_pool_cancel_alarm(alarm_pool_get_default(), id); }
static inline bool add_repeating_timer_us(int64_t delay_us, repeating_timer_callback_t cb, void *user_data, repeating_timer_t *out) {
    return alarm_pool_add_repeating_timer_us(alarm_pool_get_default(), delay_us, cb, user_data, out);
}
static inline bool add_repeating_timer_ms(int32_t delay_ms, repeating_timer_callback_t cb, void *user_data, repeating_timer_t *out) {
    return add_repeating_timer_us((int64_t)delay_ms * 1000, cb, user_data, out);
}
static inline bool cancel_repeating_timer(repeating_timer_t *timer) {
    bool r = timer->alarm_id ? alarm_pool_cancel_alarm(timer->pool, timer->alarm_id) : false;
    timer->alarm_id = 0;
    return r;
}

#ifdef __cplusplus
}
#endif

#endif
