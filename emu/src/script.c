/* script.c — scripted input for headless runs, plus PNG screenshots.
 *
 * Script syntax (one command per line, '#' comments):
 *   wait MS                  pause the script
 *   press BTN [MS]           press and release a button (default 150 ms)
 *   hold BTN / release BTN   hold or release a button
 *   touch X Y [MS]           tap the screen (default 120 ms)
 *   drag X1 Y1 X2 Y2 [MS]    swipe (default 300 ms)
 *   screenshot FILE [lcd|device]
 *   set NAME V [V V]         sensors: temp rh lux accel gyro mag tilt noise
 *   log TEXT
 *   header                   log the MAIN-CPU GPIO header state (pins, VIO, Vout)
 *   expect REGEX [MS]        wait (default 2000 ms) for a DIAG / log line matching the
 *                            POSIX extended REGEX ("quoted" if it has spaces); on a
 *                            timeout the run ends at once with exit status 1
 *   quit
 * Buttons: GREY YELLOW GREEN BLUE RED CENTER UP DOWN LEFT RIGHT HOME OK CANCEL PAGE
 */
#include "emu/emu.h"

#include <ctype.h>
#include <errno.h>
#include <regex.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef FW2_EMU_HAVE_ZLIB
#include <zlib.h>
#endif

bool emu_touch_get(int *x, int *y);

#define MAX_LINES 1024
static char   *s_lines[MAX_LINES];
static int     s_nlines, s_pc;
static bool    s_active;
static uint64_t s_resume_us;
static struct { bool on; int btn; uint64_t until; } s_press;
static struct { bool on; uint64_t start, until; int x1, y1, x2, y2; } s_touch;

/* ------------------------------------------------ output lines for expect */
/* Every DIAG line and emulator log line (without its [diag]/[emu] prefix)
 * since the run started. `expect` consumes them like pexpect: it looks at
 * lines after the one its previous match was on, so output printed before
 * the `expect` line was reached still counts. */
#define NLINES   512
#define LINE_MAX_LEN 320
static char     s_out[NLINES][LINE_MAX_LEN];
static uint64_t s_out_count;             /* lines captured so far */
static uint64_t s_cursor;                /* first line the next expect examines */
static struct {
    bool     on;
    regex_t  re;
    char     src[256];
    uint64_t deadline;
    int      line, timeout_ms;
} s_expect;

void emu_script_line(const char *line) {
    emu_rec_log_line(line);
    snprintf(s_out[s_out_count % NLINES], LINE_MAX_LEN, "%s", line);
    s_out_count++;
}

/* True when a line since the cursor matches; the cursor moves past it. */
static bool expect_scan(void) {
    if (s_out_count - s_cursor > NLINES) s_cursor = s_out_count - NLINES;
    for (; s_cursor < s_out_count; s_cursor++)
        if (regexec(&s_expect.re, s_out[s_cursor % NLINES], 0, NULL, 0) == 0) {
            s_cursor++;
            return true;
        }
    return false;
}

static void strip_comment(char *p);

void emu_script_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) emu_fatal("cannot open script %s", path);
    char buf[512];
    while (fgets(buf, sizeof buf, f) && s_nlines < MAX_LINES) {
        char *p = buf;
        while (isspace((unsigned char)*p)) p++;
        strip_comment(p);
        if (!*p) continue;
        s_lines[s_nlines++] = strdup(p);
    }
    fclose(f);
    s_active = s_nlines > 0;
    emu_log("script: %d commands from %s", s_nlines, path);
}

bool emu_script_active(void) { return s_active; }

static void set_btn(int b, bool down) {
    uint16_t m = emu_pic_buttons();
    m = down ? (uint16_t)(m | (1u << b)) : (uint16_t)(m & ~(1u << b));
    emu_pic_set_buttons(m);
}

static int need_btn(const char *name, int line) {
    int b = name ? emu_btn_from_name(name) : -1;
    if (b < 0) emu_fatal("script line %d: unknown button '%s'", line + 1, name ? name : "");
    return b;
}

/* expect "REGEX with spaces" [MS]  |  expect REGEX [MS] */
static void exec_expect(const char *args, uint64_t now, int ln) {
    while (*args == ' ' || *args == '\t') args++;
    char re[256];
    size_t n = 0;
    if (*args == '"') {
        args++;
        while (*args && *args != '"' && n < sizeof re - 1) {
            if (*args == '\\' && args[1] == '"') args++;      /* \" inside quotes */
            re[n++] = *args++;
        }
        if (*args != '"') emu_fatal("script line %d: expect: missing closing quote", ln + 1);
        args++;
    } else {
        while (*args && *args != ' ' && *args != '\t' && n < sizeof re - 1) re[n++] = *args++;
    }
    re[n] = 0;
    if (!n) emu_fatal("script line %d: expect REGEX [TIMEOUT_MS]", ln + 1);
    while (*args == ' ' || *args == '\t') args++;
    int ms = 2000;
    if (*args) {
        char *end;
        long v = strtol(args, &end, 10);
        while (*end == ' ' || *end == '\t') end++;
        if (*end || v <= 0) emu_fatal("script line %d: expect: bad timeout '%s'", ln + 1, args);
        ms = (int)v;
    }
    if (s_expect.src[0]) regfree(&s_expect.re);
    int rc = regcomp(&s_expect.re, re, REG_EXTENDED | REG_NOSUB);
    if (rc) {
        char msg[128];
        regerror(rc, &s_expect.re, msg, sizeof msg);
        emu_fatal("script line %d: expect: bad regex /%s/: %s", ln + 1, re, msg);
    }
    snprintf(s_expect.src, sizeof s_expect.src, "%s", re);
    s_expect.on = true;
    s_expect.line = ln + 1;
    s_expect.timeout_ms = ms;
    s_expect.deadline = now + (uint64_t)ms * 1000u;
}

/* Strict argument parsing: a token that isn't a number is an error, not 0. */
static long need_int(const char *t, const char *cmd, int ln) {
    char *end;
    long v = t ? strtol(t, &end, 10) : 0;
    if (!t || end == t || *end) emu_fatal("script line %d: %s: '%s' is not a whole number", ln + 1, cmd, t ? t : "");
    return v;
}

static float need_float(const char *t, const char *cmd, int ln) {
    char *end;
    float v = t ? strtof(t, &end) : 0;
    if (!t || end == t || *end) emu_fatal("script line %d: %s: '%s' is not a number", ln + 1, cmd, t ? t : "");
    return v;
}

static long opt_ms(const char *t, long dflt, const char *cmd, int ln) {
    long v = t ? need_int(t, cmd, ln) : dflt;
    if (v < 0) emu_fatal("script line %d: %s: negative time %ld", ln + 1, cmd, v);
    return v;
}

static bool s_from_web;              /* a command from the page: errors are logged, not fatal */

static void exec(char *line, uint64_t now) {
    char raw[512];
    snprintf(raw, sizeof raw, "%s", line);
    int ln = s_pc - 1;
    if (!strncmp(line, "expect", 6) && (line[6] == ' ' || line[6] == '\t' || !line[6])) {
        exec_expect(line + 6, now, ln);
        return;
    }
    if (!strncmp(line, "radio", 5) && (line[5] == ' ' || line[5] == '\t' || !line[5])) {
        const char *rest = line + 5;                   /* free text: SSIDs and names have spaces */
        while (*rest == ' ' || *rest == '\t') rest++;
        const char *err = !strncmp(rest, "load ", 5) ? emu_radio_load(rest + 5) : emu_radio_line(rest);
        if (err) {
            if (s_from_web) emu_log("radio: %s", err);
            else emu_fatal("script line %d: radio: %s", ln + 1, err);
        }
        return;
    }
    char *save = NULL, *t[8] = { 0 };
    int n = 0;
    for (char *tok = strtok_r(line, " \t", &save); tok; tok = strtok_r(NULL, " \t", &save)) {
        if (n == 8) emu_fatal("script line %d: too many arguments", ln + 1);
        t[n++] = tok;
    }
    if (!n) return;
    const char *cmd = t[0];
    int nargs = n - 1;
#define ARGS(lo, hi, usage) \
    do { if (nargs < (lo) || nargs > (hi)) emu_fatal("script line %d: usage: %s", ln + 1, usage); } while (0)
    if (!strcmp(cmd, "wait")) {
        ARGS(1, 1, "wait MS");
        s_resume_us = now + (uint64_t)opt_ms(t[1], 0, cmd, ln) * 1000u;
    } else if (!strcmp(cmd, "press")) {
        ARGS(1, 2, "press BTN [MS]");
        int b = need_btn(t[1], ln);
        long ms = opt_ms(t[2], 150, cmd, ln);
        set_btn(b, true);
        s_press.on = true; s_press.btn = b;
        s_press.until = now + (uint64_t)ms * 1000u;
        s_resume_us = s_press.until + 50000u;
    } else if (!strcmp(cmd, "hold") || !strcmp(cmd, "release")) {
        ARGS(1, 1, "hold BTN / release BTN");
        set_btn(need_btn(t[1], ln), cmd[0] == 'h');
    } else if (!strcmp(cmd, "touch")) {
        ARGS(2, 3, "touch X Y [MS]");
        s_touch.x1 = s_touch.x2 = (int)need_int(t[1], cmd, ln);
        s_touch.y1 = s_touch.y2 = (int)need_int(t[2], cmd, ln);
        long ms = opt_ms(t[3], 120, cmd, ln);
        s_touch.on = true;
        s_touch.start = now;
        s_touch.until = now + (uint64_t)ms * 1000u;
        emu_touch_set(s_touch.x1, s_touch.y1, true);
        s_resume_us = s_touch.until + 50000u;
    } else if (!strcmp(cmd, "drag")) {
        ARGS(4, 5, "drag X1 Y1 X2 Y2 [MS]");
        s_touch.x1 = (int)need_int(t[1], cmd, ln); s_touch.y1 = (int)need_int(t[2], cmd, ln);
        s_touch.x2 = (int)need_int(t[3], cmd, ln); s_touch.y2 = (int)need_int(t[4], cmd, ln);
        long ms = opt_ms(t[5], 300, cmd, ln);
        s_touch.on = true;
        s_touch.start = now;
        s_touch.until = now + (uint64_t)ms * 1000u;
        emu_touch_set(s_touch.x1, s_touch.y1, true);
        s_resume_us = s_touch.until + 50000u;
    } else if (!strcmp(cmd, "screenshot")) {
        ARGS(1, 2, "screenshot FILE [lcd|device]");
        if (t[2] && strcmp(t[2], "lcd") && strcmp(t[2], "device"))
            emu_fatal("script line %d: screenshot: '%s' is not lcd or device", ln + 1, t[2]);
        bool dev = t[2] && !strcmp(t[2], "device");
        if (emu_screenshot(t[1], dev) != 0) {
            char why[600];
            snprintf(why, sizeof why, "script line %d: screenshot %s could not be written", ln + 1, t[1]);
            emu_log("FAIL %s", why);
            emu_run_end(why, 1);
        }
        emu_log("script: screenshot -> %s", t[1]);
    } else if (!strcmp(cmd, "set")) {
        ARGS(2, 5, "set NAME V [V V V]");
        float v[4];
        for (int i = 0; i < nargs - 1; i++) v[i] = need_float(t[2 + i], cmd, ln);
        if (!emu_sensor_set(t[1], nargs - 1, v))
            emu_fatal("script line %d: set NAME V [V V V] (temp rh lux accel gyro mag tilt noise mics mic.A-D "
                      "tone miclevel gpioN vrefext)", ln + 1);
        if (emu_verbose) { char d[160]; emu_sensor_describe(d, sizeof d); emu_log("sensors: %s", d); }
    } else if (!strcmp(cmd, "play")) {
        ARGS(1, 3, "play FILE|@launch [loop] [step]  /  play stop");
        if (!strcmp(t[1], "stop") && nargs == 1) { emu_sensor_play_stop(); return; }
        bool loop = false, step = false;
        for (int i = 2; i <= nargs; i++) {
            if (!strcmp(t[i], "loop")) loop = true;
            else if (!strcmp(t[i], "step")) step = true;
            else emu_fatal("script line %d: play: '%s' is not loop or step", ln + 1, t[i]);
        }
        const char *err = emu_sensor_play(t[1], loop, step);
        if (err) {
            if (s_from_web) emu_log("play: %s", err);
            else emu_fatal("script line %d: play %s", ln + 1, err);
        }
    } else if (!strcmp(cmd, "log")) {
        const char *rest = raw + 3;
        while (*rest == ' ' || *rest == '\t') rest++;
        emu_log("script: %s", rest);
    } else if (!strcmp(cmd, "header")) {
        ARGS(0, 0, "header");
        emu_header_pin_t pins[EMU_HEADER_PINS];
        char out[400];
        int k = snprintf(out, sizeof out, "VIO %.2f V, Vout %.2f V;", emu_main_vio(), emu_main_vout());
        emu_main_header(pins);
        for (int i = 0; i < EMU_HEADER_PINS && k < (int)sizeof out - 16; i++)
            k += snprintf(out + k, sizeof out - (size_t)k, " %u=%d%s", (unsigned)pins[i].gpio, pins[i].level,
                          pins[i].pwm ? "(pwm)" : pins[i].output ? "(out)" : pins[i].ext ? "(ext)" : "");
        emu_log("script: header %s", out);
    } else if (!strcmp(cmd, "quit")) {
        ARGS(0, 0, "quit");
        emu_run_end("script quit", 0);
    } else {
        emu_fatal("script line %d: unknown command '%s'", ln + 1, cmd);
    }
#undef ARGS
}

/* Remove a '#' comment that is not inside double quotes, then trailing
 * blanks. Used for script files and for commands from the web page. */
static void strip_comment(char *p) {
    bool quoted = false;
    for (char *c = p; *c; c++) {
        if (*c == '\\' && quoted && c[1]) { c++; continue; }
        if (*c == '"') quoted = !quoted;
        else if (*c == '#' && !quoted) { *c = 0; break; }
    }
    char *e = p + strlen(p);
    while (e > p && isspace((unsigned char)e[-1])) *--e = 0;
}

/* Immediate command from outside the script timeline (e.g. the web page).
 * Timed commands (press/touch/drag) still use their durations; `wait` and
 * `quit` are not accepted here. */
bool emu_script_exec_line(const char *line) {
    char buf[512];
    snprintf(buf, sizeof buf, "%s", line);
    char *p = buf;
    while (*p == ' ') p++;
    strip_comment(p);
    if (!*p || !strncmp(p, "wait", 4) || !strncmp(p, "quit", 4)) return false;
    if (!strncmp(p, "record ", 7)) {                    /* the page's Record button */
        const char *arg = p + 7;
        while (*arg == ' ') arg++;
        if (!strcmp(arg, "stop")) emu_rec_stop();
        else emu_rec_start(arg);
        return true;
    }
    static const char *const ok[] = { "press", "hold", "release", "touch", "drag", "set", "log", "screenshot", "header", "play" };
    bool known = false;
    for (size_t i = 0; i < sizeof ok / sizeof ok[0]; i++)
        if (!strncmp(p, ok[i], strlen(ok[i]))) known = true;
    if (!known) return false;
    emu_rec_command(p);
    s_from_web = true;
    exec(p, emu_time_us());
    s_from_web = false;
    return true;
}

void emu_script_task(void) {
    uint64_t now = emu_time_us();
    if (s_press.on && now >= s_press.until) { set_btn(s_press.btn, false); s_press.on = false; }
    if (s_touch.on) {
        if (now >= s_touch.until) { emu_touch_set(s_touch.x2, s_touch.y2, false); s_touch.on = false; }
        else {
            double t = (double)(now - s_touch.start) / (double)(s_touch.until - s_touch.start);
            emu_touch_set(s_touch.x1 + (int)((s_touch.x2 - s_touch.x1) * t),
                          s_touch.y1 + (int)((s_touch.y2 - s_touch.y1) * t), true);
        }
    }
    if (s_expect.on) {
        if (expect_scan()) {
            s_expect.on = false;
            if (emu_verbose) emu_log("script: expect /%s/ matched", s_expect.src);
        } else if (now >= s_expect.deadline) {
            char why[400];
            snprintf(why, sizeof why, "script line %d: expect /%s/ -- no matching line within %d ms",
                     s_expect.line, s_expect.src, s_expect.timeout_ms);
            emu_log("FAIL %s", why);
            emu_run_end(why, 1);
        }
    }
    while (s_active && now >= s_resume_us && !s_press.on && !s_touch.on && !s_expect.on) {
        if (s_pc >= s_nlines) { s_active = false; break; }
        char buf[512];
        snprintf(buf, sizeof buf, "%s", s_lines[s_pc++]);
        exec(buf, now);
    }
}

/* ------------------------------------------------------------------ PNG */
static uint32_t crc_table[256];
static void crc_init(void) {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[n] = c;
    }
}
static uint32_t crc_upd(uint32_t c, const uint8_t *b, size_t n) {
    for (size_t i = 0; i < n; i++) c = crc_table[(c ^ b[i]) & 0xFF] ^ (c >> 8);
    return c;
}
static void be32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void chunk(FILE *f, const char *type, const uint8_t *data, size_t n) {
    uint8_t h[8];
    be32(h, (uint32_t)n);
    memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    if (n) fwrite(data, 1, n, f);
    uint32_t c = crc_upd(0xFFFFFFFFu, (const uint8_t *)type, 4);
    c = crc_upd(c, data, n) ^ 0xFFFFFFFFu;
    uint8_t t[4];
    be32(t, c);
    fwrite(t, 1, 4, f);
}

/* PNG with zlib when the build found it (FW2_EMU_HAVE_ZLIB), otherwise
 * stored (uncompressed) deflate blocks: larger files, no dependency. */
static size_t zlib_stored(const uint8_t *raw, size_t raw_len, uint8_t **out);

/* Create the folders a file path needs (like `mkdir -p "$(dirname PATH)"`). */
void emu_make_parents(const char *path) {
    char d[1024];
    snprintf(d, sizeof d, "%s", path);
    for (char *c = d + 1; *c; c++)
        if (*c == '/') { *c = 0; mkdir(d, 0777); *c = '/'; }
}

static int write_png(const char *path, const uint32_t *argb, int w, int h) {
    if (!crc_table[1]) crc_init();
    size_t raw_len = (size_t)h * (1 + (size_t)w * 3);
    uint8_t *raw = (uint8_t *)malloc(raw_len);
    size_t k = 0;
    for (int y = 0; y < h; y++) {
        raw[k++] = 0;
        for (int x = 0; x < w; x++) {
            uint32_t p = argb[y * w + x];
            raw[k++] = (uint8_t)(p >> 16); raw[k++] = (uint8_t)(p >> 8); raw[k++] = (uint8_t)p;
        }
    }
    uint8_t *z = NULL;
    size_t o = 0;
#ifdef FW2_EMU_HAVE_ZLIB
    uLongf zl = compressBound((uLong)raw_len);
    z = (uint8_t *)malloc(zl);
    if (z && compress2(z, &zl, raw, (uLong)raw_len, 6) == Z_OK) o = zl;
    else { free(z); z = NULL; }
#endif
    if (!z) o = zlib_stored(raw, raw_len, &z);
    emu_make_parents(path);
    FILE *f = fopen(path, "wb");
    if (!f) { emu_log("screenshot: cannot write %s (%s)", path, strerror(errno)); free(raw); free(z); return -1; }
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13];
    be32(ihdr, (uint32_t)w); be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, o);
    chunk(f, "IEND", NULL, 0);
    bool ok = !ferror(f);
    if (fclose(f) != 0) ok = false;
    free(raw);
    free(z);
    if (!ok) { emu_log("screenshot: writing %s failed", path); return -1; }
    return 0;
}

static size_t zlib_stored(const uint8_t *raw, size_t raw_len, uint8_t **out) {
    size_t nblk = (raw_len + 65534) / 65535;
    size_t z_len = 2 + raw_len + nblk * 5 + 4;
    uint8_t *z = (uint8_t *)malloc(z_len);
    size_t o = 0;
    z[o++] = 0x78; z[o++] = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw_len; i++) { a = (a + raw[i]) % 65521u; b = (b + a) % 65521u; }
    for (size_t off = 0; off < raw_len; off += 65535) {
        size_t n = raw_len - off < 65535 ? raw_len - off : 65535;
        z[o++] = off + n >= raw_len ? 1 : 0;
        z[o++] = (uint8_t)n; z[o++] = (uint8_t)(n >> 8);
        z[o++] = (uint8_t)~n; z[o++] = (uint8_t)(~n >> 8);
        memcpy(z + o, raw + off, n);
        o += n;
    }
    be32(z + o, (b << 16) | a);
    o += 4;
    *out = z;
    return o;
}

int emu_screenshot(const char *path, bool full_device) {
    if (!full_device) return write_png(path, emu_lcd_pixels(), EMU_LCD_W, EMU_LCD_H);
    uint32_t *buf = (uint32_t *)malloc((size_t)EMU_SKIN_W * EMU_SKIN_H * 4);
    emu_skin_render(buf);
    int rc = write_png(path, buf, EMU_SKIN_W, EMU_SKIN_H);
    free(buf);
    return rc;
}
