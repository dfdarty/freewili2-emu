/* sensor_play.c — play a time-stamped sensor log into the sensor models.
 *
 * A CSV with a time column and any of the sensor columns. Between rows the
 * values are interpolated linearly (or held, with `step`), and the sensor
 * models ask for the current values each time the app reads a part, so the
 * app sees the log at its own read rate. At the end the last row holds, or
 * the log starts again with `loop`.
 *
 *   # comments and blank lines are ignored
 *   t, ax, ay, az, gx, gy, gz, lux
 *   0.0, 0, 0, 1, 0, 0, 0, 50000
 *   2.0, 0, 0, 1, ,  ,  ,              <- an empty cell repeats the row above
 *
 * Columns: t (seconds) or t_ms; temp, rh, lux; ax ay az (g); gx gy gz (°/s);
 * mx my mz (µT); pitch roll (°, sets the gravity vector like `set tilt`).
 * Longer spellings work too: accel_x / accel.x, gyro_x, mag_x, humidity...
 *
 * "@launch" is a built-in model-rocket flight (see LAUNCH below). */
#include "emu/emu.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

enum { C_TEMP, C_RH, C_LUX, C_AX, C_AY, C_AZ, C_GX, C_GY, C_GZ, C_MX, C_MY, C_MZ, C_PITCH, C_ROLL, NCOL };

static const struct { const char *name; int col; } ALIASES[] = {
    { "temp", C_TEMP }, { "temp_c", C_TEMP }, { "temperature", C_TEMP },
    { "rh", C_RH }, { "humidity", C_RH }, { "rh_pct", C_RH },
    { "lux", C_LUX }, { "light", C_LUX },
    { "ax", C_AX }, { "accel_x", C_AX }, { "accel.x", C_AX }, { "acc_x", C_AX },
    { "ay", C_AY }, { "accel_y", C_AY }, { "accel.y", C_AY }, { "acc_y", C_AY },
    { "az", C_AZ }, { "accel_z", C_AZ }, { "accel.z", C_AZ }, { "acc_z", C_AZ },
    { "gx", C_GX }, { "gyro_x", C_GX }, { "gyro.x", C_GX },
    { "gy", C_GY }, { "gyro_y", C_GY }, { "gyro.y", C_GY },
    { "gz", C_GZ }, { "gyro_z", C_GZ }, { "gyro.z", C_GZ },
    { "mx", C_MX }, { "mag_x", C_MX }, { "mag.x", C_MX },
    { "my", C_MY }, { "mag_y", C_MY }, { "mag.y", C_MY },
    { "mz", C_MZ }, { "mag_z", C_MZ }, { "mag.z", C_MZ },
    { "pitch", C_PITCH }, { "roll", C_ROLL },
};

/* A model rocket on a mid-size motor, with the device's z axis along the
 * rocket. The accelerometer reads what it feels: 1 g on the pad, thrust
 * during the burn (WiliBSP's ±4 g range clips it), drag deceleration while
 * coasting, the ejection kick and a tumble at apogee, swinging under the
 * parachute, landing, then lying on its side. Light flickers in the tumble. */
static const char LAUNCH[] =
    "# @launch: built-in model-rocket flight (z axis = rocket axis)\n"
    "t,    ax,   ay,   az,   gx,   gy,   gz,   lux,   temp\n"
    "0.0,  0,    0,    1,    0,    0,    0,    52000, 31\n"
    "2.0,  0,    0,    1,    0,    0,    0,    52000, 31\n"
    "2.05, 0.05, 0,    7.8,  0,    0,    15,   52000, 31\n"
    "2.4,  0.1,  -0.1, 6.9,  2,    -1,   40,   52000, 31\n"
    "3.3,  0.05, 0.05, 5.6,  1,    2,    60,   52000, 31\n"
    "3.4,  0,    0,    -0.6, 0,    0,    60,   52000, 31\n"
    "5.0,  0,    0,    -0.3, 0,    0,    45,   54000, 30.6\n"
    "8.0,  0.02, 0,    -0.12,3,    -2,   30,   56000, 30.2\n"
    "11.8, 0.05, 0.03, -0.02,8,    6,    20,   57000, 30.0\n"
    "12.0, 0.6,  -0.4, 8.5,  150,  -90,  60,   57000, 30.0\n"
    "12.1, -1.2, 2.1,  -3.0, 420,  -260, 310,  9000,  30.0\n"
    "12.3, 0.8,  -1.5, 1.2,  -380, 300,  -220, 61000, 30.0\n"
    "12.5, -0.4, 1.1,  -0.8, 250,  -410, 180,  4000,  30.0\n"
    "12.8, 1.4,  0.3,  0.5,  -150, 220,  -300, 58000, 30.0\n"
    "13.2, 0.2,  -0.3, 4.2,  60,   -40,  90,   30000, 30.0\n"
    "13.6, 0.3,  0.2,  1.3,  35,   20,   -15,  45000, 30.0\n"
    "16.0, -0.25,0.15, 1.05, -25,  15,   10,   47000, 30.1\n"
    "20.0, 0.2,  -0.2, 1.02, 20,   -18,  -8,   48000, 30.3\n"
    "25.0, -0.15,0.1,  1.03, -15,  12,   6,    49000, 30.5\n"
    "30.0, 0.12, -0.08,1.01, 12,   -10,  -5,   50000, 30.7\n"
    "35.0, -0.1, 0.06, 1.02, -10,  8,    4,    51000, 30.9\n"
    "40.0, 0.05, 0.03, 1.0,  6,    -4,   -2,   52000, 31\n"
    "40.1, 0.9,  0.4,  3.6,  -80,  60,   20,   52000, 31\n"
    "40.3, 0.95, 0.1,  0.2,  -10,  5,    0,    52000, 31\n"
    "41.0, 1.0,  0,    0,    0,    0,    0,    52000, 31\n"
    "50.0, 1.0,  0,    0,    0,    0,    0,    52000, 31\n";

static struct {
    bool     on, loop, step, finished;
    char     name[256];
    float   *rows;            /* nrows x (1 + NCOL): t, then columns */
    unsigned nrows, cursor;
    bool     has[NCOL];
    uint64_t start_us;
} P;

#define ROW(i) (&P.rows[(size_t)(i) * (1 + NCOL)])

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

static int column_of(const char *h) {
    for (size_t i = 0; i < sizeof ALIASES / sizeof ALIASES[0]; i++)
        if (!strcasecmp(h, ALIASES[i].name)) return ALIASES[i].col;
    return -1;
}

static char s_err[320];

/* Parse CSV text into rows. Returns NULL or an error message. */
static const char *parse(char *text, float **rows_out, unsigned *n_out, bool has_out[NCOL]) {
    int map[64];
    int ncols = 0;
    float time_scale = 0.0f;
    bool header = false;
    float *rows = NULL, prev[NCOL];
    unsigned n = 0, cap = 0;
    bool have[NCOL] = { false };
    int line_no = 0;
    for (char *save = NULL, *line = strtok_r(text, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        line_no++;
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        char *l = trim(line);
        if (!*l) continue;
        char *cells[64];
        int nc = 0;
        for (char *c = l;; ) {
            if (nc == 64) { snprintf(s_err, sizeof s_err, "line %d: more than 64 columns", line_no); goto fail; }
            char *comma = strchr(c, ',');
            if (comma) *comma = 0;
            cells[nc++] = trim(c);
            if (!comma) break;
            c = comma + 1;
        }
        if (!header) {
            for (int i = 0; i < nc; i++) {
                const char *h = cells[i];
                if (!strcasecmp(h, "t") || !strcasecmp(h, "time") || !strcasecmp(h, "t_s") || !strcasecmp(h, "seconds")) {
                    map[i] = -2; time_scale = 1.0f;
                } else if (!strcasecmp(h, "t_ms") || !strcasecmp(h, "ms") || !strcasecmp(h, "time_ms")) {
                    map[i] = -2; time_scale = 0.001f;
                } else if ((map[i] = column_of(h)) < 0) {
                    snprintf(s_err, sizeof s_err, "unknown column '%s' (use t or t_ms; temp rh lux; ax ay az; "
                             "gx gy gz; mx my mz; pitch roll)", h);
                    goto fail;
                } else if (has_out[map[i]]) {
                    snprintf(s_err, sizeof s_err, "column '%s' appears twice", h);
                    goto fail;
                } else has_out[map[i]] = true;
            }
            if (time_scale == 0.0f) { snprintf(s_err, sizeof s_err, "the header needs a time column: t (seconds) or t_ms"); goto fail; }
            bool a = has_out[C_AX] || has_out[C_AY] || has_out[C_AZ];
            if (a && !(has_out[C_AX] && has_out[C_AY] && has_out[C_AZ])) { snprintf(s_err, sizeof s_err, "accel needs all of ax, ay, az"); goto fail; }
            if ((has_out[C_GX] || has_out[C_GY] || has_out[C_GZ]) && !(has_out[C_GX] && has_out[C_GY] && has_out[C_GZ])) {
                snprintf(s_err, sizeof s_err, "gyro needs all of gx, gy, gz"); goto fail;
            }
            if ((has_out[C_MX] || has_out[C_MY] || has_out[C_MZ]) && !(has_out[C_MX] && has_out[C_MY] && has_out[C_MZ])) {
                snprintf(s_err, sizeof s_err, "mag needs all of mx, my, mz"); goto fail;
            }
            if (has_out[C_PITCH] != has_out[C_ROLL]) { snprintf(s_err, sizeof s_err, "tilt needs both pitch and roll"); goto fail; }
            if (a && has_out[C_PITCH]) { snprintf(s_err, sizeof s_err, "give either ax/ay/az or pitch/roll, not both"); goto fail; }
            ncols = nc;
            header = true;
            continue;
        }
        if (nc != ncols) { snprintf(s_err, sizeof s_err, "line %d: %d cells, the header has %d", line_no, nc, ncols); goto fail; }
        if (n == cap) {
            cap = cap ? cap * 2u : 256u;
            float *r = (float *)realloc(rows, (size_t)cap * (1 + NCOL) * sizeof(float));
            if (!r) { snprintf(s_err, sizeof s_err, "out of memory"); goto fail; }
            rows = r;
        }
        float *row = &rows[(size_t)n * (1 + NCOL)];
        bool have_t = false;
        for (int i = 0; i < nc; i++) {
            const char *c = cells[i];
            int col = map[i];
            if (!*c) {
                if (col == -2) { snprintf(s_err, sizeof s_err, "line %d: the time is empty", line_no); goto fail; }
                if (!have[col]) { snprintf(s_err, sizeof s_err, "line %d: empty cell with no row above to repeat", line_no); goto fail; }
                row[1 + col] = prev[col];
                continue;
            }
            char *end;
            float v = strtof(c, &end);
            if (end == c || *end || !isfinite(v)) { snprintf(s_err, sizeof s_err, "line %d: '%s' is not a number", line_no, c); goto fail; }
            if (col == -2) { row[0] = v * time_scale; have_t = true; }
            else { row[1 + col] = v; prev[col] = v; have[col] = true; }
        }
        (void)have_t;
        if (n && row[0] < rows[(size_t)(n - 1) * (1 + NCOL)]) {
            snprintf(s_err, sizeof s_err, "line %d: time goes backwards", line_no); goto fail;
        }
        n++;
    }
    if (!header) { snprintf(s_err, sizeof s_err, "no header line"); goto fail; }
    if (!n) { snprintf(s_err, sizeof s_err, "no data rows"); goto fail; }
    *rows_out = rows;
    *n_out = n;
    return NULL;
fail:
    free(rows);
    return s_err;
}

const char *emu_sensor_play(const char *path, bool loop, bool step) {
    char *text = NULL;
    if (!strcmp(path, "@launch")) {
        text = strdup(LAUNCH);
    } else {
        FILE *f = fopen(path, "rb");
        if (!f) { snprintf(s_err, sizeof s_err, "can't open %s", path); return s_err; }
        fseek(f, 0, SEEK_END);
        long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (sz < 0 || sz > 64L * 1024 * 1024) { fclose(f); snprintf(s_err, sizeof s_err, "%s is too large", path); return s_err; }
        text = (char *)malloc((size_t)sz + 1);
        if (text && fread(text, 1, (size_t)sz, f) != (size_t)sz) { free(text); text = NULL; }
        fclose(f);
        if (!text) { snprintf(s_err, sizeof s_err, "can't read %s", path); return s_err; }
        text[sz] = 0;
    }
    float *rows = NULL;
    unsigned n = 0;
    bool has[NCOL] = { false };
    const char *err = parse(text, &rows, &n, has);
    free(text);
    if (err) {
        static char msg[600];
        snprintf(msg, sizeof msg, "%s: %s", path, err);
        return msg;
    }
    free(P.rows);
    memset(&P, 0, sizeof P);
    P.rows = rows;
    P.nrows = n;
    memcpy(P.has, has, sizeof has);
    P.loop = loop;
    P.step = step;
    snprintf(P.name, sizeof P.name, "%s", path);
    P.start_us = emu_time_us();
    P.on = true;
    float dur = ROW(n - 1)[0] - ROW(0)[0];
    emu_log("sensors: playing %s (%u rows, %.1f s%s%s)", path, n, dur, loop ? ", looped" : "", step ? ", stepped" : "");
    emu_sensor_play_task();
    return NULL;
}

void emu_sensor_play_stop(void) {
    if (P.on) emu_log("sensors: playback of %s stopped", P.name);
    P.on = false;
}

void emu_sensor_play_task(void) {
    if (!P.on) return;
    float t0 = ROW(0)[0], tend = ROW(P.nrows - 1)[0];
    float t = t0 + (float)((double)(emu_time_us() - P.start_us) / 1e6);
    if (t >= tend) {
        if (P.loop && tend > t0) {
            t = t0 + fmodf(t - t0, tend - t0);
            if (t < ROW(P.cursor)[0]) P.cursor = 0;
        } else {
            t = tend;
            if (!P.finished) {
                P.finished = true;
                emu_log("sensors: playback of %s finished", P.name);
            }
        }
    }
    if (t < ROW(P.cursor)[0]) P.cursor = 0;
    while (P.cursor + 1 < P.nrows && ROW(P.cursor + 1)[0] <= t) P.cursor++;
    const float *a = ROW(P.cursor);
    const float *b = P.cursor + 1 < P.nrows ? ROW(P.cursor + 1) : a;
    float f = 0.0f;
    if (!P.step && b != a && b[0] > a[0]) f = (t - a[0]) / (b[0] - a[0]);
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    float v[NCOL];
    for (int c = 0; c < NCOL; c++) v[c] = a[1 + c] + (b[1 + c] - a[1 + c]) * f;
    if (P.has[C_TEMP]) emu_sensor_set("temp", 1, &v[C_TEMP]);
    if (P.has[C_RH]) emu_sensor_set("rh", 1, &v[C_RH]);
    if (P.has[C_LUX]) emu_sensor_set("lux", 1, &v[C_LUX]);
    if (P.has[C_AX]) emu_sensor_set("accel", 3, &v[C_AX]);
    if (P.has[C_GX]) emu_sensor_set("gyro", 3, &v[C_GX]);
    if (P.has[C_MX]) emu_sensor_set("mag", 3, &v[C_MX]);
    if (P.has[C_PITCH]) emu_sensor_set("tilt", 2, &v[C_PITCH]);
}

bool emu_sensor_playing(void) { return P.on && !P.finished; }
