/* dev_pdm.c — the four onboard PDM MEMS microphones, as seen by the
 * pdm_capture PIO program on the display CPU.
 *
 * Hardware: one shared MIC_CLK (PDM_CLK_HZ, 1.024 MHz), two data lines, two
 * mics per line (one answers on each clock phase). The PIO program runs 6
 * cycles per clock, shifts 4 bits in per clock — [SIG2@high, SIG1@high,
 * SIG2@low, SIG1@low] = [C, A, D, B] — and autopushes one word every 8
 * clocks into the RX FIFO, where a free-running DMA channel drains it into
 * the driver's ring.
 *
 * Model: each mic is a 2nd-order sigma-delta modulator fed with the shared
 * acoustic scene (the --mic-wav source, speaker bleed, a noise floor) plus
 * its own independent self-noise, clocked at clk_sys / clkdiv / 6. Words go
 * out through the state machine's RX DREQ exactly as the PIO would produce
 * them. With MIC_PWR (IO expander P1 bit 7) off, the data lines sit low:
 * the driver sees pure DC — the same symptom hello_mics warns about.
 *
 * Per-mic gains ("set mic.A 0.5", "set mics 1,1,0,1") let tests exercise
 * beamforming and the physical-order check. */
#include "emu/emu.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#define NMICS 4
enum { MA = 0, MB, MC, MD };           /* same indices as the driver's MIC_A..MIC_D */
#define SCENE_DIV 16u                  /* evaluate the scene every 16 PDM clocks (64 kHz) */
#define RXF 8u                         /* joined RX FIFO depth */

typedef struct { float i1, i2; uint32_t rng; } sdm_t;

static struct {
    PIO       pio;
    unsigned  sm;
    bool      bound;
    uint64_t  anchor_us, done;         /* done = PDM clocks produced since anchor */
    double    hz;
    sdm_t     sdm[NMICS];
    float     gain[NMICS];
    float     x[NMICS];                /* current input per mic, held for SCENE_DIV clocks */
    uint32_t  scene_rng;
    unsigned  fifo_n;                  /* words waiting in the RX FIFO (no DMA yet) */
    uint64_t  dropped;
    bool      powered_last, warned_off;
} P = { .gain = { 1.0f, 1.0f, 1.0f, 1.0f }, .scene_rng = 777u };   /* --sensor may set gains before init */

static void pdm_put(emu_pio_sm_device_t *d, uint32_t w) { (void)d; (void)w; }   /* TX unused */
static unsigned pdm_tx_level(emu_pio_sm_device_t *d) { (void)d; return 0; }
static void pdm_reset(emu_pio_sm_device_t *d, bool clear) { (void)d; if (clear) P.fifo_n = 0; }
static emu_pio_sm_device_t s_dev = { .name = "pdm_capture", .put = pdm_put,
                                     .tx_level = pdm_tx_level, .reset = pdm_reset };

static void pdm_bind(struct pio_hw *pio, unsigned sm) {
    P.pio = pio;
    P.sm = sm;
    P.bound = true;
    P.anchor_us = 0;
    P.done = 0;
    P.fifo_n = 0;
    memset(P.sdm, 0, sizeof P.sdm);
    for (int m = 0; m < NMICS; m++) P.sdm[m].rng = 0x9E3779B9u * (uint32_t)(m + 1);
    emu_pio_bind(pio->index, sm, &s_dev);
    if (emu_verbose) emu_log("pdm: pio%u sm%u bound to the 4-mic array model", pio->index, sm);
}

static double pdm_clock_hz(void) {
    uint32_t cd = P.pio->sm[P.sm].clkdiv;
    double div = (double)(cd >> 16) + (double)((cd >> 8) & 0xFF) / 256.0;
    if (div < 1.0) div = 1.0;
    return (double)clock_get_hz(clk_sys) / div / 6.0;
}

static bool mics_powered(void) { return emu_ioexp_output_high(1, 7); }

/* One sigma-delta step: input x in [-1, 1], returns the output bit. */
static inline int sdm_step(sdm_t *s, float x) {
    s->rng = s->rng * 1664525u + 1013904223u;
    float n = ((float)(s->rng >> 8) / 16777216.0f - 0.5f) * 1e-3f;   /* dither + self-noise */
    float y = (s->i2 >= 0.0f) ? 1.0f : -1.0f;
    s->i1 += x + n - y;
    s->i2 += s->i1 - y;
    /* keep the loop bounded if an input overdrives it */
    if (s->i1 > 4.0f) s->i1 = 4.0f; else if (s->i1 < -4.0f) s->i1 = -4.0f;
    if (s->i2 > 8.0f) s->i2 = 8.0f; else if (s->i2 < -8.0f) s->i2 = -8.0f;
    return y > 0.0f;
}

/* The scene is in int16 units; the mics' acoustic full scale maps int16 FS to
 * 0.5 of the modulator's range, so a full-scale WAV decimates to ~±8192 PCM
 * without driving the 2nd-order loop into overload. */
static void update_inputs(double t) {
    float s = emu_audio_scene_at(t, emu_audio_speaker_at(t) * 0.2f, &P.scene_rng) / 65536.0f;
    for (int m = 0; m < NMICS; m++) {
        /* independent self-noise per capsule (~-67 dBFS after decimation) */
        sdm_t *d = &P.sdm[m];
        d->rng = d->rng * 1664525u + 1013904223u;
        float self = ((float)(d->rng >> 8) / 16777216.0f - 0.5f) * 3e-3f;
        float x = s * P.gain[m] + self;
        if (x > 0.8f) x = 0.8f; else if (x < -0.8f) x = -0.8f;
        P.x[m] = x;
    }
}

static uint32_t make_word(uint64_t clk0, double t0, double hz, bool powered) {
    uint32_t w = 0;
    for (unsigned k = 0; k < 8; k++) {
        uint64_t clk = clk0 + k;
        if (powered && (clk % SCENE_DIV) == 0) update_inputs(t0 + (double)k / hz);
        int c = 0, a = 0, d = 0, b = 0;
        if (powered) {
            c = sdm_step(&P.sdm[MC], P.x[MC]);
            a = sdm_step(&P.sdm[MA], P.x[MA]);
            d = sdm_step(&P.sdm[MD], P.x[MD]);
            b = sdm_step(&P.sdm[MB], P.x[MB]);
        }
        w = (w << 4) | (uint32_t)(c << 3 | a << 2 | d << 1 | b);
    }
    return w;
}

void emu_pdm_task(void) {
    if (!P.bound || !emu_pio_sm_enabled(P.pio->index, P.sm)) { P.anchor_us = 0; return; }
    uint64_t now = emu_time_us();
    double hz = pdm_clock_hz();
    if (fabs(hz - P.hz) > 1.0) {
        P.hz = hz;
        P.anchor_us = 0;
        if (emu_verbose) emu_log("pdm: MIC_CLK = %.0f Hz (%.0f Hz PCM after /64)", hz, hz / 64.0);
    }
    if (!P.anchor_us) { P.anchor_us = now; P.done = 0; return; }

    bool powered = mics_powered();
    if (powered != P.powered_last) {
        P.powered_last = powered;
        if (emu_verbose) emu_log("pdm: mic rail %s", powered ? "on" : "off");
    }
    if (!powered && !P.warned_off) {
        emu_log("pdm: capture running with MIC_PWR (ioexp P1.7) off — the data lines read 0");
        P.warned_off = true;
    }

    uint64_t due = (uint64_t)((double)(now - P.anchor_us) * hz / 1e6) & ~(uint64_t)7;
    if (due <= P.done) return;
    if (due - P.done > (uint64_t)(hz / 5)) {          /* host stalled >200 ms: drop the gap */
        P.anchor_us = now;
        P.done = 0;
        return;
    }
    unsigned dreq = pio_get_dreq(P.pio, P.sm, false);
    double t_anchor = (double)P.anchor_us / 1e6;
    for (uint64_t clk = P.done; clk < due; clk += 8) {
        uint32_t w = make_word(clk, t_anchor + (double)clk / hz, hz, powered);
        if (emu_dma_dreq_push(dreq, &w, 1) == 1) continue;
        /* No DMA draining the FIFO: the SM stalls once 8 words are queued. */
        if (P.fifo_n < RXF) P.fifo_n++;
        else P.dropped++;
    }
    P.done = due;
}

bool emu_pdm_set(const char *name, int n, const float *v) {
    if (!strcasecmp(name, "mics") && n == NMICS) {
        for (int m = 0; m < NMICS; m++) P.gain[m] = v[m];
        return true;
    }
    int letter = (name[0] && !strncasecmp(name, "mic.", 4)) ? (name[4] & ~0x20) : 0;
    if (letter >= 'A' && letter <= 'D' && !name[5] && n == 1) {
        P.gain[letter - 'A'] = v[0];
        return true;
    }
    return emu_audio_set(name, n, v);            /* tone / miclevel */
}

void emu_pdm_describe(char *out, size_t cap) {
    snprintf(out, cap, "mics A=%.2f B=%.2f C=%.2f D=%.2f (%s)", P.gain[MA], P.gain[MB], P.gain[MC],
             P.gain[MD], mics_powered() ? "powered" : "MIC_PWR off");
}

void emu_pdm_init(void) {
    emu_pio_model_register("pdm_capture", pdm_bind);
}
