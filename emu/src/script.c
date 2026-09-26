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
 *   quit
 * Buttons: GREY YELLOW GREEN BLUE RED CENTER UP DOWN LEFT RIGHT HOME OK CANCEL PAGE
 */
#include "emu/emu.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool emu_touch_get(int *x, int *y);

#define MAX_LINES 1024
static char   *s_lines[MAX_LINES];
static int     s_nlines, s_pc;
static bool    s_active;
static uint64_t s_resume_us;
static struct { bool on; int btn; uint64_t until; } s_press;
static struct { bool on; uint64_t start, until; int x1, y1, x2, y2; } s_touch;

void emu_script_load(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) emu_fatal("cannot open script %s", path);
    char buf[512];
    while (fgets(buf, sizeof buf, f) && s_nlines < MAX_LINES) {
        char *p = buf;
        while (isspace((unsigned char)*p)) p++;
        char *e = p + strlen(p);
        while (e > p && isspace((unsigned char)e[-1])) *--e = 0;
        if (!*p || *p == '#') continue;
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

static void exec(char *line, uint64_t now) {
    char raw[512];
    snprintf(raw, sizeof raw, "%s", line);
    char *save = NULL;
    char *cmd = strtok_r(line, " \t", &save);
    char *a1 = strtok_r(NULL, " \t", &save);
    char *a2 = strtok_r(NULL, " \t", &save);
    char *a3 = strtok_r(NULL, " \t", &save);
    char *a4 = strtok_r(NULL, " \t", &save);
    char *a5 = strtok_r(NULL, " \t", &save);
    int ln = s_pc - 1;
    if (!strcmp(cmd, "wait")) {
        s_resume_us = now + (uint64_t)atoll(a1 ? a1 : "0") * 1000u;
    } else if (!strcmp(cmd, "press")) {
        int b = need_btn(a1, ln);
        set_btn(b, true);
        s_press.on = true; s_press.btn = b;
        s_press.until = now + (uint64_t)(a2 ? atoi(a2) : 150) * 1000u;
        s_resume_us = s_press.until + 50000u;
    } else if (!strcmp(cmd, "hold")) {
        set_btn(need_btn(a1, ln), true);
    } else if (!strcmp(cmd, "release")) {
        set_btn(need_btn(a1, ln), false);
    } else if (!strcmp(cmd, "touch")) {
        if (!a1 || !a2) emu_fatal("script line %d: touch X Y [MS]", ln + 1);
        s_touch.on = true;
        s_touch.x1 = s_touch.x2 = atoi(a1);
        s_touch.y1 = s_touch.y2 = atoi(a2);
        s_touch.start = now;
        s_touch.until = now + (uint64_t)(a3 ? atoi(a3) : 120) * 1000u;
        emu_touch_set(s_touch.x1, s_touch.y1, true);
        s_resume_us = s_touch.until + 50000u;
    } else if (!strcmp(cmd, "drag")) {
        if (!a4) emu_fatal("script line %d: drag X1 Y1 X2 Y2 [MS]", ln + 1);
        s_touch.on = true;
        s_touch.x1 = atoi(a1); s_touch.y1 = atoi(a2);
        s_touch.x2 = atoi(a3); s_touch.y2 = atoi(a4);
        s_touch.start = now;
        s_touch.until = now + (uint64_t)(a5 ? atoi(a5) : 300) * 1000u;
        emu_touch_set(s_touch.x1, s_touch.y1, true);
        s_resume_us = s_touch.until + 50000u;
    } else if (!strcmp(cmd, "screenshot")) {
        if (!a1) emu_fatal("script line %d: screenshot FILE [lcd|device]", ln + 1);
        bool dev = a2 && !strcmp(a2, "device");
        if (emu_screenshot(a1, dev) == 0) emu_log("script: screenshot -> %s", a1);
    } else if (!strcmp(cmd, "set")) {
        float v[3];
        int n = 0;
        char *vals[3] = { a2, a3, a4 };
        for (int i = 0; i < 3 && vals[i]; i++) v[n++] = strtof(vals[i], NULL);
        if (!a1 || !emu_sensor_set(a1, n, v)) emu_fatal("script line %d: set NAME V [V V] (temp rh lux accel gyro mag tilt noise)", ln + 1);
        if (emu_verbose) { char d[160]; emu_sensor_describe(d, sizeof d); emu_log("sensors: %s", d); }
    } else if (!strcmp(cmd, "log")) {
        const char *rest = raw + 3;
        while (*rest == ' ' || *rest == '\t') rest++;
        emu_log("script: %s", rest);
    } else if (!strcmp(cmd, "quit")) {
        emu_app_exit("script quit");
    } else {
        emu_fatal("script line %d: unknown command '%s'", ln + 1, cmd);
    }
}

/* Immediate command from outside the script timeline (e.g. the web page).
 * Timed commands (press/touch/drag) still use their durations; `wait` and
 * `quit` are not accepted here. */
bool emu_script_exec_line(const char *line) {
    char buf[512];
    snprintf(buf, sizeof buf, "%s", line);
    char *p = buf;
    while (*p == ' ') p++;
    if (!*p || !strncmp(p, "wait", 4) || !strncmp(p, "quit", 4)) return false;
    static const char *const ok[] = { "press", "hold", "release", "touch", "drag", "set", "log", "screenshot" };
    bool known = false;
    for (size_t i = 0; i < sizeof ok / sizeof ok[0]; i++)
        if (!strncmp(p, ok[i], strlen(ok[i]))) known = true;
    if (!known) return false;
    exec(p, emu_time_us());
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
    while (s_active && now >= s_resume_us && !s_press.on && !s_touch.on) {
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

/* Uncompressed (stored-deflate) PNG: small code, no dependencies. */
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
    FILE *f = fopen(path, "wb");
    if (!f) { free(raw); free(z); emu_log("screenshot: cannot write %s", path); return -1; }
    static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13];
    be32(ihdr, (uint32_t)w); be32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, o);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
    return 0;
}

int emu_screenshot(const char *path, bool full_device) {
    if (!full_device) return write_png(path, emu_lcd_pixels(), EMU_LCD_W, EMU_LCD_H);
    uint32_t *buf = (uint32_t *)malloc((size_t)EMU_SKIN_W * EMU_SKIN_H * 4);
    emu_skin_render(buf);
    int rc = write_png(path, buf, EMU_SKIN_W, EMU_SKIN_H);
    free(buf);
    return rc;
}
