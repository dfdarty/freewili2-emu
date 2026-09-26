/* dev_audio.c — NAU88C10 codec (I2C 0x1A) + I2S bus (pio0 program model)
 * + host audio output + microphone sources.
 *
 * Data path, per I2S frame at the real rate fs = clk_sys / clkdiv / 128:
 *   TX: word from the SM's TX FIFO (CPU pio_sm_put) or its paced DMA
 *       channel -> left sample -> codec DAC (mute, DAC volume, speaker /
 *       headphone routing and volume) -> host speakers and/or WAV file.
 *   RX: while autopush is on, one word per frame: right slot = codec ADC
 *       (mic source + speaker bleed + noise floor). If nothing drains the
 *       4-deep RX FIFO the state machine stalls — as on hardware, where that
 *       also silences the DAC (see WiliBSP audio_i2s_duplex.c).
 * The codec sits on power zone 3 (AUDIO).
 */
#include "emu/emu.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/pio.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifndef FW2EMU_HEADLESS_ONLY
#include <SDL.h>
#endif

/* ------------------------------------------------------------- codec */
#define CODEC_ADDR 0x1A
static struct { uint16_t reg[0x80]; uint8_t ptr; } C;

static void codec_reset(void) {
    memset(C.reg, 0, sizeof C.reg);
    C.reg[0x0B] = 0x0FF;           /* DAC volume 0 dB */
    C.reg[0x36] = 0x039;           /* speaker 0 dB    */
    C.reg[0x3F] = 0x01A;           /* silicon revision (non-zero) */
}

static bool codec_present(emu_i2c_device_t *d) { (void)d; return emu_rail_on(3); }

static bool codec_write(emu_i2c_device_t *d, const uint8_t *b, size_t n, bool nostop) {
    (void)d; (void)nostop;
    if (!n) return true;
    C.ptr = (uint8_t)(b[0] >> 1);
    if (n >= 2) {
        uint16_t v = (uint16_t)(((b[0] & 1) << 8) | b[1]);
        if (C.ptr == 0x00) codec_reset();
        else if (C.ptr < 0x80 && C.ptr != 0x3F) C.reg[C.ptr] = v;
    }
    return true;
}

static bool codec_read(emu_i2c_device_t *d, uint8_t *dst, size_t n, bool nostop) {
    (void)d; (void)nostop;
    uint16_t v = C.ptr < 0x80 ? C.reg[C.ptr] : 0;
    if (n > 0) dst[0] = (uint8_t)((v >> 8) & 1);
    if (n > 1) dst[1] = (uint8_t)v;
    for (size_t i = 2; i < n; i++) dst[i] = 0;
    return true;
}

static emu_i2c_device_t s_codec = { .name = "nau88c10", .addr = CODEC_ADDR, .write = codec_write,
                                    .read = codec_read, .present = codec_present };

static float db_gain(float db) { return powf(10.0f, db / 20.0f); }

/* Output gains, recomputed per batch. */
static void codec_route(float *spk, float *hp) {
    *spk = *hp = 0.0f;
    if (!emu_rail_on(3)) return;
    if (C.reg[0x0A] & 0x40) return;                              /* DAC soft mute */
    uint16_t dv = C.reg[0x0B] & 0xFF;
    if (dv == 0) return;
    float dac = db_gain((float)((int)dv - 255) * 0.5f);
    bool amp = (C.reg[0x03] & 0x60) == 0x60;                     /* speaker drivers powered */
    if (amp && !(C.reg[0x36] & 0x40)) *spk = dac * db_gain((float)((int)(C.reg[0x36] & 0x3F) - 0x39));
    if (C.reg[0x38] & 0x0001) *hp = dac;
}

/* ------------------------------------------------------------ mic source */
static struct {
    int16_t *pcm;
    size_t   n;
    uint32_t rate;
} MIC;

static bool wav_load(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    uint8_t h[12];
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) { fclose(f); return false; }
    uint16_t fmt = 0, ch = 0, bits = 0;
    uint32_t rate = 0;
    for (;;) {
        uint8_t ck[8];
        if (fread(ck, 1, 8, f) != 8) break;
        uint32_t len = (uint32_t)ck[4] | ((uint32_t)ck[5] << 8) | ((uint32_t)ck[6] << 16) | ((uint32_t)ck[7] << 24);
        if (!memcmp(ck, "fmt ", 4)) {
            uint8_t fm[16];
            if (len < 16 || fread(fm, 1, 16, f) != 16) break;
            fmt = (uint16_t)(fm[0] | (fm[1] << 8));
            ch = (uint16_t)(fm[2] | (fm[3] << 8));
            rate = (uint32_t)fm[4] | ((uint32_t)fm[5] << 8) | ((uint32_t)fm[6] << 16) | ((uint32_t)fm[7] << 24);
            bits = (uint16_t)(fm[14] | (fm[15] << 8));
            fseek(f, (long)(len - 16 + (len & 1)), SEEK_CUR);
        } else if (!memcmp(ck, "data", 4)) {
            if (fmt != 1 || !ch || (bits != 16 && bits != 8)) break;
            size_t frames = len / (ch * (bits / 8u));
            MIC.pcm = (int16_t *)malloc(frames * sizeof(int16_t));
            for (size_t i = 0; i < frames; i++) {
                int acc = 0;
                for (int c = 0; c < ch; c++) {
                    if (bits == 16) { int16_t v; if (fread(&v, 2, 1, f) != 1) v = 0; acc += v; }
                    else { int v = fgetc(f); acc += (v - 128) << 8; }
                }
                MIC.pcm[i] = (int16_t)(acc / ch);
            }
            MIC.n = frames;
            MIC.rate = rate;
            break;
        } else {
            fseek(f, (long)(len + (len & 1)), SEEK_CUR);
        }
    }
    fclose(f);
    return MIC.n > 0;
}

void emu_audio_set_mic_wav(const char *path) {
    if (!wav_load(path)) emu_fatal("mic: cannot read %s (need 8/16-bit PCM WAV)", path);
    emu_log("mic: %s — %zu samples at %u Hz, looped", path, MIC.n, (unsigned)MIC.rate);
}

static float s_mic_level = 1.0f;
void emu_audio_set_mic_level(float g) { s_mic_level = g; }

/* A synthetic tone in the room ("set tone HZ AMP", AMP in int16 units; 0 = off). */
static double s_tone_hz;
static float  s_tone_amp;
bool emu_audio_set(const char *name, int n, const float *v) {
    if (!strcasecmp(name, "tone") && n >= 1) {
        s_tone_hz = v[0];
        s_tone_amp = n >= 2 ? v[1] : 8000.0f;
        if (s_tone_hz <= 0) s_tone_amp = 0;
        return true;
    }
    if (!strcasecmp(name, "miclevel") && n >= 1) { s_mic_level = v[0]; return true; }
    return false;
}

/* The acoustic scene at the device at time t (seconds): the --mic-wav
 * source (looped) plus speaker bleed and a noise floor. Time-based, so the
 * codec ADC and the PDM mic array hear the same sound. */
float emu_audio_scene_at(double t, float speaker_bleed, uint32_t *rng) {
    float v = 0.0f;
    if (MIC.n) v = MIC.pcm[(size_t)(t * (double)MIC.rate) % MIC.n] * s_mic_level;
    if (s_tone_amp) v += s_tone_amp * (float)sin(2.0 * M_PI * s_tone_hz * fmod(t, 1000.0));
    *rng = *rng * 1664525u + 1013904223u;
    float noise = ((float)(*rng >> 16) / 65536.0f - 0.5f) * 16.0f;   /* ~-66 dBFS floor */
    return v + speaker_bleed + noise;
}

float emu_audio_speaker_now(void);

/* ------------------------------------------------------------ host output */
static struct {
    FILE    *wav;
    uint32_t wav_frames, wav_rate;
    uint32_t dev;                 /* SDL audio device id, 0 = none */
    int      dev_rate;
    bool     enabled;             /* host speakers wanted */
    int16_t  buf[4096];
    size_t   nbuf;
} OUT;

void emu_audio_set_wav_out(const char *path) {
    OUT.wav = fopen(path, "wb");
    if (!OUT.wav) emu_fatal("audio: cannot write %s", path);
    static const uint8_t zero[44] = { 0 };
    fwrite(zero, 1, 44, OUT.wav);
}

void emu_audio_enable_host(bool on) { OUT.enabled = on; }

static void le32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

void emu_audio_finish(void) {
    if (!OUT.wav) return;
    uint8_t h[44];
    memcpy(h, "RIFF", 4); le32(h + 4, 36 + OUT.wav_frames * 2);
    memcpy(h + 8, "WAVEfmt ", 8); le32(h + 16, 16);
    h[20] = 1; h[21] = 0; h[22] = 1; h[23] = 0;               /* PCM, mono */
    le32(h + 24, OUT.wav_rate); le32(h + 28, OUT.wav_rate * 2);
    h[32] = 2; h[33] = 0; h[34] = 16; h[35] = 0;
    memcpy(h + 36, "data", 4); le32(h + 40, OUT.wav_frames * 2);
    fseek(OUT.wav, 0, SEEK_SET);
    fwrite(h, 1, 44, OUT.wav);
    fclose(OUT.wav);
    OUT.wav = NULL;
    emu_log("audio: wrote %u samples at %u Hz", (unsigned)OUT.wav_frames, (unsigned)OUT.wav_rate);
}

static void host_flush(int rate) {
    if (!OUT.nbuf) return;
    if (OUT.wav) {
        if (!OUT.wav_rate) OUT.wav_rate = (uint32_t)rate;
        fwrite(OUT.buf, 2, OUT.nbuf, OUT.wav);
        OUT.wav_frames += (uint32_t)OUT.nbuf;
    }
#ifndef FW2EMU_HEADLESS_ONLY
    if (OUT.enabled) {
        if (OUT.dev && OUT.dev_rate != rate) { SDL_CloseAudioDevice(OUT.dev); OUT.dev = 0; }
        if (!OUT.dev) {
            if (!SDL_WasInit(SDL_INIT_AUDIO)) SDL_InitSubSystem(SDL_INIT_AUDIO);
            SDL_AudioSpec want = { .freq = rate, .format = AUDIO_S16SYS, .channels = 1, .samples = 512 };
            OUT.dev = SDL_OpenAudioDevice(NULL, 0, &want, NULL, 0);
            OUT.dev_rate = rate;
            if (OUT.dev) SDL_PauseAudioDevice(OUT.dev, 0);
            else { emu_log("audio: no host output (%s)", SDL_GetError()); OUT.enabled = false; }
        }
        if (OUT.dev) {
            uint32_t queued = SDL_GetQueuedAudioSize(OUT.dev) / 2;
            if (queued < (uint32_t)rate / 5)                        /* keep <200 ms latency */
                SDL_QueueAudio(OUT.dev, OUT.buf, (uint32_t)(OUT.nbuf * 2));
        }
    }
#endif
    OUT.nbuf = 0;
}

static void host_sample(float v, int rate) {
    if (v > 32767.0f) v = 32767.0f;
    if (v < -32768.0f) v = -32768.0f;
    OUT.buf[OUT.nbuf++] = (int16_t)lroundf(v);
    if (OUT.nbuf == sizeof OUT.buf / sizeof OUT.buf[0]) host_flush(rate);
}

/* ---------------------------------------------------------------- I2S */
#define TXF 8
#define RXF 4
static struct {
    PIO      pio;
    unsigned sm;
    bool     bound;
    uint32_t tx[TXF];
    unsigned tx_n, tx_h;
    uint32_t rx[RXF];
    unsigned rx_n;
    uint64_t anchor_us, done;
    double   fs;
    bool     stalled, warned_stall;
    float    last_spk;            /* last speaker-routed sample, for mic bleed */
    float    peak_out;
    uint64_t loud_us;             /* last time something audible played */
} I;

static unsigned i2s_tx_level(emu_pio_sm_device_t *d) { (void)d; return I.tx_n; }
static void i2s_put(emu_pio_sm_device_t *d, uint32_t w) {
    (void)d;
    if (I.tx_n == TXF) return;
    I.tx[(I.tx_h + I.tx_n) % TXF] = w;
    I.tx_n++;
}
static void i2s_reset(emu_pio_sm_device_t *d, bool clear) {
    (void)d;
    if (clear) { I.tx_n = 0; I.rx_n = 0; }
    if (I.stalled && emu_verbose) emu_log("i2s: state machine restarted");
    I.stalled = false;
}

static emu_pio_sm_device_t s_i2s = { .name = "i2s_duplex", .put = i2s_put, .tx_level = i2s_tx_level, .reset = i2s_reset };

static void i2s_bind(struct pio_hw *pio, unsigned sm) {
    I.pio = pio;
    I.sm = sm;
    I.bound = true;
    I.tx_n = I.rx_n = 0;
    I.done = 0;
    I.anchor_us = 0;
    emu_pio_bind(pio->index, sm, &s_i2s);
    if (emu_verbose) emu_log("i2s: pio%u sm%u bound to the NAU88C10 model", pio->index, sm);
}

static double i2s_fs(void) {
    uint32_t cd = I.pio->sm[I.sm].clkdiv;
    double div = (double)(cd >> 16) + (double)((cd >> 8) & 0xFF) / 256.0;
    if (div < 1.0) div = 1.0;
    return (double)clock_get_hz(clk_sys) / div / 128.0;
}

static void i2s_frame(double t, float spk, float hp, int rate) {
    bool autopush = (I.pio->sm[I.sm].shiftctrl & PIO_SM0_SHIFTCTRL_AUTOPUSH_BITS) != 0;
    unsigned dreq_tx = pio_get_dreq(I.pio, I.sm, true), dreq_rx = pio_get_dreq(I.pio, I.sm, false);

    /* RX first: an undrained FIFO stops the whole state machine. */
    if (autopush) {
        while (I.rx_n && emu_dma_dreq_push(dreq_rx, &I.rx[0], 1) == 1) {
            memmove(&I.rx[0], &I.rx[1], (I.rx_n - 1) * sizeof I.rx[0]);
            I.rx_n--;
        }
        if (I.rx_n == RXF) {
            if (!I.warned_stall) {
                emu_log("i2s: RX FIFO full with autopush on and nothing draining it — the state "
                        "machine stalls (on hardware BCLK/LRCK and the DAC stop too)");
                I.warned_stall = true;
            }
            I.stalled = true;
            host_sample(0.0f, rate);
            return;
        }
        I.stalled = false;
    }

    uint32_t w = 0;
    bool have = false;
    if (I.tx_n) { w = I.tx[I.tx_h]; I.tx_h = (I.tx_h + 1) % TXF; I.tx_n--; have = true; }
    else if (emu_dma_dreq_pull(dreq_tx, &w, 1) == 1) have = true;
    float left = have ? (float)(int16_t)(w >> 16) : 0.0f;

    float s_spk = left * spk, s_hp = left * hp;
    I.last_spk = s_spk;
    float out = s_spk + s_hp;
    if (fabsf(out) > I.peak_out) I.peak_out = fabsf(out);
    if (fabsf(out) > 64.0f) I.loud_us = emu_time_us();
    host_sample(out, rate);

    if (autopush) {
        float adc = 0.0f;
        static uint32_t rng = 12345u;
        if (C.reg[0x02] & 0x01) adc = emu_audio_scene_at(t, s_spk * 0.2f, &rng);   /* ADC enabled */
        if (adc > 32767.0f) adc = 32767.0f;
        if (adc < -32768.0f) adc = -32768.0f;
        uint32_t word = (uint16_t)(int16_t)lroundf(adc);                       /* right slot */
        if (emu_dma_dreq_push(dreq_rx, &word, 1) != 1) I.rx[I.rx_n++] = word;
    }
}

void emu_audio_task(void) {
    if (!I.bound || !emu_pio_sm_enabled(I.pio->index, I.sm)) { I.anchor_us = 0; return; }
    uint64_t now = emu_time_us();
    double fs = i2s_fs();
    if (fabs(fs - I.fs) > 0.5) { I.fs = fs; I.anchor_us = 0; if (emu_verbose) emu_log("i2s: fs = %.1f Hz", fs); }
    if (!I.anchor_us) { I.anchor_us = now; I.done = 0; return; }
    uint64_t due = (uint64_t)((double)(now - I.anchor_us) * fs / 1e6);
    if (due < I.done) return;
    uint64_t n = due - I.done;
    if (n > (uint64_t)(fs / 5)) {                 /* host stalled >200 ms: drop the gap */
        I.anchor_us = now;
        I.done = 0;
        return;
    }
    float spk, hp;
    codec_route(&spk, &hp);
    int rate = (int)lround(fs);
    for (uint64_t i = 0; i < n; i++)
        i2s_frame((double)I.anchor_us / 1e6 + (double)(I.done + i) / fs, spk, hp, rate);
    I.done = due;
    host_flush(rate);
}

/* Speaker output right now (int16 units), for the PDM mics' acoustic bleed. */
float emu_audio_speaker_now(void) { return I.last_spk; }

/* status for the skin: which outputs are audibly playing — 0 none, bit0 speaker,
 * bit1 headphone jack; level 0..1 */
int emu_audio_status(float *level) {
    float spk, hp;
    codec_route(&spk, &hp);
    if (level) { *level = I.peak_out / 32768.0f; I.peak_out *= 0.8f; }
    if (!I.bound || !emu_pio_sm_enabled(I.pio->index, I.sm)) return 0;
    if (!I.loud_us || emu_time_us() - I.loud_us > 300000u) return 0;   /* route idle */
    return (spk > 0 ? 1 : 0) | (hp > 0 ? 2 : 0);
}

void emu_audio_init(void) {
    codec_reset();
    emu_i2c_attach(1, &s_codec);
    emu_pio_model_register("i2s_duplex", i2s_bind);
}
