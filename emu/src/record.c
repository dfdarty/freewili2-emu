/* record.c — turn what a person does into an input script.
 *
 * With --record FILE (or the Record button on the web page) every input a
 * person gives is logged with its time: buttons (keys or mouse on the
 * panel), touches and drags on the glass, screenshots (F2), and commands
 * from the web page's panels (sensor sliders, header pins, sensor logs, the
 * guide's buttons). Input a script gives is not recorded.
 *
 * At the end the log becomes a script that replays the session with the same
 * timing: `wait`, `press`, `touch`, `drag`, `hold`/`release`, `set`... and
 * after each step, the lines the app logged in response as commented-out
 * `# expect "..."` lines. Uncommenting the ones that matter turns the
 * recording into a pass/fail test. */
#include "emu/emu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

extern const unsigned char fw2app_uf2_info[];

enum { E_BTN_DOWN, E_BTN_UP, E_TOUCH_DOWN, E_TOUCH_MOVE, E_TOUCH_UP, E_CMD, E_SHOT, E_LOG };

typedef struct {
    uint64_t t;                /* us since the emulator started */
    int      type;
    int      a, b;             /* button, or x/y */
    char    *text;             /* command / log line */
} ev_t;

static struct {
    bool     on;
    char     path[512];
    uint64_t start_us;
    ev_t    *ev;
    size_t   n, cap;
    uint16_t buttons;
    bool     touching;
    unsigned logs_since_action;
} R;

#define MAX_LOGS_PER_STEP 40   /* kept per step; the script shows the first 2 and last 3 */

bool emu_rec_active(void) { return R.on; }

static void add(int type, int a, int b, const char *text) {
    if (!R.on) return;
    if (R.n == R.cap) {
        R.cap = R.cap ? R.cap * 2 : 256;
        R.ev = (ev_t *)realloc(R.ev, R.cap * sizeof *R.ev);
        if (!R.ev) emu_fatal("record: out of memory");
    }
    ev_t *e = &R.ev[R.n++];
    e->t = emu_time_us() - R.start_us;
    e->type = type;
    e->a = a;
    e->b = b;
    e->text = text ? strdup(text) : NULL;
}

void emu_rec_start(const char *path) {
    if (R.on) emu_rec_stop();
    memset(&R, 0, sizeof R);
    snprintf(R.path, sizeof R.path, "%s", path);
    R.start_us = 0;                      /* scripts run from the emulator's start, so times do too */
    R.buttons = emu_pic_buttons();
    R.on = true;
    emu_log("record: recording your input to %s", path);
}

void emu_rec_buttons(uint16_t mask) {
    if (!R.on) return;
    uint16_t changed = mask ^ R.buttons;
    for (unsigned b = 0; b < EMU_BTN_COUNT; b++)
        if (changed & (1u << b)) {
            add((mask >> b) & 1u ? E_BTN_DOWN : E_BTN_UP, (int)b, 0, NULL);
            if ((mask >> b) & 1u) R.logs_since_action = 0;
        }
    R.buttons = mask;
}

void emu_rec_touch(int x, int y, bool down) {
    if (!R.on) return;
    if (down && !R.touching) { R.touching = true; add(E_TOUCH_DOWN, x, y, NULL); R.logs_since_action = 0; }
    else if (down) add(E_TOUCH_MOVE, x, y, NULL);
    else if (R.touching) { R.touching = false; add(E_TOUCH_UP, x, y, NULL); }
}

void emu_rec_command(const char *line) { if (R.on) { add(E_CMD, 0, 0, line); R.logs_since_action = 0; } }
void emu_rec_screenshot(void) { add(E_SHOT, 0, 0, NULL); }

void emu_rec_log_line(const char *line) {
    if (!R.on || R.logs_since_action > MAX_LOGS_PER_STEP) return;
    static const char *const skip[] = { "script:", "perf:", "record:", "screenshot", "web:", "sleep ", "stopped" };
    for (size_t i = 0; i < sizeof skip / sizeof skip[0]; i++)
        if (!strncmp(line, skip[i], strlen(skip[i]))) return;
    if (!line[0]) return;
    /* Lines the app repeats (a status line every second) say nothing about
     * a step: keep only the first time each line appears. */
    for (size_t i = R.n; i-- > 0; )
        if (R.ev[i].type == E_LOG && !strcmp(R.ev[i].text, line)) return;
    R.logs_since_action++;
    add(E_LOG, 0, 0, line);
}

/* ------------------------------------------------------------- writing */
static bool is_action(const ev_t *e) { return e->type != E_LOG && e->type != E_TOUCH_MOVE; }

static long ms_of(uint64_t us) { return (long)((us + 500u) / 1000u); }

/* How long a script command keeps the script busy (see script.c: timed
 * commands resume 50 ms after they end). */
static uint64_t consumes_us(const char *cmd) {
    char w[16], btn[32];
    long a, b, c, d, e, ms = -1;
    if (sscanf(cmd, "%15s", w) != 1) return 0;
    if (!strcmp(w, "press")) { ms = 150; if (sscanf(cmd, "%*s %31s %ld", btn, &a) == 2) ms = a; }
    else if (!strcmp(w, "touch")) { ms = 120; if (sscanf(cmd, "%*s %ld %ld %ld", &a, &b, &c) == 3) ms = c; }
    else if (!strcmp(w, "drag")) { ms = 300; if (sscanf(cmd, "%*s %ld %ld %ld %ld %ld", &a, &b, &c, &d, &e) == 5) ms = e; }
    return ms < 0 ? 0 : (uint64_t)(ms + 50) * 1000u;
}

/* A log line as a regular expression. Numbers of three or more digits
 * (times, readings) become [0-9]+ since they vary from run to run; shorter
 * ones (counts, ids) stay as they were. */
static void regex_quote(const char *s, char *out, size_t cap) {
    size_t k = 0;
    while (*s && k + 8 < cap) {
        if (*s >= '0' && *s <= '9') {
            size_t n = strspn(s, "0123456789");
            if (n >= 3) { memcpy(out + k, "[0-9]+", 6); k += 6; }
            else { memcpy(out + k, s, n); k += n; }
            s += n;
            continue;
        }
        if (strchr(".*+?()[]{}^$|\\", *s)) out[k++] = '\\';
        if (*s == '"') { out[k++] = '.'; s++; continue; }            /* the script's quotes have no escape */
        out[k++] = *s++;
    }
    out[k] = 0;
}

static const char *set_name(const char *cmd, char *buf, size_t cap) {
    if (strncmp(cmd, "set ", 4)) return NULL;
    if (sscanf(cmd + 4, "%63s", buf) != 1 || cap < 64) return NULL;
    return buf;
}

static void write_script(FILE *f) {
    time_t now = time(NULL);
    char when[32];
    strftime(when, sizeof when, "%Y-%m-%d %H:%M", localtime(&now));
    const char *name = (const char *)fw2app_uf2_info + 16;           /* fw2app_uf2_info_t.name */
    fprintf(f, "# Recorded by the FREE-WILi 2 emulator (--record) on %s, app %.31s.\n", when, name);
    fprintf(f, "# Replays what you did with the same timing. The \"# expect\" lines are what the\n"
               "# app logged after each step (numbers of 3+ digits loosened to [0-9]+):\n"
               "# uncomment the ones that matter to turn this into a pass/fail test.\n"
               "# https://dfdarty.github.io/freewili2-emu/scripting/\n");
    uint64_t sc = 0;                          /* where the script's own clock is */
    unsigned shots = 0;
    bool *done = (bool *)calloc(R.n + 1, 1);
    for (size_t i = 0; i < R.n; i++) {
        ev_t *e = &R.ev[i];
        if (done[i] || e->type == E_TOUCH_MOVE) continue;
        if (e->type == E_LOG) {
            /* The lines the app logged after a step. A long run shows its
             * first two (the reaction) and last three (where it settled). */
            size_t k = i, nlog = 0;
            while (k < R.n && !is_action(&R.ev[k])) { if (R.ev[k].type == E_LOG) nlog++; k++; }
            size_t seen = 0;
            for (size_t j = i; j < k; j++) {
                if (R.ev[j].type != E_LOG) continue;
                done[j] = true;
                seen++;
                if (nlog > 5 && seen > 2 && seen <= nlog - 3) {
                    if (seen == 3) fprintf(f, "# ... %zu more lines ...\n", nlog - 5);
                    continue;
                }
                /* A line that came later than the script would get there is
                 * waited for, so uncommenting its expect keeps the timing. */
                if (R.ev[j].t > sc + 50000u) { fprintf(f, "wait %ld\n", ms_of(R.ev[j].t - sc)); sc = R.ev[j].t; }
                char q[600];
                regex_quote(R.ev[j].text, q, sizeof q);
                fprintf(f, "# expect \"%s\"\n", q);
            }
            continue;
        }
        if (e->type == E_CMD) {                /* a slider dragged: keep only where it stopped */
            char n1[64], n2[64];
            const char *a = set_name(e->text, n1, sizeof n1);
            bool superseded = false;
            for (size_t j = i + 1; a && j < R.n; j++) {
                if (!is_action(&R.ev[j])) continue;
                const char *b = R.ev[j].type == E_CMD ? set_name(R.ev[j].text, n2, sizeof n2) : NULL;
                superseded = b && !strcmp(a, b) && R.ev[j].t - e->t < 500000u;
                break;
            }
            if (superseded) continue;
        }
        if (e->t > sc + 1000u) { fprintf(f, "wait %ld\n", ms_of(e->t - sc)); sc = e->t; }
        else if (e->t > sc) sc = e->t;
        switch (e->type) {
        case E_BTN_DOWN: {
            size_t up = 0;
            bool alone = true;
            for (size_t j = i + 1; j < R.n; j++) {
                if (R.ev[j].type == E_BTN_UP && R.ev[j].a == e->a) { up = j; break; }
                if (is_action(&R.ev[j])) alone = false;
            }
            if (up && alone) {
                long ms = ms_of(R.ev[up].t - e->t);
                if (ms < 1) ms = 1;
                fprintf(f, "press %s %ld\n", emu_btn_name((unsigned)e->a), ms);
                done[up] = true;
                sc += (uint64_t)(ms + 50) * 1000u;
            } else {
                fprintf(f, "hold %s\n", emu_btn_name((unsigned)e->a));
            }
            break;
        }
        case E_BTN_UP:
            fprintf(f, "release %s\n", emu_btn_name((unsigned)e->a));
            break;
        case E_TOUCH_DOWN: {
            int x1 = e->a, y1 = e->b;
            uint64_t t1 = e->t;
            for (size_t j = i + 1; j < R.n; j++) {
                if (R.ev[j].type == E_TOUCH_MOVE) { x1 = R.ev[j].a; y1 = R.ev[j].b; t1 = R.ev[j].t; done[j] = true; }
                else if (R.ev[j].type == E_TOUCH_UP) { t1 = R.ev[j].t; done[j] = true; break; }
            }
            long ms = ms_of(t1 - e->t);
            if (ms < 20) ms = 20;
            int dx = x1 - e->a, dy = y1 - e->b;
            if (dx * dx + dy * dy <= 36) fprintf(f, "touch %d %d %ld\n", e->a, e->b, ms);
            else fprintf(f, "drag %d %d %d %d %ld\n", e->a, e->b, x1, y1, ms);
            sc += (uint64_t)(ms + 50) * 1000u;
            break;
        }
        case E_TOUCH_UP:
            break;                             /* a touch that started before recording */
        case E_CMD:
            fprintf(f, "%s\n", e->text);
            sc += consumes_us(e->text);
            break;
        case E_SHOT:
            fprintf(f, "screenshot out/recorded-%u.png device\n", ++shots);
            break;
        }
    }
    free(done);
    uint64_t end = emu_time_us() - R.start_us;
    if (end > sc + 1000u) fprintf(f, "wait %ld\n", ms_of(end - sc > 2000000u ? 2000000u : end - sc));
    fprintf(f, "quit\n");
}

void emu_rec_stop(void) {
    if (!R.on) return;
    R.on = false;
    emu_make_parents(R.path);
    FILE *f = fopen(R.path, "w");
    if (!f) {
        emu_log("record: can't write %s", R.path);
    } else {
        write_script(f);
        fclose(f);
        emu_log("record: script written to %s", R.path);
    }
    for (size_t i = 0; i < R.n; i++) free(R.ev[i].text);
    free(R.ev);
    R.ev = NULL;
    R.n = R.cap = 0;
}
