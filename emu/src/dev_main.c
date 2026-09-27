/* dev_main.c — the FREE-WILi 2 MAIN CPU, as the display CPU sees it.
 *
 * The MAIN RP2350 owns the SD card, the user GPIO header and the programmable
 * Vout. A WiliBSP app reaches it through libs/onewili over the FwGUI display
 * link: UART0 (display GPIO 0-3) at 8 Mbaud with hardware flow control. The
 * MAIN firmware is not public; this model answers at the wire level, byte for
 * byte, from what the client library, its upstream repository
 * (github.com/freewili/onewili) and the OneWili reference
 * (freewili.com/onewili) document. docs/main-link.md lists every behaviour
 * that is inferred rather than documented.
 *
 * Display -> MAIN: event frames
 *     B0 1D | len u16le | event u8 | payload[len] | cksum u16le
 *   len excludes the event byte; cksum = 16-bit sum of every preceding byte.
 *     event 24 (M_TERM_INPUT): 01 marker | count | console text
 *     event 42 (SDFS_REQUEST): one SDFS frame
 *     event 48 (POWER_ZONES):  zone mask, 3 bytes LE
 * MAIN -> display: command frames
 *     BE BA | len u16le | cmd u8 | payload[len] | cksum u16le
 *   len counts the payload; cksum = sum of sync + len + cmd + payload.
 *     cmd 0x5D: console output (responses "[path ts seq body ok]" and events)
 *     cmd 0x5E: binary WILI event frames (gpioReport)
 *     cmd 0x5F: one SDFS frame
 *
 * Link timing: MAIN -> display bytes leave at 8 Mbaud and stop while the
 * display's 32-byte RX FIFO is full (its RTS). MAIN's own receive side is a
 * 2048-byte DMA ring that never asserts RTS and, on overrun, keeps the newest
 * bytes (both per the upstream sdfs_client.c notes). MAIN handles one request
 * at a time and blocks while its reply is still going out.
 */
#include "emu/emu.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* ------------------------------------------------------------ link / timing */
#define LINK_UART        0
#define BYTE_US          1.25        /* 10 bits at 8 Mbaud */
#define DISPLAY_RX_FIFO  32u         /* display UART RX FIFO; RTS when full */
#define MAIN_RX_RING     2048u       /* fw2main kHwRxRingSize */
#define MAX_EVENT        600u        /* largest event payload accepted */
#define TEXT_CHUNK       256u        /* console bytes per 0x5D frame (inferred) */

#define EVT_SYNC0 0xB0
#define EVT_SYNC1 0x1D
#define CMD_SYNC0 0xBE
#define CMD_SYNC1 0xBA
#define EVT_TERM_INPUT  24
#define EVT_SDFS        42
#define EVT_POWER_ZONES 48
#define CMD_TEXT   0x5D
#define CMD_BINARY 0x5E
#define CMD_SDFS   0x5F

/* How long MAIN spends on a request before its reply starts (approximate;
 * the real numbers depend on the card and on what else MAIN is doing). */
#define COST_TEXT_US        150
#define COST_EVENT_US       20
#define COST_SD_META_US     1500     /* open / stat / mkdir / remove / rename / list / close */
#define COST_SD_CHUNK_US    100      /* per 96-byte read or write chunk */
#define CARD_STALL_EVERY    (16u * 1024u)   /* bytes written between card busy periods */
#define CARD_STALL_US       6000

/* ------------------------------------------------------------ SDFS wire */
/* The values fw2main is built with (sdfslib/CMakeLists.txt, mirrored by the
 * #error guard in libs/onewili/src/onewili_sd.c). Deliberately not taken from
 * the client's headers, so a client built with other values fails here too. */
#define SDFS_HDR          11u
#define SDFS_MAX_PAYLOAD  96u
#define SDFS_MAX_PATH     128u
#define SDFS_MAX_HANDLES  2
#define SDFS_NO_VERIFY    0xFFFFFFFFu

enum {
    OP_WRITE_CHUNK = 1, OP_READ_REQ = 2, OP_READ_CHUNK = 3, OP_STATUS_REQ = 4, OP_STATUS_RESP = 5,
    OP_NACK = 6, OP_STAT_REQ = 7, OP_STAT_RESP = 8, OP_LIST_REQ = 9, OP_LIST_CHUNK = 10,
    OP_MKDIR_REQ = 11, OP_REMOVE_REQ = 12, OP_RENAME_REQ = 13, OP_RESULT = 14, OP_OPEN_REQ = 15,
    OP_OPEN_RESP = 16, OP_HREAD_REQ = 17, OP_HWRITE_CHUNK = 18, OP_SEEK_REQ = 19, OP_HCLOSE_REQ = 20,
};
#define FLAG_APPEND 0x01u
#define FLAG_LAST   0x02u
enum {
    ST_OK = 0, ST_NOT_FOUND = 1, ST_IO = 2, ST_NO_SPACE = 3, ST_TOO_BIG = 4, ST_TIMEOUT = 5,
    ST_BUSY = 6, ST_BAD_REQUEST = 7, ST_NO_CARD = 8, ST_NOT_MOUNTED = 9, ST_PENDING = 100,
};

typedef struct {
    uint8_t  op;
    uint16_t req;
    uint8_t  flags;
    uint16_t seq, total;
    uint8_t  path_len;
    uint16_t payload_len;
    const uint8_t *path, *payload;
} sdfs_frame_t;

/* ------------------------------------------------------------ GPIO header */
static const uint8_t HEADER_GPIO[EMU_HEADER_PINS] = { 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 25, 26, 27 };
#define HEADER_MASK 0x0E03FF00u
/* gpio_get_all() on a bench unit with nothing on the header, GPIO 25 low
 * (apps/hello_vref/README.md: "bitfield 0xE0FF21F3"). Bits that are not
 * header pins are MAIN-internal signals and are reported as captured; idle
 * header inputs read as they did there (8, 13, 16 and 17 high). */
#define BENCH_BITFIELD 0xE0FF21F3u

enum { PIN_INPUT, PIN_OUTPUT, PIN_PWM };
typedef struct {
    uint8_t  mode;
    bool     out;          /* output latch */
    int      ext;          /* level driven from outside, -1 = none */
    double   pwm_hz, pwm_duty;
    uint64_t pwm_t0;
} hpin_t;

/* ------------------------------------------------------------ SD state */
typedef struct {
    bool     used;
    FILE    *f;
    uint8_t  mode;         /* 0 read, 1 write/truncate, 2 append */
    uint32_t wr_bytes;
    uint8_t  first_err, batch_err;
} sd_handle_t;

typedef struct {           /* stateless whole-file write in progress */
    bool     used;
    uint16_t req;
    FILE    *f;
    uint16_t next_seq;
    uint32_t bytes;
    uint8_t  status;
} sd_write_t;

typedef struct { uint16_t req; uint8_t status; uint32_t bytes; } sd_result_t;

typedef struct {           /* streaming read, one chunk per step */
    bool     active;
    bool     whole;        /* READ_REQ (own file) vs HREAD (handle) */
    uint16_t req, seq, total;
    FILE    *f;
    uint32_t remaining;    /* HREAD: bytes still allowed */
} sd_read_job_t;

/* ------------------------------------------------------------ the model */
static struct {
    emu_uart_device_t dev;

    /* display -> MAIN: DMA ring (monotonic byte counters) */
    uint8_t  ring[MAIN_RX_RING];
    uint64_t rx_total, rx_used;
    uint64_t overrun_bytes;
    struct { uint64_t end; double t; } arrivals[64];   /* when each batch of bytes was complete */
    int      narr;

    /* event-frame parser */
    int      st;
    uint16_t len, got, sum, ck;
    uint8_t  code;
    bool     overlong;
    uint8_t  pay[MAX_EVENT];
    uint64_t bad_frames;

    /* MAIN -> display */
    uint8_t *tx;
    size_t   tx_len, tx_off, tx_cap;
    double   tx_clock;                 /* when the wire is next free (us) */
    double   tx_not_before;            /* reply ready time */
    double   free_at;                  /* MAIN's virtual clock: done with the last request */

    /* console */
    char     line[1100];
    size_t   line_len;
    bool     line_overflow;
    uint32_t seq;

    /* power zones as reported by the display (event 48) */
    bool     zones_reported;
    uint32_t zone_mask;

    /* header GPIO, Vout, VREF pin */
    hpin_t   pin[EMU_HEADER_PINS];
    uint32_t stream_ms;
    double   stream_next;
    bool     vout_en;
    float    vout_set;
    float    vref_ext;                 /* volts on Trig_IN/VREF; <0 = nothing connected */

    /* SD card */
    char        root[1024];
    bool        no_card;
    bool        card_ready;
    sd_handle_t h[SDFS_MAX_HANDLES];
    sd_write_t  w[4];
    sd_result_t res[16];
    int         res_next;
    uint8_t     last_status;
    uint32_t    last_bytes;
    uint64_t    written_total;
    sd_read_job_t job;
    uint64_t    last_sd_us;
} M;

static double now_us(void) { return (double)emu_time_us(); }

/* ======================================================= MAIN -> display */
static void tx_put(const uint8_t *b, size_t n) {
    if (M.tx_off == M.tx_len) M.tx_off = M.tx_len = 0;
    if (M.tx_len + n > M.tx_cap) {
        size_t cap = M.tx_cap ? M.tx_cap : 4096;
        while (cap < M.tx_len + n) cap *= 2;
        uint8_t *p = (uint8_t *)realloc(M.tx, cap);
        if (!p) emu_fatal("main: out of memory");
        M.tx = p;
        M.tx_cap = cap;
    }
    memcpy(M.tx + M.tx_len, b, n);
    M.tx_len += n;
}

static void send_cmd(uint8_t cmd, const uint8_t *p, size_t n) {
    uint8_t h[5] = { CMD_SYNC0, CMD_SYNC1, (uint8_t)n, (uint8_t)(n >> 8), cmd };
    uint16_t sum = 0;
    for (int i = 0; i < 5; i++) sum = (uint16_t)(sum + h[i]);
    for (size_t i = 0; i < n; i++) sum = (uint16_t)(sum + p[i]);
    uint8_t ck[2] = { (uint8_t)sum, (uint8_t)(sum >> 8) };
    tx_put(h, 5);
    if (n) tx_put(p, n);
    tx_put(ck, 2);
}

static size_t tx_pending(void) { return M.tx_len - M.tx_off; }

/* Put the reply bytes that are due on the wire, as far as the display's RX
 * FIFO has room (its RTS stops MAIN otherwise). */
static void tx_pump(double now) {
    if (!tx_pending() || now < M.tx_not_before) return;
    double start = M.tx_not_before > M.tx_clock ? M.tx_not_before : M.tx_clock;
    /* After an idle or RTS-blocked spell, at most a FIFO's worth is "late". */
    if (start < now - DISPLAY_RX_FIFO * BYTE_US) start = now - DISPLAY_RX_FIFO * BYTE_US;
    size_t level = emu_uart_rx_level(LINK_UART);
    size_t room = level < DISPLAY_RX_FIFO ? DISPLAY_RX_FIFO - level : 0;
    size_t due = now > start ? (size_t)((now - start) / BYTE_US) : 0;
    size_t n = tx_pending();
    if (n > room) n = room;
    if (n > due) n = due;
    if (n) {
        emu_uart_to_mcu(LINK_UART, M.tx + M.tx_off, n);
        M.tx_off += n;
        start += (double)n * BYTE_US;
    }
    M.tx_clock = start;
    if (!tx_pending()) {
        M.tx_off = M.tx_len = 0;
        if (M.free_at < M.tx_clock) M.free_at = M.tx_clock;   /* MAIN was blocked on the write */
    }
}

/* ======================================================= console output */
static void console_out(const char *s, size_t n) {
    while (n) {
        size_t k = n > TEXT_CHUNK ? TEXT_CHUNK : n;
        send_cmd(CMD_TEXT, (const uint8_t *)s, k);
        s += k;
        n -= k;
    }
}

/* "[<path> <ts> <seq> <body> <ok>]" (rpConsole::printMenuResponse). */
static void respond(const char *path, bool ok, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static void respond(const char *path, bool ok, const char *fmt, ...) {
    char body[512], out[700];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof body, fmt, ap);
    va_end(ap);
    /* MAIN boots before the display app starts; its clock is ahead of ours. */
    uint64_t ts_ns = (emu_time_us() + 4000000u) * 1000u;
    int n = snprintf(out, sizeof out, "[%s %016llX %u %s %d]\n", path, (unsigned long long)ts_ns,
                     (unsigned)++M.seq, body, ok ? 1 : 0);
    if (n > (int)sizeof out - 1) n = (int)sizeof out - 1;
    console_out(out, (size_t)n);
    if (emu_verbose) emu_log("main: %.*s", n - 1, out);
}

/* ======================================================= header GPIO */
static int header_index(long gpio) {
    for (int i = 0; i < EMU_HEADER_PINS; i++) if (HEADER_GPIO[i] == gpio) return i;
    return -1;
}

static bool pin_level(int i, double now) {
    const hpin_t *p = &M.pin[i];
    if (p->mode == PIN_OUTPUT) return p->out;
    if (p->mode == PIN_PWM) {
        if (p->pwm_duty <= 0) return false;
        if (p->pwm_duty >= 100) return true;
        double ph = fmod((now - (double)p->pwm_t0) * 1e-6 * p->pwm_hz, 1.0);
        return ph < p->pwm_duty / 100.0;
    }
    if (p->ext >= 0) return p->ext != 0;
    return (BENCH_BITFIELD >> HEADER_GPIO[i]) & 1u;
}

static uint32_t gpio_bitfield(void) {
    double now = now_us();
    uint32_t v = BENCH_BITFIELD & ~HEADER_MASK;
    for (int i = 0; i < EMU_HEADER_PINS; i++)
        if (pin_level(i, now)) v |= 1u << HEADER_GPIO[i];
    return v;
}

float emu_main_vout(void) {
    /* The Vout regulator sits behind the Analog power zone (11). */
    return M.vout_en && emu_rail_on(11) ? M.vout_set : 0.0f;
}

/* The header's level-shifter rail, as selected on the display I/O expander
 * (PCAL6524 port 2). Rail values are those measured on a bench unit
 * (docs/superpowers/findings/2026-07-26-gpio-vref-e2e.md in WiliBSP),
 * including ~4.8 V on the external pin with nothing connected. */
float emu_main_vio(void) {
    float v = 0.0f;
    if (emu_ioexp_output_high(2, 6)) v = fmaxf(v, 3.27f);
    if (emu_ioexp_output_high(2, 5)) v = fmaxf(v, 4.83f);
    if (emu_ioexp_output_high(2, 3)) v = fmaxf(v, M.vref_ext >= 0 ? M.vref_ext : 4.80f);
    if (emu_ioexp_output_high(2, 4)) v = fmaxf(v, emu_main_vout());
    return v;
}

/* Display-CPU ADC inputs (GPIO 40+n): Vout monitor on input 1 and VIO monitor
 * on input 5, each behind a 2:1 divider; ~12 mV of offset at every pin (a
 * grounded divider reads ~25 mV on hardware). */
float emu_adc_input_volts(unsigned input) {
    const float offset = 0.0125f;
    switch (input) {
    case 1: return offset + emu_main_vout() * 0.5f;
    case 5: return offset + emu_main_vio() * 0.5f;
    case 8: return 0.706f;                          /* RP2350 temperature sensor at ~27 C */
    default: return offset;
    }
}

int emu_main_header(emu_header_pin_t out[EMU_HEADER_PINS]) {
    double now = now_us();
    for (int i = 0; i < EMU_HEADER_PINS; i++) {
        out[i].gpio = HEADER_GPIO[i];
        out[i].output = M.pin[i].mode != PIN_INPUT;
        out[i].pwm = M.pin[i].mode == PIN_PWM;
        out[i].level = pin_level(i, now);
        out[i].ext = M.pin[i].mode == PIN_INPUT && M.pin[i].ext >= 0;
    }
    return EMU_HEADER_PINS;
}

bool emu_main_sd_active(void) { return M.last_sd_us && emu_time_us() - M.last_sd_us < 150000u; }

bool emu_main_set(const char *name, int n, const float *v) {
    if (!strcasecmp(name, "vrefext") && n >= 1) { M.vref_ext = v[0]; return true; }
    if (!strncasecmp(name, "gpio", 4) && isdigit((unsigned char)name[4]) && n >= 1) {
        int i = header_index(strtol(name + 4, NULL, 10));
        if (i < 0) return false;
        M.pin[i].ext = v[0] < 0 ? -1 : v[0] != 0;
        if (emu_verbose) emu_log("main: header GPIO %s %s", name + 4,
                                 M.pin[i].ext < 0 ? "released" : M.pin[i].ext ? "driven high" : "driven low");
        return true;
    }
    return false;
}

/* gpioReport binary event (header type 0, 12-byte payload) for i\g\o. */
static void send_gpio_report(void) {
    uint8_t f[12 + 12];
    uint64_t ts = (emu_time_us() + 4000000u) * 1000u;
    uint32_t bits = gpio_bitfield();
    memcpy(f, "WILI", 4);
    f[4] = 1; f[5] = 0;                 /* repeat count (inferred) */
    f[6] = 0; f[7] = 0;                 /* header type 0 = gpioReport */
    f[8] = 12; f[9] = 0; f[10] = 0; f[11] = 0;
    for (int i = 0; i < 8; i++) f[12 + i] = (uint8_t)(ts >> (8 * i));
    for (int i = 0; i < 4; i++) f[20 + i] = (uint8_t)(bits >> (8 * i));
    send_cmd(CMD_BINARY, f, sizeof f);
}

/* ======================================================= console commands */
static const char *const ZONE_NAMES[18] = {
    "", "Sensors", "Display", "Audio", "Sub-GHz", "ESP32", "FPGA", "SD_Card", "USB_Hub", "Board_LED",
    "LEDs", "Analog", "Aux", "NFC/RFID", "FTDI", "CAN", "Debug_CPU", "CM0",
};

/* Documented EPOWERZONE refusal: only once the display has reported zones. */
static bool zone_refused(const char *path, unsigned zone) {
    if (!M.zones_reported || (M.zone_mask & (1u << (zone - 1)))) return false;
    respond(path, false, "EPOWERZONE %u %s", zone, ZONE_NAMES[zone]);
    return true;
}

static bool arg_long(char **cur, long *out) {
    char *t = strtok_r(NULL, " \t", cur);
    if (!t) return false;
    char *end;
    *out = strtol(t, &end, 10);
    return *end == 0;
}

static bool arg_double(char **cur, double *out) {
    char *t = strtok_r(NULL, " \t", cur);
    if (!t) return false;
    char *end;
    *out = strtod(t, &end);
    return *end == 0;
}

static void cmd_gpio_set(const char *path, char **cur, char op) {
    long pin;
    if (!arg_long(cur, &pin)) { respond(path, false, "Invalid argument"); return; }
    int i = header_index(pin);
    if (i < 0) { respond(path, false, "Invalid pin %ld", pin); return; }
    hpin_t *p = &M.pin[i];
    bool was = pin_level(i, now_us());
    p->mode = PIN_OUTPUT;
    p->out = op == 's' ? true : op == 'l' ? false : !was;
    if (emu_verbose) emu_log("main: header GPIO %ld -> %d", pin, p->out);
    respond(path, true, "Ok");
}

static void cmd_gpio_pwm(const char *path, char **cur) {
    long pin;
    double hz, duty;
    if (!arg_long(cur, &pin) || !arg_double(cur, &hz) || !arg_double(cur, &duty)) {
        respond(path, false, "Invalid argument");
        return;
    }
    int i = header_index(pin);
    if (i < 0 || hz <= 0 || duty < 0 || duty > 100) { respond(path, false, "Invalid pin or PWM setting"); return; }
    M.pin[i].mode = PIN_PWM;
    M.pin[i].pwm_hz = hz;
    M.pin[i].pwm_duty = duty;
    M.pin[i].pwm_t0 = emu_time_us();
    respond(path, true, "Ok");
}

static void run_command(char *line) {
    char *cur = NULL;
    char *path = strtok_r(line, " \t", &cur);
    if (!path) return;
    if (!strcmp(path, "i\\g\\s") || !strcmp(path, "i\\g\\l") || !strcmp(path, "i\\g\\t")) {
        if (!zone_refused(path, 6)) cmd_gpio_set(path, &cur, path[4]);
    } else if (!strcmp(path, "i\\g\\p")) {
        if (!zone_refused(path, 6)) cmd_gpio_pwm(path, &cur);
    } else if (!strcmp(path, "i\\g\\u")) {
        if (!zone_refused(path, 6)) respond(path, true, "%04X", (unsigned)gpio_bitfield());
    } else if (!strcmp(path, "i\\g\\o")) {
        long ms;
        if (zone_refused(path, 6)) return;
        if (!arg_long(&cur, &ms)) { respond(path, false, "Invalid argument"); return; }
        M.stream_ms = ms <= 0 ? 0 : (uint32_t)ms;
        M.stream_next = now_us() + M.stream_ms * 1000.0;
        respond(path, true, "Ok");
    } else if (!strcmp(path, "i\\g\\v")) {
        /* MAIN forwards the selection to the display firmware's expander
         * driver; a WiliBSP app does not listen for it, so nothing changes
         * (use ioexp_vref() on the display side). */
        long src;
        if (zone_refused(path, 1)) return;
        if (!arg_long(&cur, &src) || src < 0 || src > 4) { respond(path, false, "Invalid argument"); return; }
        if (emu_verbose) emu_log("main: i\\g\\v %ld is carried out by the display firmware, not by a WiliBSP app", src);
        respond(path, true, "Ok");
    } else if (!strcmp(path, "i\\a\\u")) {
        long en;
        double volts = 0;
        if (zone_refused(path, 11)) return;
        if (!arg_long(&cur, &en)) { respond(path, false, "Invalid argument"); return; }
        if (en && (!arg_double(&cur, &volts) || volts < 1.0 || volts > 5.5)) {
            respond(path, false, "Voltage must be 1.0 - 5.5 V");
            return;
        }
        M.vout_en = en != 0;
        if (en) M.vout_set = (float)volts;
        if (emu_verbose) emu_log("main: programmable Vout %s %.2f V", M.vout_en ? "on" : "off", M.vout_set);
        respond(path, true, "Ok");
    } else {
        /* Not modelled (or not a command at all): answer with a well-formed
         * failure so the caller gets OW_ERR_FAILED instead of a 5 s timeout.
         * Said once per command without -v, every time with it. */
        static char seen[16][24];
        static int nseen;
        bool first = true;
        for (int i = 0; i < nseen; i++) if (!strncmp(seen[i], path, sizeof seen[i] - 1)) first = false;
        if (first && nseen < 16) snprintf(seen[nseen++], sizeof seen[0], "%s", path);
        if (first || emu_verbose)
            emu_log("main: OneWili command '%s' is not modelled by the emulator (answered with a failure)", path);
        respond(path, false, "Not supported by the FREE-WILi 2 emulator");
    }
}

static void console_input(const uint8_t *b, size_t n) {
    for (size_t i = 0; i < n; i++) {
        uint8_t c = b[i];
        if (c == 0x02) { M.line_len = 0; M.line_overflow = false; continue; }   /* root + quiet mode */
        if (c == '\r' || c == '\n') {
            if (M.line_overflow) {
                emu_log("main: console line longer than %zu bytes dropped", sizeof M.line - 1);
            } else if (M.line_len) {
                M.line[M.line_len] = 0;
                if (emu_verbose > 1) emu_log("main: console '%s'", M.line);
                run_command(M.line);
            }
            M.line_len = 0;
            M.line_overflow = false;
            continue;
        }
        if (M.line_len + 1 < sizeof M.line) M.line[M.line_len++] = (char)c;
        else M.line_overflow = true;
    }
}

/* ======================================================= SD card (SDFS) */
static void sd_send(uint8_t op, uint16_t req, uint8_t flags, uint16_t seq, uint16_t total,
                    const uint8_t *payload, uint16_t plen) {
    uint8_t f[SDFS_HDR + SDFS_MAX_PAYLOAD];
    f[0] = op;
    f[1] = (uint8_t)req; f[2] = (uint8_t)(req >> 8);
    f[3] = flags;
    f[4] = (uint8_t)seq; f[5] = (uint8_t)(seq >> 8);
    f[6] = (uint8_t)total; f[7] = (uint8_t)(total >> 8);
    f[8] = 0;
    f[9] = (uint8_t)plen; f[10] = (uint8_t)(plen >> 8);
    if (plen) memcpy(f + SDFS_HDR, payload, plen);
    send_cmd(CMD_SDFS, f, SDFS_HDR + plen);
}

static void sd_status_only(uint8_t op, uint16_t req, uint8_t status) { sd_send(op, req, 0, 0, 0, &status, 1); }
static void sd_nack(uint16_t req, uint8_t status) { sd_status_only(OP_NACK, req, status); }
static void sd_result(uint16_t req, uint8_t status) { sd_status_only(OP_RESULT, req, status); }

static void put32(uint8_t *p, uint32_t v) { for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i)); }
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool mkdir_p(const char *dir) {
    char tmp[1200];
    snprintf(tmp, sizeof tmp, "%s", dir);
    for (char *p = tmp + 1; *p; p++)
        if (*p == '/') { *p = 0; mkdir(tmp, 0777); *p = '/'; }
    return mkdir(tmp, 0777) == 0 || errno == EEXIST;
}

static void write_sample(const char *rel, const char *text) {
    char p[1100];
    snprintf(p, sizeof p, "%s/%s", M.root, rel);
    FILE *f = fopen(p, "wb");
    if (f) { fputs(text, f); fclose(f); }
}

/* The card folder is created (with a few sample files) the first time an app
 * touches the card, so apps that never use it leave no folder behind. */
static bool card_present(void) {
    if (M.no_card) return false;
    if (!emu_rail_on(7)) return false;               /* SD Card power zone off */
    if (M.card_ready) return true;
    struct stat st;
    if (stat(M.root, &st) != 0) {
        char sub[1100];
        snprintf(sub, sizeof sub, "%s/samples", M.root);
        if (!mkdir_p(sub)) {
            emu_log("main: cannot create SD card folder %s (%s); card reads as missing", M.root, strerror(errno));
            M.no_card = true;
            return false;
        }
        write_sample("README.TXT",
                     "Emulated FREE-WILi 2 SD card.\n"
                     "This folder stands in for the card in the MAIN CPU's slot.\n"
                     "Apps reach it with ow_sd_* (libs/onewili) as \"/...\" paths.\n");
        write_sample("samples/hello.txt", "Hello from the emulated SD card!\n");
        write_sample("samples/readings.csv", "time_s,temp_c,lux\n0,23.5,310\n1,23.6,312\n2,23.6,309\n");
        emu_log("main: created SD card folder %s with sample files", M.root);
    } else if (!S_ISDIR(st.st_mode)) {
        emu_log("main: SD card path %s is not a folder; card reads as missing", M.root);
        M.no_card = true;
        return false;
    }
    M.card_ready = true;
    return true;
}

static bool fat_name_ok(const char *s, size_t n) {
    if (!n || (n == 1 && s[0] == '.') || (n == 2 && s[0] == '.' && s[1] == '.')) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c < 0x20 || strchr("\"*:<>?|\\", c)) return false;
    }
    return true;
}

/* Map an SD path ("/dir/file") to the host folder. FAT names are case
 * insensitive, so each existing component is matched without regard to case.
 * Returns ST_OK, ST_BAD_REQUEST (malformed path) or ST_NOT_FOUND (a parent
 * folder is missing); *exists says whether the last component exists. */
static int sd_resolve(const uint8_t *path, size_t n, char *out, size_t cap, bool *exists) {
    char p[SDFS_MAX_PATH + 1];
    if (n == 0 || n > SDFS_MAX_PATH || path[0] != '/') return ST_BAD_REQUEST;
    memcpy(p, path, n);
    p[n] = 0;
    if (strlen(p) != n) return ST_BAD_REQUEST;       /* embedded NUL */
    snprintf(out, cap, "%s", M.root);
    *exists = true;
    char *save = NULL;
    for (char *c = strtok_r(p, "/", &save); c; c = strtok_r(NULL, "/", &save)) {
        if (!fat_name_ok(c, strlen(c))) return ST_BAD_REQUEST;
        if (!*exists) return ST_NOT_FOUND;           /* a parent is missing */
        size_t len = strlen(out);
        snprintf(out + len, cap - len, "/%s", c);
        struct stat st;
        if (stat(out, &st) == 0) continue;
        out[len] = 0;
        DIR *d = opendir(out);
        bool found = false;
        if (d) {
            struct dirent *e;
            while ((e = readdir(d)) != NULL)
                if (!strcasecmp(e->d_name, c)) {
                    snprintf(out + len, cap - len, "/%s", e->d_name);
                    found = true;
                    break;
                }
            closedir(d);
        }
        if (!found) {
            snprintf(out + len, cap - len, "/%s", c);
            *exists = false;
        }
    }
    return ST_OK;
}

static bool is_dir(const char *p) { struct stat st; return stat(p, &st) == 0 && S_ISDIR(st.st_mode); }

static void remember_result(uint16_t req, uint8_t status, uint32_t bytes) {
    M.res[M.res_next] = (sd_result_t){ req, status, bytes };
    M.res_next = (M.res_next + 1) % (int)(sizeof M.res / sizeof M.res[0]);
    M.last_status = status;
    M.last_bytes = bytes;
}

static void account_write(size_t n, double *cost) {
    uint64_t before = M.written_total;
    M.written_total += n;
    if (before / CARD_STALL_EVERY != M.written_total / CARD_STALL_EVERY) {
        *cost += CARD_STALL_US;                       /* the card goes busy (allocation / flush) */
        if (emu_verbose > 1) emu_log("main: SD card busy %d us", CARD_STALL_US);
    }
}

static sd_handle_t *handle_of(uint8_t h) { return h < SDFS_MAX_HANDLES && M.h[h].used ? &M.h[h] : NULL; }

/* READ_CHUNK / HREAD streaming: one chunk per call. */
static void read_job_step(double *cost) {
    sd_read_job_t *j = &M.job;
    uint8_t buf[SDFS_MAX_PAYLOAD];
    size_t want = SDFS_MAX_PAYLOAD;
    if (!j->whole && j->remaining < want) want = j->remaining;
    size_t got = want ? fread(buf, 1, want, j->f) : 0;
    bool last;
    if (j->whole) last = (uint16_t)(j->seq + 1) >= j->total || got < want;
    else {
        j->remaining -= (uint32_t)got;
        last = j->remaining == 0 || got < want;
    }
    sd_send(OP_READ_CHUNK, j->req, last ? FLAG_LAST : 0, j->seq, j->whole ? j->total : 0, buf, (uint16_t)got);
    j->seq++;
    *cost += COST_SD_CHUNK_US;
    if (last) {
        if (j->whole) fclose(j->f);
        j->active = false;
    }
}

static void sd_list(uint16_t req, const char *dir) {
    DIR *d = opendir(dir);
    if (!d) { sd_nack(req, ST_NOT_FOUND); return; }
    char **names = NULL;
    size_t nn = 0, cap = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (nn == cap) {
            cap = cap ? cap * 2 : 32;
            char **p = (char **)realloc(names, cap * sizeof *names);
            if (!p) break;
            names = p;
        }
        names[nn++] = strdup(e->d_name);
    }
    closedir(d);
    /* FAT lists in directory order; the host's order is arbitrary, so sort. */
    for (size_t i = 1; i < nn; i++)
        for (size_t k = i; k > 0 && strcasecmp(names[k - 1], names[k]) > 0; k--) {
            char *t = names[k]; names[k] = names[k - 1]; names[k - 1] = t;
        }
    uint8_t chunk[SDFS_MAX_PAYLOAD];
    size_t used = 0;
    uint16_t seq = 0;
    for (size_t i = 0; i < nn; i++) {
        char full[1700];
        struct stat st;
        snprintf(full, sizeof full, "%s/%s", dir, names[i]);
        bool dirent_is_dir = stat(full, &st) == 0 && S_ISDIR(st.st_mode);
        size_t nl = strlen(names[i]);
        if (nl > SDFS_MAX_PAYLOAD - 6) nl = SDFS_MAX_PAYLOAD - 6;   /* one entry must fit one chunk */
        if (used + 6 + nl > SDFS_MAX_PAYLOAD) {
            sd_send(OP_LIST_CHUNK, req, 0, seq++, 0, chunk, (uint16_t)used);
            used = 0;
        }
        chunk[used] = dirent_is_dir ? 1 : 0;
        put32(chunk + used + 1, dirent_is_dir ? 0 : (uint32_t)st.st_size);
        chunk[used + 5] = (uint8_t)nl;
        memcpy(chunk + used + 6, names[i], nl);
        used += 6 + nl;
        free(names[i]);
    }
    free(names);
    sd_send(OP_LIST_CHUNK, req, FLAG_LAST, seq, 0, chunk, (uint16_t)used);
}

static void sd_request(const uint8_t *b, size_t n, double *cost) {
    if (n < SDFS_HDR) { emu_log("main: SDFS frame too short (%zu bytes) dropped", n); return; }
    sdfs_frame_t r = {
        .op = b[0], .req = (uint16_t)(b[1] | b[2] << 8), .flags = b[3],
        .seq = (uint16_t)(b[4] | b[5] << 8), .total = (uint16_t)(b[6] | b[7] << 8),
        .path_len = b[8], .payload_len = (uint16_t)(b[9] | b[10] << 8),
    };
    if (r.path_len > SDFS_MAX_PATH || r.payload_len > SDFS_MAX_PAYLOAD ||
        SDFS_HDR + r.path_len + r.payload_len != n) {
        emu_log("main: malformed SDFS frame (op %u) dropped", r.op);
        return;
    }
    r.path = b + SDFS_HDR;
    r.payload = r.path + r.path_len;
    M.last_sd_us = emu_time_us();
    if (emu_verbose > 1) emu_log("main: SDFS op %u req %u seq %u path '%.*s' payload %u", r.op, r.req, r.seq,
                                 r.path_len, (const char *)r.path, r.payload_len);

    bool card = card_present();
    char host[1400], host2[1400];
    bool exists = false;
    int st;
    *cost += COST_SD_META_US;

    if (r.op == OP_STATUS_REQ) {
        uint8_t p[7] = { ST_PENDING, card, card };
        uint32_t bytes = 0;
        if (r.req == 0) { p[0] = M.last_status; bytes = M.last_bytes; }
        else
            for (size_t i = 0; i < sizeof M.res / sizeof M.res[0]; i++)
                if (M.res[i].req == r.req) { p[0] = M.res[i].status; bytes = M.res[i].bytes; }
        put32(p + 3, bytes);
        sd_send(OP_STATUS_RESP, r.req, 0, 0, 0, p, 7);
        return;
    }
    if (!card) {
        if (r.op == OP_WRITE_CHUNK || r.op == OP_HWRITE_CHUNK) {
            if (r.op == OP_WRITE_CHUNK && (r.flags & FLAG_LAST)) remember_result(r.req, ST_NO_CARD, 0);
            if (r.op == OP_HWRITE_CHUNK && (r.flags & FLAG_LAST)) sd_nack(r.req, ST_NO_CARD);
            return;
        }
        sd_nack(r.req, ST_NO_CARD);
        return;
    }

    switch (r.op) {
    case OP_WRITE_CHUNK: {                            /* fire-and-forget whole-file write */
        *cost += COST_SD_CHUNK_US - COST_SD_META_US;
        sd_write_t *w = NULL;
        for (size_t i = 0; i < sizeof M.w / sizeof M.w[0]; i++)
            if (M.w[i].used && M.w[i].req == r.req) w = &M.w[i];
        if (r.seq == 0) {
            if (w) { if (w->f) fclose(w->f); w->used = false; }
            for (size_t i = 0; i < sizeof M.w / sizeof M.w[0] && !w; i++) if (!M.w[i].used) w = &M.w[i];
            if (!w) { emu_log("main: too many concurrent SD writes; req %u dropped", r.req); return; }
            *w = (sd_write_t){ .used = true, .req = r.req };
            *cost += COST_SD_META_US;
            st = sd_resolve(r.path, r.path_len, host, sizeof host, &exists);
            if (st == ST_OK && exists && is_dir(host)) st = ST_IO;
            if (st == ST_OK) {
                w->f = fopen(host, (r.flags & FLAG_APPEND) ? "ab" : "wb");
                if (!w->f) st = ST_IO;
            }
            w->status = (uint8_t)st;
        } else if (!w) {
            if (emu_verbose) emu_log("main: SDFS write chunk for unknown req %u dropped", r.req);
            return;
        } else if (r.seq != w->next_seq && w->status == ST_OK) {
            w->status = ST_BAD_REQUEST;               /* lost chunk */
        }
        if (w->status == ST_OK && r.payload_len) {
            size_t k = fwrite(r.payload, 1, r.payload_len, w->f);
            w->bytes += (uint32_t)k;
            if (k != r.payload_len) w->status = ST_IO;
            account_write(k, cost);
        }
        w->next_seq = (uint16_t)(r.seq + 1);
        if (r.flags & FLAG_LAST) {
            if (w->f && fclose(w->f) != 0 && w->status == ST_OK) w->status = ST_IO;
            remember_result(r.req, w->status, w->bytes);
            w->used = false;
        }
        return;
    }
    case OP_READ_REQ: {
        st = sd_resolve(r.path, r.path_len, host, sizeof host, &exists);
        if (st != ST_OK) { sd_nack(r.req, (uint8_t)st); return; }
        if (!exists || is_dir(host)) { sd_nack(r.req, ST_NOT_FOUND); return; }
        FILE *f = fopen(host, "rb");
        if (!f) { sd_nack(r.req, ST_IO); return; }
        struct stat sb;
        fstat(fileno(f), &sb);
        uint64_t chunks = ((uint64_t)sb.st_size + SDFS_MAX_PAYLOAD - 1) / SDFS_MAX_PAYLOAD;
        if (chunks > 0xFFFF) { fclose(f); sd_nack(r.req, ST_TOO_BIG); return; }
        M.job = (sd_read_job_t){ .active = true, .whole = true, .req = r.req,
                                 .total = (uint16_t)(chunks ? chunks : 1), .f = f };
        return;
    }
    case OP_STAT_REQ: {
        uint8_t p[6] = { 0 };
        st = sd_resolve(r.path, r.path_len, host, sizeof host, &exists);
        struct stat sb;
        if (st == ST_OK && (!exists || stat(host, &sb) != 0)) st = ST_NOT_FOUND;
        p[0] = (uint8_t)st;
        if (st == ST_OK) {
            p[1] = S_ISDIR(sb.st_mode) ? 1 : 0;
            put32(p + 2, p[1] ? 0 : (uint32_t)sb.st_size);
        }
        sd_send(OP_STAT_RESP, r.req, 0, 0, 0, p, 6);
        return;
    }
    case OP_LIST_REQ:
        st = sd_resolve(r.path, r.path_len, host, sizeof host, &exists);
        if (st != ST_OK) { sd_nack(r.req, (uint8_t)st); return; }
        if (!exists || !is_dir(host)) { sd_nack(r.req, ST_NOT_FOUND); return; }
        sd_list(r.req, host);
        return;
    case OP_MKDIR_REQ:
        st = sd_resolve(r.path, r.path_len, host, sizeof host, &exists);
        if (st == ST_OK && exists) st = ST_IO;                     /* FR_EXIST */
        if (st == ST_OK && mkdir(host, 0777) != 0) st = ST_IO;
        sd_result(r.req, (uint8_t)st);
        return;
    case OP_REMOVE_REQ:
        st = sd_resolve(r.path, r.path_len, host, sizeof host, &exists);
        if (st == ST_OK && !strcmp(host, M.root)) st = ST_BAD_REQUEST;
        if (st == ST_OK && !exists) st = ST_NOT_FOUND;
        if (st == ST_OK && (is_dir(host) ? rmdir(host) : unlink(host)) != 0) st = ST_IO;   /* FR_DENIED: not empty */
        sd_result(r.req, (uint8_t)st);
        return;
    case OP_RENAME_REQ: {
        bool dst_exists = false;
        st = sd_resolve(r.path, r.path_len, host, sizeof host, &exists);
        if (st == ST_OK && !exists) st = ST_NOT_FOUND;
        if (st == ST_OK) st = sd_resolve(r.payload, r.payload_len, host2, sizeof host2, &dst_exists);
        if (st == ST_OK && dst_exists) st = ST_IO;                 /* FR_EXIST */
        if (st == ST_OK && rename(host, host2) != 0) st = ST_IO;
        sd_result(r.req, (uint8_t)st);
        return;
    }
    case OP_OPEN_REQ: {
        uint8_t p[2] = { ST_OK, 0xFF };
        int slot = -1;
        uint8_t mode = r.payload_len == 1 ? r.payload[0] : 0xFF;
        for (int i = 0; i < SDFS_MAX_HANDLES && slot < 0; i++) if (!M.h[i].used) slot = i;
        st = mode > 2 ? ST_BAD_REQUEST : sd_resolve(r.path, r.path_len, host, sizeof host, &exists);
        if (st == ST_OK && slot < 0) st = ST_BUSY;
        if (st == ST_OK && exists && is_dir(host)) st = mode == 0 ? ST_NOT_FOUND : ST_IO;
        if (st == ST_OK && mode == 0 && !exists) st = ST_NOT_FOUND;
        FILE *f = NULL;
        if (st == ST_OK) {
            if (mode == 0) f = fopen(host, "rb");
            else if (mode == 1) f = fopen(host, "wb");
            else {
                f = fopen(host, "r+b");
                if (!f) f = fopen(host, "w+b");
                if (f) fseek(f, 0, SEEK_END);
            }
            if (!f) st = ST_IO;
        }
        if (st == ST_OK) {
            M.h[slot] = (sd_handle_t){ .used = true, .f = f, .mode = mode };
            p[1] = (uint8_t)slot;
        }
        p[0] = (uint8_t)st;
        sd_send(OP_OPEN_RESP, r.req, 0, 0, 0, p, 2);
        return;
    }
    case OP_HREAD_REQ: {
        sd_handle_t *h = r.payload_len == 5 ? handle_of(r.payload[0]) : NULL;
        if (!h) { sd_nack(r.req, ST_BAD_REQUEST); return; }
        if (h->mode != 0) { sd_nack(r.req, ST_IO); return; }      /* FR_DENIED */
        M.job = (sd_read_job_t){ .active = true, .whole = false, .req = r.req, .f = h->f,
                                 .remaining = get32(r.payload + 1) };
        *cost -= COST_SD_META_US;
        return;
    }
    case OP_HWRITE_CHUNK: {
        *cost += COST_SD_CHUNK_US - COST_SD_META_US;
        sd_handle_t *h = r.payload_len >= 1 ? handle_of(r.payload[0]) : NULL;
        if (!h) {
            if (r.flags & FLAG_LAST) sd_result(r.req, ST_BAD_REQUEST);
            else if (emu_verbose) emu_log("main: SDFS write to a closed handle dropped");
            return;
        }
        size_t len = r.payload_len - 1u, k = 0;
        if (h->mode == 0) { if (!h->batch_err) h->batch_err = ST_IO; }
        else if (len) {
            k = fwrite(r.payload + 1, 1, len, h->f);
            if (k != len && !h->batch_err) h->batch_err = ST_IO;
            account_write(k, cost);
        }
        h->wr_bytes += (uint32_t)k;
        if (h->batch_err && !h->first_err) h->first_err = h->batch_err;
        if (r.flags & FLAG_LAST) {                    /* one acknowledgement per batch */
            sd_result(r.req, h->batch_err);
            h->batch_err = ST_OK;
        }
        return;
    }
    case OP_SEEK_REQ: {
        sd_handle_t *h = r.payload_len == 5 ? handle_of(r.payload[0]) : NULL;
        if (!h) { sd_result(r.req, ST_BAD_REQUEST); return; }
        long off = (long)get32(r.payload + 1);
        if (h->mode == 0) {                           /* f_lseek clamps to the end in read mode */
            struct stat sb;
            fstat(fileno(h->f), &sb);
            if (off > (long)sb.st_size) off = (long)sb.st_size;
        }
        sd_result(r.req, fseek(h->f, off, SEEK_SET) == 0 ? ST_OK : ST_IO);
        return;
    }
    case OP_HCLOSE_REQ: {
        sd_handle_t *h = r.payload_len == 5 ? handle_of(r.payload[0]) : NULL;
        if (!h) { sd_result(r.req, ST_BAD_REQUEST); return; }
        uint32_t expect = get32(r.payload + 1);
        uint8_t s = h->first_err;
        if (fclose(h->f) != 0 && !s) s = ST_IO;
        if (!s && expect != SDFS_NO_VERIFY && expect != h->wr_bytes) {
            s = ST_IO;                                /* a write chunk never arrived */
            emu_log("main: SD close: %u bytes written, the app sent %u (a write chunk was lost)",
                    (unsigned)h->wr_bytes, (unsigned)expect);
        }
        if (M.job.active && M.job.f == h->f) M.job.active = false;
        memset(h, 0, sizeof *h);
        sd_result(r.req, s);
        return;
    }
    default:
        emu_log("main: SDFS opcode %u is not a request (answered with NACK)", r.op);
        sd_nack(r.req, ST_BAD_REQUEST);
        return;
    }
}

/* ======================================================= event dispatch */
static void handle_event(uint8_t code, const uint8_t *p, size_t n, double *cost) {
    switch (code) {
    case EVT_TERM_INPUT:
        *cost += COST_TEXT_US;
        if (n >= 2 && p[0] == 0x01) {                 /* OneWili chunk: marker, count, text */
            size_t cnt = p[1] <= n - 2 ? p[1] : n - 2;
            console_input(p + 2, cnt);
        } else {
            console_input(p, n);                      /* plain terminal keystrokes */
        }
        return;
    case EVT_SDFS:
        sd_request(p, n, cost);
        return;
    case EVT_POWER_ZONES:
        *cost += COST_EVENT_US;
        if (n >= 3) {
            M.zones_reported = true;
            M.zone_mask = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
            if (emu_verbose) emu_log("main: display reports power zones 0x%05x", (unsigned)M.zone_mask);
        }
        return;
    default:
        *cost += COST_EVENT_US;
        if (emu_verbose) emu_log("main: display event %u (%zu bytes) ignored", code, n);
        return;
    }
}

/* Time the byte with counter `idx` finished arriving. */
static double arrival_of(uint64_t idx) {
    for (int i = 0; i < M.narr; i++) if (M.arrivals[i].end > idx) return M.arrivals[i].t;
    return M.narr ? M.arrivals[M.narr - 1].t : 0;
}

static void drop_arrivals_before(uint64_t idx) {
    int k = 0;
    while (k < M.narr - 1 && M.arrivals[k].end <= idx) k++;
    if (k) { memmove(M.arrivals, M.arrivals + k, (size_t)(M.narr - k) * sizeof M.arrivals[0]); M.narr -= k; }
}

/* Feed ring bytes to the parser until one frame is complete. Returns true
 * with the frame in M.code / M.pay / M.len and *ready = its arrival time. */
static bool parse_frame(double *ready) {
    while (M.rx_used < M.rx_total) {
        uint64_t idx = M.rx_used++;
        uint8_t b = M.ring[idx % MAIN_RX_RING];
        switch (M.st) {
        case 0: if (b == EVT_SYNC0) { M.sum = b; M.st = 1; } break;
        case 1:
            if (b == EVT_SYNC1) { M.sum = (uint16_t)(M.sum + b); M.st = 2; }
            else M.st = b == EVT_SYNC0 ? 1 : 0;
            break;
        case 2: M.sum = (uint16_t)(M.sum + b); M.len = b; M.st = 3; break;
        case 3:
            M.sum = (uint16_t)(M.sum + b);
            M.len |= (uint16_t)(b << 8);
            M.got = 0;
            M.overlong = M.len > MAX_EVENT;
            M.st = 4;
            break;
        case 4: M.sum = (uint16_t)(M.sum + b); M.code = b; M.st = M.len ? 5 : 6; break;
        case 5:
            M.sum = (uint16_t)(M.sum + b);
            if (!M.overlong) M.pay[M.got] = b;
            if (++M.got >= M.len) M.st = 6;
            break;
        case 6: M.ck = b; M.st = 7; break;
        case 7:
            M.ck |= (uint16_t)(b << 8);
            M.st = 0;
            if (M.ck != M.sum || M.overlong) {
                M.bad_frames++;
                if (emu_verbose) emu_log("main: link frame (event %u, %u bytes) failed its checksum; dropped", M.code, M.len);
                break;
            }
            *ready = arrival_of(idx);
            return true;
        }
    }
    return false;
}

/* MAIN's main loop, run up to `now`: parse and handle requests on MAIN's own
 * (virtual) clock, stopping while a reply is still being written out. */
static void main_service(double now) {
    tx_pump(now);
    if (M.stream_ms && now >= M.stream_next) {
        if (!tx_pending()) send_gpio_report();
        M.stream_next += M.stream_ms * 1000.0;
        if (M.stream_next < now) M.stream_next = now + M.stream_ms * 1000.0;
        M.tx_not_before = M.tx_not_before > now ? M.tx_not_before : now;
        tx_pump(now);
    }
    for (int guard = 0; guard < 4096; guard++) {
        if (tx_pending() || M.free_at > now) break;
        double cost = 0, start;
        if (M.job.active) {
            start = M.free_at;
            read_job_step(&cost);
        } else {
            double ready;
            if (!parse_frame(&ready)) break;
            start = ready > M.free_at ? ready : M.free_at;
            handle_event(M.code, M.pay, M.len, &cost);
        }
        M.free_at = start + cost;
        if (tx_pending()) {
            M.tx_not_before = M.free_at;
            tx_pump(now);
        }
    }
    drop_arrivals_before(M.rx_used);
}

/* ======================================================= UART hooks */
static void link_rx(emu_uart_device_t *d, const uint8_t *b, size_t n) {
    (void)d;
    double now = now_us();
    main_service(now);                               /* MAIN catches up before new bytes land */
    for (size_t i = 0; i < n; i++) {
        if (M.rx_total - M.rx_used >= MAIN_RX_RING) {   /* DMA ring full: newest wins */
            M.rx_used++;
            if (!M.overrun_bytes++)
                emu_log("main: receive ring overrun: the display sent faster than MAIN could process; "
                        "oldest bytes lost (as on hardware)");
        }
        M.ring[M.rx_total++ % MAIN_RX_RING] = b[i];
    }
    double done = now + (double)n * BYTE_US;
    if (M.narr && (M.narr == (int)(sizeof M.arrivals / sizeof M.arrivals[0]))) {
        M.arrivals[M.narr - 1].end = M.rx_total;       /* coalesce into the newest batch */
        M.arrivals[M.narr - 1].t = done;
    } else {
        M.arrivals[M.narr].end = M.rx_total;
        M.arrivals[M.narr].t = done;
        M.narr++;
    }
}

static void link_poll(emu_uart_device_t *d) {
    (void)d;
    main_service(now_us());
}

void emu_main_task(void) { main_service(now_us()); }

/* Inputs can be set from the command line (--sensor gpio9=1) before
 * emu_main_init() runs, so their defaults are set at load time. */
__attribute__((constructor)) static void main_boot(void) {
    for (int i = 0; i < EMU_HEADER_PINS; i++) M.pin[i].ext = -1;
    M.vref_ext = -1.0f;
}

void emu_main_init(const char *sdcard_dir) {
    if (sdcard_dir && !strcasecmp(sdcard_dir, "none")) {
        M.no_card = true;
        snprintf(M.root, sizeof M.root, "none");
    } else {
        snprintf(M.root, sizeof M.root, "%s", sdcard_dir && *sdcard_dir ? sdcard_dir : "sdcard");
        size_t n = strlen(M.root);
        while (n > 1 && M.root[n - 1] == '/') M.root[--n] = 0;
    }
    M.dev.name = "main-cpu";
    M.dev.rx_from_mcu = link_rx;
    M.dev.poll = link_poll;
    emu_uart_attach(LINK_UART, &M.dev);
}
