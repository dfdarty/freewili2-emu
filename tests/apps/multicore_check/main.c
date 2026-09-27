/* multicore_check — the second core and the SDK's inter-core API, checked
 * from an app. Logs "mc: <step> ok" per step, then "mc: PASS" or
 * "mc: FAIL <step>". */
#include "fw2.h"
#include "platform/diag.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/sync.h"
#include "pico/util/queue.h"
#include "pico/unique_id.h"
#include "hardware/irq.h"
#include "hardware/sync.h"

static bool s_ok = true;
static void check(bool cond, const char *what) {
    if (cond) DIAG("mc: %s ok\n", what);
    else { DIAG("mc: FAIL %s\n", what); s_ok = false; }
}

static volatile uint32_t s_core1_num = 99;
static volatile uint32_t s_hb;
static mutex_t s_mtx;
static volatile uint32_t s_count;
static queue_t s_q;
static semaphore_t s_sem;
static volatile uint32_t s_irq_core = 99, s_irq_hits;
static volatile uint64_t s_core1_slept_us;

#define ROUNDS 2000u

static void core1_main(void) {
    s_core1_num = get_core_num();
    multicore_lockout_victim_init();
    /* 1: FIFO ping-pong */
    for (int i = 0; i < 10; i++) multicore_fifo_push_blocking(multicore_fifo_pop_blocking() + 1u);
    /* 2: mutex contention */
    for (uint32_t i = 0; i < ROUNDS; i++) {
        mutex_enter_blocking(&s_mtx);
        uint32_t c = s_count;
        if ((i & 63u) == 0) tight_loop_contents();   /* hand over while holding it */
        s_count = c + 1u;
        mutex_exit(&s_mtx);
    }
    multicore_fifo_push_blocking(0xC0DE0002u);
    /* 3: queue, 100 items through a 4-deep queue */
    for (uint32_t i = 1; i <= 100; i++) queue_add_blocking(&s_q, &i);
    /* 4: semaphore */
    sleep_ms(20);
    sem_release(&s_sem);
    /* 5: sleeping in parallel with core 0 */
    multicore_fifo_pop_blocking();
    absolute_time_t t0 = get_absolute_time();
    sleep_ms(100);
    s_core1_slept_us = (uint64_t)absolute_time_diff_us(t0, get_absolute_time());
    /* 6: FIFO interrupt on core 0 */
    multicore_fifo_pop_blocking();
    multicore_fifo_push_blocking(0xC0DE0006u);
    /* 7: masked interrupt stays pending */
    multicore_fifo_pop_blocking();
    multicore_fifo_push_blocking(0xC0DE0007u);
    /* 8: heartbeat for lockout; ends when told */
    for (;;) {
        s_hb++;
        if (multicore_fifo_rvalid() && multicore_fifo_pop_blocking() == 0xDEADu) break;
        sleep_us(200);
    }
    /* returning ends core 1 */
}

static void core1_again(void) { s_core1_num = 100u + get_core_num(); for (;;) sleep_ms(10); }

static void fifo_irq(void) {
    s_irq_core = get_core_num();
    while (multicore_fifo_rvalid()) { (void)multicore_fifo_pop_blocking(); s_irq_hits++; }
    multicore_fifo_clear_irq();
}

int main(void) {
    board_init();
    fw2_app_recovery_init();
    st7796_init();
    st7796_fill_screen(0x0000);
    board_backlight_set(1);
    check(get_core_num() == 0, "core0 id");

    mutex_init(&s_mtx);
    queue_init(&s_q, sizeof(uint32_t), 4);
    sem_init(&s_sem, 0, 1);

    multicore_launch_core1(core1_main);
    uint32_t v = 0;
    for (int i = 0; i < 10; i++) { multicore_fifo_push_blocking(v); v = multicore_fifo_pop_blocking() + 1u; }
    check(v == 20u && s_core1_num == 1u, "fifo ping-pong");

    for (uint32_t i = 0; i < ROUNDS; i++) {
        mutex_enter_blocking(&s_mtx);
        uint32_t c = s_count;
        if ((i & 63u) == 32u) tight_loop_contents();
        s_count = c + 1u;
        mutex_exit(&s_mtx);
    }
    check(multicore_fifo_pop_blocking() == 0xC0DE0002u && s_count == 2u * ROUNDS, "mutex");

    uint32_t sum = 0, last = 0, x;
    bool ordered = true;
    for (int i = 0; i < 100; i++) { queue_remove_blocking(&s_q, &x); ordered &= x == last + 1u; last = x; sum += x; }
    check(ordered && sum == 5050u && queue_get_max_level(&s_q) <= 4u, "queue");

    check(!sem_acquire_timeout_ms(&s_sem, 1) && sem_acquire_timeout_ms(&s_sem, 500), "semaphore");

    absolute_time_t t0 = get_absolute_time();
    multicore_fifo_push_blocking(5);
    sleep_ms(100);
    uint64_t both = (uint64_t)absolute_time_diff_us(t0, get_absolute_time());
    while (!s_core1_slept_us) tight_loop_contents();
    uint64_t wall = (uint64_t)absolute_time_diff_us(t0, get_absolute_time());
    DIAG("mc: sleep core0=%u us core1=%u us wall=%u us\n", (unsigned)both, (unsigned)s_core1_slept_us, (unsigned)wall);
    check(s_core1_slept_us >= 100000u && wall < 160000u, "parallel sleep");

    irq_set_exclusive_handler(SIO_IRQ_FIFO, fifo_irq);
    irq_set_enabled(SIO_IRQ_FIFO, true);
    multicore_fifo_push_blocking(6);
    absolute_time_t until = make_timeout_time_ms(200);
    while (!s_irq_hits && !time_reached(until)) tight_loop_contents();
    check(s_irq_hits == 1u && s_irq_core == 0u, "fifo irq on core 0");

    uint32_t save = save_and_disable_interrupts();
    multicore_fifo_push_blocking(7);
    sleep_ms(20);                                  /* core 1 answers meanwhile */
    bool held = s_irq_hits == 1u && multicore_fifo_rvalid();
    restore_interrupts(save);
    check(held && s_irq_hits == 2u, "masked irq held until restore");
    irq_set_enabled(SIO_IRQ_FIFO, false);

    sleep_ms(20);
    multicore_lockout_start_blocking();
    uint32_t hb = s_hb;
    sleep_ms(30);
    bool frozen = s_hb == hb;
    multicore_lockout_end_blocking();
    sleep_ms(30);
    check(frozen && s_hb > hb, "lockout");

    spin_lock_t *sl = spin_lock_instance((uint)spin_lock_claim_unused(true));
    uint32_t ss = spin_lock_blocking(sl);
    bool locked = is_spin_locked(sl);
    spin_unlock(sl, ss);
    check(locked && !is_spin_locked(sl), "spin lock");

    multicore_fifo_push_blocking(0xDEADu);         /* core 1 returns */
    sleep_ms(20);
    multicore_reset_core1();
    multicore_launch_core1(core1_again);
    sleep_ms(20);
    check(s_core1_num == 101u, "relaunch");

    char id[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
    pico_get_unique_board_id_string(id, sizeof id);
    DIAG("mc: board id %s\n", id);

    DIAG(s_ok ? "mc: PASS\n" : "mc: FAIL\n");
    for (;;) { fw2_app_recovery_task(); tight_loop_contents(); }
}
