/* dev_sensors.c — register-level models of the four on-board I2C sensors.
 *
 *   SHT40   0x44  temperature / humidity   command + 6-byte CRC'd result
 *   OPT4001 0x45  ambient light            16-bit big-endian registers
 *   BMI323  0x68  accelerometer + gyro     16-bit little-endian, 2 dummy bytes on read
 *   BMM350  0x14  magnetometer             8-bit regs, 2 dummy bytes, OTP download
 *
 * All four sit on power zone 1 (SENSORS) and NAK without it. Readings come
 * from a "world" the user controls (script `set`, browser sliders, or
 * --sensor NAME=VALUES) and are encoded exactly the way the parts report them,
 * so WiliBSP's unmodified drivers decode them back to the same numbers.
 */
#include "emu/emu.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ---------------------------------------------------------------- world */
static struct {
    float temp_c, rh_pct, lux;
    float ax, ay, az;        /* g   */
    float gx, gy, gz;        /* dps */
    float mx, my, mz;        /* uT  */
    float noise;             /* 0 = exact readings */
} W = {
    .temp_c = 24.0f, .rh_pct = 45.0f, .lux = 320.0f,
    .ax = 0.0f, .ay = 0.0f, .az = 1.0f,
    .mx = 22.0f, .my = 5.0f, .mz = -40.0f,   /* roughly Florida: ~46 uT, dipping down */
};

static float jitter(float scale) {
    if (W.noise <= 0.0f) return 0.0f;
    return ((float)rand() / (float)RAND_MAX - 0.5f) * 2.0f * scale * W.noise;
}

/* orientation in degrees -> gravity vector, for a quick "tilt" */
static void set_tilt(float pitch, float roll) {
    float p = pitch * (float)M_PI / 180.0f, r = roll * (float)M_PI / 180.0f;
    W.ax = -sinf(p);
    W.ay = sinf(r) * cosf(p);
    W.az = cosf(r) * cosf(p);
}

bool emu_sensor_set(const char *name, int n, const float *v) {
    if (!strcasecmp(name, "temp") && n >= 1) W.temp_c = v[0];
    else if ((!strcasecmp(name, "rh") || !strcasecmp(name, "humidity")) && n >= 1) W.rh_pct = v[0];
    else if (!strcasecmp(name, "lux") && n >= 1) W.lux = v[0] < 0 ? 0 : v[0];
    else if (!strcasecmp(name, "accel") && n >= 3) { W.ax = v[0]; W.ay = v[1]; W.az = v[2]; }
    else if (!strcasecmp(name, "gyro") && n >= 3) { W.gx = v[0]; W.gy = v[1]; W.gz = v[2]; }
    else if (!strcasecmp(name, "mag") && n >= 3) { W.mx = v[0]; W.my = v[1]; W.mz = v[2]; }
    else if (!strcasecmp(name, "tilt") && n >= 2) set_tilt(v[0], v[1]);
    else if (!strcasecmp(name, "noise") && n >= 1) W.noise = v[0];
    else return emu_pdm_set(name, n, v);        /* mics / mic.A..mic.D gains */
    return true;
}

/* "name=v1,v2,v3" */
bool emu_sensor_set_str(const char *spec) {
    char name[32];
    const char *eq = strchr(spec, '=');
    if (!eq || eq - spec >= (long)sizeof name) return false;
    memcpy(name, spec, (size_t)(eq - spec));
    name[eq - spec] = 0;
    float v[4] = { 0, 0, 0, 0 };
    int n = 0;
    const char *p = eq + 1;
    while (n < 4 && *p) {
        char *end;
        v[n] = strtof(p, &end);
        if (end == p) break;
        n++;
        p = (*end == ',' || *end == ' ') ? end + 1 : end;
    }
    return emu_sensor_set(name, n, v);
}

void emu_sensor_describe(char *out, size_t cap) {
    snprintf(out, cap, "%.1fC %.0f%%RH %.0flux  acc %.2f %.2f %.2fg  mag %.0f %.0f %.0fuT",
             W.temp_c, W.rh_pct, W.lux, W.ax, W.ay, W.az, W.mx, W.my, W.mz);
}

static bool sensors_powered(emu_i2c_device_t *d) { (void)d; return emu_rail_on(1); }

/* ---------------------------------------------------------------- SHT40 */
static struct { uint8_t pending[6]; bool have; } SHT;

static uint8_t crc8_31(const uint8_t *d, int n) {
    uint8_t c = 0xFF;
    for (int i = 0; i < n; i++) {
        c ^= d[i];
        for (int b = 0; b < 8; b++) c = (c & 0x80) ? (uint8_t)((c << 1) ^ 0x31) : (uint8_t)(c << 1);
    }
    return c;
}

static void sht_word(uint8_t *o, uint16_t w) { o[0] = (uint8_t)(w >> 8); o[1] = (uint8_t)w; o[2] = crc8_31(o, 2); }

static uint16_t clamp_u16(float f) { return f <= 0 ? 0 : f >= 65535.0f ? 65535 : (uint16_t)lroundf(f); }

static bool sht_write(emu_i2c_device_t *d, const uint8_t *b, size_t n, bool nostop) {
    (void)d; (void)nostop;
    if (!n) return true;
    switch (b[0]) {
    case 0xFD: case 0xF6: case 0xE0: {       /* measure: high / medium / low precision */
        float t = W.temp_c + jitter(0.05f), rh = W.rh_pct + jitter(0.3f);
        sht_word(&SHT.pending[0], clamp_u16((t + 45.0f) * 65535.0f / 175.0f));
        sht_word(&SHT.pending[3], clamp_u16((rh + 6.0f) * 65535.0f / 125.0f));
        SHT.have = true;
        break;
    }
    case 0x89:                                 /* serial number */
        sht_word(&SHT.pending[0], 0x0F2E);
        sht_word(&SHT.pending[3], 0x7A51);
        SHT.have = true;
        break;
    case 0x94:                                 /* soft reset */
        SHT.have = false;
        break;
    default:
        return false;                          /* unknown command: NAK */
    }
    return true;
}

static bool sht_read(emu_i2c_device_t *d, uint8_t *dst, size_t n, bool nostop) {
    (void)d; (void)nostop;
    if (!SHT.have) return false;               /* nothing converted yet: NAK */
    for (size_t i = 0; i < n; i++) dst[i] = i < 6 ? SHT.pending[i] : 0xFF;
    SHT.have = false;
    return true;
}

static emu_i2c_device_t s_sht = { .name = "sht40", .addr = 0x44, .write = sht_write,
                                  .read = sht_read, .present = sensors_powered };

/* -------------------------------------------------------------- OPT4001 */
static struct { uint8_t ptr; uint16_t cfg; uint8_t counter; } OPT;

static uint8_t opt4001_crc(uint8_t e, uint32_t m, uint8_t c) {
    /* datasheet CRC over exponent, mantissa and counter bits */
    uint32_t bits = ((uint32_t)e << 24) | (m << 4) | c;      /* 4 + 20 + 4 bits */
    uint8_t x0 = 0, x1 = 0, x2 = 0, x3 = 0;
    for (int i = 0; i < 28; i++) x0 ^= (bits >> i) & 1;
    for (int i = 1; i < 28; i += 2) x1 ^= (bits >> i) & 1;
    for (int i = 3; i < 28; i += 4) x2 ^= (bits >> i) & 1;
    for (int i = 7; i < 28; i += 8) x3 ^= (bits >> i) & 1;
    return (uint8_t)(x0 | (x1 << 1) | (x2 << 2) | (x3 << 3));
}

static uint16_t opt_reg(uint8_t r) {
    float lux = W.lux + jitter(W.lux * 0.01f + 0.05f);
    if (lux < 0) lux = 0;
    uint64_t codes = (uint64_t)llroundf(lux / 437.5e-6f);
    uint8_t e = 0;
    while ((codes >> e) > 0xFFFFFu && e < 15) e++;
    uint32_t m = (uint32_t)(codes >> e) & 0xFFFFFu;
    switch (r) {
    case 0x00: return (uint16_t)((e << 12) | ((m >> 8) & 0x0FFF));
    case 0x01: {
        uint8_t c = OPT.counter++ & 0x0F;
        return (uint16_t)(((m & 0xFF) << 8) | (c << 4) | opt4001_crc(e, m, c));
    }
    case 0x0A: return OPT.cfg;
    case 0x11: return 0x0121;                  /* device id */
    default:   return 0;
    }
}

static bool opt_write(emu_i2c_device_t *d, const uint8_t *b, size_t n, bool nostop) {
    (void)d; (void)nostop;
    if (!n) return true;
    OPT.ptr = b[0];
    if (n >= 3 && b[0] == 0x0A) OPT.cfg = (uint16_t)((b[1] << 8) | b[2]);
    return true;
}

static bool opt_read(emu_i2c_device_t *d, uint8_t *dst, size_t n, bool nostop) {
    (void)d; (void)nostop;
    uint8_t r = OPT.ptr;
    for (size_t i = 0; i < n; i += 2, r++) {
        uint16_t v = opt_reg(r);
        dst[i] = (uint8_t)(v >> 8);
        if (i + 1 < n) dst[i + 1] = (uint8_t)v;
    }
    return true;
}

static emu_i2c_device_t s_opt = { .name = "opt4001", .addr = 0x45, .write = opt_write,
                                  .read = opt_read, .present = sensors_powered };

/* --------------------------------------------------------------- BMI323 */
static struct { uint8_t ptr; uint16_t acc_conf, gyr_conf; } IMU;

static int acc_range_g(void) { static const int r[4] = { 2, 4, 8, 16 }; return r[(IMU.acc_conf >> 4) & 3]; }
static int gyr_range_dps(void) { static const int r[5] = { 125, 250, 500, 1000, 2000 }; int i = (IMU.gyr_conf >> 4) & 7; return r[i > 4 ? 4 : i]; }

static int16_t s16(float v, float full) {
    float f = v / full * 32768.0f;
    if (f > 32767.0f) f = 32767.0f;
    if (f < -32768.0f) f = -32768.0f;
    return (int16_t)lroundf(f);
}

static uint16_t imu_reg(uint8_t r) {
    bool acc_on = ((IMU.acc_conf >> 12) & 7) != 0, gyr_on = ((IMU.gyr_conf >> 12) & 7) != 0;
    float ga = (float)acc_range_g(), gg = (float)gyr_range_dps();
    switch (r) {
    case 0x00: return 0x0043;                                   /* CHIP_ID */
    case 0x02: return 0x0000;                                   /* STATUS / ERR */
    case 0x03: return acc_on ? (uint16_t)s16(W.ax + jitter(0.004f), ga) : 0x8000;
    case 0x04: return acc_on ? (uint16_t)s16(W.ay + jitter(0.004f), ga) : 0x8000;
    case 0x05: return acc_on ? (uint16_t)s16(W.az + jitter(0.004f), ga) : 0x8000;
    case 0x06: return gyr_on ? (uint16_t)s16(W.gx + jitter(0.1f), gg) : 0x8000;
    case 0x07: return gyr_on ? (uint16_t)s16(W.gy + jitter(0.1f), gg) : 0x8000;
    case 0x08: return gyr_on ? (uint16_t)s16(W.gz + jitter(0.1f), gg) : 0x8000;
    case 0x09: return (uint16_t)(int16_t)lroundf((W.temp_c + 2.0f - 23.0f) * 512.0f);  /* die temp */
    case 0x20: return IMU.acc_conf;
    case 0x21: return IMU.gyr_conf;
    default:   return 0;
    }
}

static bool imu_write(emu_i2c_device_t *d, const uint8_t *b, size_t n, bool nostop) {
    (void)d; (void)nostop;
    if (!n) return true;
    IMU.ptr = b[0];
    if (n >= 3) {
        uint16_t v = (uint16_t)(b[1] | (b[2] << 8));
        if (b[0] == 0x20) IMU.acc_conf = v;
        else if (b[0] == 0x21) IMU.gyr_conf = v;
        else if (b[0] == 0x7E && v == 0xDEAF) { IMU.acc_conf = 0x0028; IMU.gyr_conf = 0x0048; }
    }
    return true;
}

static bool imu_read(emu_i2c_device_t *d, uint8_t *dst, size_t n, bool nostop) {
    (void)d; (void)nostop;
    for (size_t i = 0; i < n; i++) {
        if (i < 2) { dst[i] = 0; continue; }                     /* two dummy bytes */
        size_t k = i - 2;
        uint16_t v = imu_reg((uint8_t)(IMU.ptr + k / 2));
        dst[i] = (k & 1) ? (uint8_t)(v >> 8) : (uint8_t)v;
    }
    return true;
}

static emu_i2c_device_t s_imu = { .name = "bmi323", .addr = 0x68, .write = imu_write,
                                  .read = imu_read, .present = sensors_powered };

/* --------------------------------------------------------------- BMM350 */
/* The emulated part has an ideal (all-zero) OTP calibration, so WiliBSP's
 * bmm350_compensate() reduces to fixed scale factors; the model inverts those
 * (same constants as bsp/sensors/bmm350_comp.c) to hit the requested uT. */
static struct { uint8_t ptr, otp_addr; uint8_t pmu; } MAG;

static void put24(uint8_t *o, int32_t v) {
    uint32_t u = (uint32_t)v & 0xFFFFFFu;
    o[0] = (uint8_t)u; o[1] = (uint8_t)(u >> 8); o[2] = (uint8_t)(u >> 16);
}

static void mag_sample(uint8_t out[12]) {
    const float adc_gain = 1.0f / 1.5f, lut_gain = 0.714607238769531f, power = 1000000.0f / 1048576.0f;
    const float LSB_XY = power / (14.55f * 19.46f * adc_gain * lut_gain);
    const float LSB_Z  = power / (9.0f * 31.0f * adc_gain * lut_gain);
    const float LSB_T  = 1.0f / (0.00204f * adc_gain * lut_gain * 1048576.0f);
    float t = W.temp_c + 1.5f;                                  /* die a little warmer */
    float traw = (t >= 0) ? t + 25.49f : t - 25.49f;
    float mx = W.mx + jitter(0.3f), my = W.my + jitter(0.3f), mz = W.mz + jitter(0.3f);
    put24(&out[0], (int32_t)lroundf(mx / LSB_XY));
    put24(&out[3], (int32_t)lroundf(my / (LSB_XY * 1.01f)));     /* sensy = +0.01 with zero OTP */
    put24(&out[6], (int32_t)lroundf(mz * (1.0f - 0.0001f * (t - 23.0f)) / LSB_Z));
    put24(&out[9], (int32_t)lroundf(traw / LSB_T));
}

static bool mag_write(emu_i2c_device_t *d, const uint8_t *b, size_t n, bool nostop) {
    (void)d; (void)nostop;
    if (!n) return true;
    MAG.ptr = b[0];
    if (n >= 2) {
        if (b[0] == 0x50 && (b[1] & 0xE0) == 0x20) MAG.otp_addr = b[1] & 0x1F;
        if (b[0] == 0x06) MAG.pmu = b[1];
    }
    return true;
}

static bool mag_read(emu_i2c_device_t *d, uint8_t *dst, size_t n, bool nostop) {
    (void)d; (void)nostop;
    uint8_t regs[0x80];
    memset(regs, 0, sizeof regs);
    regs[0x00] = 0x33;                                          /* CHIP_ID */
    regs[0x52] = 0; regs[0x53] = 0;                             /* OTP word: ideal part */
    mag_sample(&regs[0x31]);
    for (size_t i = 0; i < n; i++) dst[i] = i < 2 ? 0 : regs[(MAG.ptr + i - 2) & 0x7F];
    return true;
}

static emu_i2c_device_t s_mag = { .name = "bmm350", .addr = 0x14, .write = mag_write,
                                  .read = mag_read, .present = sensors_powered };

void emu_sensors_init(void) {
    emu_i2c_attach(1, &s_sht);
    emu_i2c_attach(1, &s_opt);
    emu_i2c_attach(1, &s_imu);
    emu_i2c_attach(1, &s_mag);
}
