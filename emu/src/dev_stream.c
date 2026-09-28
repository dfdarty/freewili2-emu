/* dev_stream.c — OneWili peer streams: MAIN's router, and the other clients.
 *
 * Peer streams carry best-effort datagrams (1..128 bytes) between OneWili
 * clients — the DISPLAY, the ESP32-C5, the CM0 and a PC host — routed by
 * MAIN (ow_stream_wire.h, which MAIN and every client share verbatim; this
 * file uses it too). The display app's end is OneWili's own client code,
 * unmodified, on the FwGUI link: event 0xF1 carries a stream payload
 * P = {dst, src, data...} to MAIN, command 0xF1 carries one back.
 *
 * MAIN's side, as ow_stream_wire.h specifies it:
 *   - HELLO opens the display's link, and every HELLO is answered with a
 *     CREDIT: {0xF1, 0, consumed_total, dropped_from, dropped_to}, u32 LE,
 *     free-running since MAIN booted.
 *   - Every data frame taken off the link adds OW_STREAM_WIRE_BYTES(n) to
 *     consumed_total — routed, dropped or gated alike — and is followed by a
 *     CREDIT, which is what lets the client's 768-unit window move on.
 *   - MAIN overwrites src with the link's own id. Datagrams to MAIN (it has
 *     no consumer), to an absent peer, to a bad id, or to the ESP32 while
 *     Wireless > ESP32 Mode isn't "OneWili API" are dropped, counted in
 *     dropped_from. Datagrams to the display while its link is closed (no
 *     stream frame for OW_STREAM_EXPIRE_MS) are dropped, counted in
 *     dropped_to.
 *
 * The other clients are stand-ins, chosen with --peer or the script's
 * `peer` command:
 *   script   datagrams to it are logged ("stream: display -> esp32 ...") for
 *            `expect`; `stream <peer> <bytes>` sends one from it
 *   dualcpu  (esp32 only) the ESP32 half of WiliBSP's dualcpu example:
 *            PONG at once, TELEMETRY about once a second (its Wi-Fi scan
 *            comes from the emulator's radio scene), SET_LED, SCAN_NOW
 * The ESP32 is there only while its power zone (5) is on and ESP32 Mode is
 * OneWili API (`w\e 1`), as on the board. One-way latency on the ESP32 link
 * is set so a display->ESP32->display round trip, UART wire time included,
 * takes about 5.5 ms; dualcpu shows 6.0 ms on a board (2026-09-28), which
 * also includes the app's own time to poll it.
 */
#include "emu/emu.h"
#include "ow_stream_wire.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ESP32_LINK_US   2600.0     /* one way, MAIN <-> ESP32 */
#define TEXT_LINK_US    1000.0     /* CM0 / PC host: queued at MAIN, fetched by polling */
#define LOOPBACK_US     50.0
#define MAX_INFLIGHT    128
#define ESP32_ZONE      5u

typedef enum { PEER_OFF, PEER_SCRIPT, PEER_DUALCPU } peer_mode_t;

typedef struct {
    double   due;                  /* when it reaches its destination */
    uint8_t  dst, src, len;
    uint8_t  data[OW_STREAM_MTU];
} dgram_t;

static const char *const PEER_NAME[OW_STREAM_PEER_COUNT] = { "main", "display", "esp32", "cm0", "host" };

static struct {
    /* the display's push link, as MAIN keeps it */
    bool     open;
    double   last_rx;
    uint32_t consumed, dropped_from, dropped_to;
    uint32_t esp32_mode;            /* Wireless > ESP32 Mode: 0 default firmware, 1 OneWili API */

    peer_mode_t mode[OW_STREAM_PEER_COUNT];
    uint32_t peer_drops[OW_STREAM_PEER_COUNT];   /* what each stand-in's ow_stream_drops would say */

    dgram_t  q[MAX_INFLIGHT];       /* datagrams on their way, any direction */
    int      nq;

    /* the dualcpu ESP32 half */
    bool     esp_up;
    double   esp_boot, esp_next_tele, esp_scan_done;
    uint16_t esp_seq;
    uint8_t  led_mode, led_r, led_g, led_b;
    bool     scanning;
    int      ap_total, nap;
    emu_ap_view_t ap[4];
} S;

/* ------------------------------------------------------------ helpers */
static bool peer_ok(unsigned id) { return id < OW_STREAM_PEER_COUNT; }

static bool esp32_present(void) {
    return S.mode[OW_STREAM_PEER_ESP32] != PEER_OFF && S.esp32_mode == 1 && emu_rail_on(ESP32_ZONE);
}

static bool peer_present(unsigned id) {
    if (id == OW_STREAM_PEER_ESP32) return esp32_present();
    return (id == OW_STREAM_PEER_CM0 || id == OW_STREAM_PEER_HOST) && S.mode[id] != PEER_OFF;
}

static double link_us(unsigned id) {
    if (id == OW_STREAM_PEER_ESP32) return ESP32_LINK_US;
    if (id == OW_STREAM_PEER_DISPLAY) return 0.0;              /* the FwGUI wire is timed by dev_main */
    return TEXT_LINK_US;
}

static void enqueue(double due, unsigned dst, unsigned src, const uint8_t *d, size_t n) {
    if (S.nq == MAX_INFLIGHT) {                                /* MAIN's queues are small too */
        if (dst == OW_STREAM_PEER_DISPLAY) S.dropped_to++;
        if (peer_ok(src)) S.peer_drops[src]++;
        return;
    }
    dgram_t *g = &S.q[S.nq++];
    g->due = due;
    g->dst = (uint8_t)dst;
    g->src = (uint8_t)src;
    g->len = (uint8_t)n;
    memcpy(g->data, d, n);
}

static void send_credit(void) {
    uint8_t p[OW_STREAM_HDR + OW_STREAM_CREDIT_LEN] = { OW_STREAM_CTL_CREDIT, OW_STREAM_PEER_MAIN };
    ow_stream_put_u32(p + 2, S.consumed);
    ow_stream_put_u32(p + 6, S.dropped_from);
    ow_stream_put_u32(p + 10, S.dropped_to);
    emu_main_stream_send(p, sizeof p);
}

/* "stream: display -> esp32 (8 bytes): 68 69 20 65 73 70 33 32  |hi esp32|"
 * -- the first 32 bytes, for `expect`. */
static void log_datagram(unsigned src, unsigned dst, const uint8_t *d, size_t n) {
    char hex[3 * 32 + 4], txt[32 + 1];
    size_t k = 0, show = n < 32 ? n : 32;
    for (size_t i = 0; i < show; i++) {
        k += (size_t)snprintf(hex + k, sizeof hex - k, "%s%02x", i ? " " : "", d[i]);
        txt[i] = d[i] >= 0x20 && d[i] < 0x7f ? (char)d[i] : '.';
    }
    txt[show] = 0;
    emu_log("stream: %s -> %s (%zu byte%s): %s%s  |%s%s|", PEER_NAME[src], PEER_NAME[dst], n, n == 1 ? "" : "s",
            hex, n > show ? " ..." : "", txt, n > show ? "..." : "");
}

/* A stand-in peer sends a datagram. MAIN routes it (to the display, the
 * only other client the emulator runs) after the sender's link latency. */
static void peer_send(unsigned src, unsigned dst, const uint8_t *d, size_t n, double now) {
    if (!peer_present(src)) return;
    if (dst != OW_STREAM_PEER_DISPLAY) { S.peer_drops[src]++; return; }
    if (emu_verbose && S.mode[src] == PEER_SCRIPT) log_datagram(src, dst, d, n);
    enqueue(now + link_us(src), dst, src, d, n);
}

/* ---------------------------------------------- the dualcpu ESP32 half */
static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

static void esp_scan(double now) {
    emu_ap_view_t all[64];
    int n = emu_radio_ap_list(all, 64);
    for (int i = 1; i < n; i++)                       /* strongest first */
        for (int j = i; j > 0 && all[j].rssi > all[j - 1].rssi; j--) {
            emu_ap_view_t t = all[j]; all[j] = all[j - 1]; all[j - 1] = t;
        }
    S.ap_total = n;
    S.nap = n < 4 ? n : 4;
    memcpy(S.ap, all, sizeof S.ap[0] * (size_t)S.nap);
    S.scanning = false;
    (void)now;
}

static void esp_telemetry(double now) {
    uint8_t p[OW_STREAM_MTU];
    size_t k = 0;
    p[k++] = 0x01;
    put16(p + k, S.esp_seq++); k += 2;
    ow_stream_put_u32(p + k, (uint32_t)((now - S.esp_boot) / 1e6)); k += 4;
    /* The die warms a little after boot and wanders; 42.3 C on the bench. */
    double up_s = (now - S.esp_boot) / 1e6;
    int16_t dc = (int16_t)(395 + (up_s > 300 ? 28 : up_s * 28 / 300) + ((S.esp_seq * 7) % 5) - 2);
    put16(p + k, (uint16_t)dc); k += 2;
    ow_stream_put_u32(p + k, 27u * 1024u + (uint32_t)((S.esp_seq * 97) % 512)); k += 4;   /* internal RAM free */
    ow_stream_put_u32(p + k, 7979u * 1024u); k += 4;                                    /* PSRAM free */
    ow_stream_put_u32(p + k, S.peer_drops[OW_STREAM_PEER_ESP32]); k += 4;
    ow_stream_put_u32(p + k, 0); k += 4;              /* GPIO reports mirrored to the ESP32 */
    ow_stream_put_u32(p + k, 0); k += 4;              /* text events mirrored to the ESP32 */
    p[k++] = S.led_mode; p[k++] = S.led_r; p[k++] = S.led_g; p[k++] = S.led_b;
    p[k++] = (uint8_t)(S.ap_total > 255 ? 255 : S.ap_total);
    p[k++] = (uint8_t)S.nap;
    for (int i = 0; i < S.nap; i++) {
        size_t sl = strlen(S.ap[i].ssid);
        if (sl > 12) sl = 12;
        p[k++] = (uint8_t)(int8_t)S.ap[i].rssi;
        p[k++] = (uint8_t)S.ap[i].channel;
        p[k++] = (uint8_t)sl;
        memcpy(p + k, S.ap[i].ssid, sl); k += sl;
    }
    peer_send(OW_STREAM_PEER_ESP32, OW_STREAM_PEER_DISPLAY, p, k, now);
}

static void esp_receive(const dgram_t *g, double now) {
    if (g->src != OW_STREAM_PEER_DISPLAY || g->len < 1) return;   /* the half ignores other peers */
    switch (g->data[0]) {
    case 0x02:                                         /* SET_LED mode r g b */
        if (g->len < 5) return;
        S.led_mode = g->data[1]; S.led_r = g->data[2]; S.led_g = g->data[3]; S.led_b = g->data[4];
        emu_log("esp32: LED %s %u,%u,%u", S.led_mode == 0 ? "off" : S.led_mode == 1 ? "solid" : "rainbow",
                S.led_r, S.led_g, S.led_b);
        return;
    case 0x03: {                                       /* PING id display_ms -> PONG id esp_ms */
        if (g->len < 9) return;
        uint8_t p[9] = { 0x04 };
        memcpy(p + 1, g->data + 1, 4);
        ow_stream_put_u32(p + 5, (uint32_t)((now - S.esp_boot) / 1000.0));
        peer_send(OW_STREAM_PEER_ESP32, OW_STREAM_PEER_DISPLAY, p, sizeof p, now);
        return;
    }
    case 0x05:                                         /* SCAN_NOW: results in the next TELEMETRY */
        if (!S.scanning) {
            S.scanning = true;
            S.esp_scan_done = now + 1.2e6;
            emu_log("esp32: Wi-Fi scan");
        }
        return;
    default:
        return;                                        /* unknown types are ignored */
    }
}

static void esp_task(double now) {
    bool up = S.mode[OW_STREAM_PEER_ESP32] == PEER_DUALCPU && esp32_present();
    if (up && !S.esp_up) {                             /* power and mode just came right: the app boots */
        S.esp_boot = now;
        S.esp_next_tele = now + 1.0e6;
        S.esp_seq = 0;
        S.scanning = true;                             /* it scans once at start */
        S.esp_scan_done = now + 1.2e6;
        emu_log("esp32: the dualcpu ESP32 half is up (a stand-in; ESP32 Mode is OneWili API)");
    } else if (!up && S.esp_up) {
        emu_log("esp32: the dualcpu ESP32 half stopped (%s)",
                !emu_rail_on(ESP32_ZONE) ? "its power zone is off" : "ESP32 Mode is not OneWili API");
    }
    S.esp_up = up;
    if (!up) return;
    if (S.scanning && now >= S.esp_scan_done) esp_scan(now);
    if (now >= S.esp_next_tele) {
        esp_telemetry(now);
        S.esp_next_tele += 1.0e6;
        if (S.esp_next_tele < now) S.esp_next_tele = now + 1.0e6;
    }
}

/* ------------------------------------------------------ MAIN's router */
void emu_stream_from_display(const uint8_t *p, size_t n) {
    double now = (double)emu_time_us();
    if (n < 1) return;
    S.last_rx = now;
    if (p[0] >= OW_STREAM_CTL_FIRST) {                 /* hop-local control */
        if (p[0] == OW_STREAM_CTL_HELLO) {
            if (!S.open && emu_verbose) emu_log("stream: the display's link is open");
            S.open = true;
            send_credit();
        }
        return;
    }
    if (!S.open && emu_verbose) emu_log("stream: the display's link is open");
    S.open = true;
    size_t data = n > OW_STREAM_HDR ? n - OW_STREAM_HDR : 0;
    S.consumed += OW_STREAM_WIRE_BYTES(data);
    unsigned dst = p[0];
    if (data == 0 || data > OW_STREAM_MTU || !peer_ok(dst) || dst == OW_STREAM_PEER_MAIN) {
        S.dropped_from++;
    } else if (dst == OW_STREAM_PEER_DISPLAY) {        /* loopback */
        enqueue(now + LOOPBACK_US, dst, OW_STREAM_PEER_DISPLAY, p + OW_STREAM_HDR, data);
    } else if (!peer_present(dst)) {
        S.dropped_from++;
        if (emu_verbose) emu_log("stream: display -> %s dropped (%s)", PEER_NAME[dst],
                                 dst == OW_STREAM_PEER_ESP32 && S.mode[dst] != PEER_OFF
                                     ? (S.esp32_mode != 1 ? "ESP32 Mode is not OneWili API" : "the ESP32 is off")
                                     : "no such client in this run (--peer)");
    } else {
        enqueue(now + link_us(dst), dst, OW_STREAM_PEER_DISPLAY, p + OW_STREAM_HDR, data);
    }
    send_credit();
}

void emu_stream_task(double now) {
    if (S.open && now - S.last_rx > OW_STREAM_EXPIRE_MS * 1000.0) {
        S.open = false;
        if (emu_verbose) emu_log("stream: the display's link closed (no stream frame for %u ms)", OW_STREAM_EXPIRE_MS);
    }
    esp_task(now);
    for (int i = 0; i < S.nq;) {
        dgram_t *g = &S.q[i];
        if (g->due > now) { i++; continue; }
        dgram_t d = *g;
        memmove(g, g + 1, sizeof *g * (size_t)(--S.nq - i));
        if (d.dst == OW_STREAM_PEER_DISPLAY) {
            if (!S.open) {                             /* the display isn't using streams */
                S.dropped_to++;
                if (peer_ok(d.src) && d.src != OW_STREAM_PEER_DISPLAY) S.peer_drops[d.src]++;
                continue;
            }
            uint8_t p[OW_STREAM_HDR + OW_STREAM_MTU] = { OW_STREAM_PEER_DISPLAY, d.src };
            memcpy(p + OW_STREAM_HDR, d.data, d.len);
            emu_main_stream_send(p, OW_STREAM_HDR + d.len);
        } else if (!peer_present(d.dst)) {
            S.peer_drops[d.dst]++;
        } else if (S.mode[d.dst] == PEER_DUALCPU) {
            esp_receive(&d, now);
        } else {
            log_datagram(d.src, d.dst, d.data, d.len);
        }
    }
}

void emu_stream_counters(uint32_t *dropped_to, uint32_t *dropped_from) {
    *dropped_to = S.dropped_to;
    *dropped_from = S.dropped_from;
}

/* ------------------------------------------------------- configuration */
void emu_stream_set_esp32_mode(uint32_t v) {
    if (v != S.esp32_mode) emu_log("main: Wireless > ESP32 Mode = %s", v ? "OneWili API" : "Default Firmware");
    S.esp32_mode = v;
}

static int peer_by_name(const char *s) {
    for (unsigned i = OW_STREAM_PEER_ESP32; i < OW_STREAM_PEER_COUNT; i++) if (!strcmp(s, PEER_NAME[i])) return (int)i;
    return -1;
}

const char *emu_stream_set_peer(const char *name, const char *mode) {
    int id = peer_by_name(name);
    if (id < 0) return "peer: esp32, cm0 or host";
    if (!strcmp(mode, "off")) S.mode[id] = PEER_OFF;
    else if (!strcmp(mode, "script")) S.mode[id] = PEER_SCRIPT;
    else if (!strcmp(mode, "dualcpu") && id == OW_STREAM_PEER_ESP32) S.mode[id] = PEER_DUALCPU;
    else return id == OW_STREAM_PEER_ESP32 ? "peer esp32: off, script or dualcpu" : "peer: off or script";
    return NULL;
}

/* "--peer esp32=dualcpu" */
const char *emu_stream_peer_arg(const char *arg) {
    char name[16];
    const char *eq = strchr(arg, '=');
    if (!eq || (size_t)(eq - arg) >= sizeof name) return "--peer NAME=MODE, e.g. esp32=dualcpu";
    memcpy(name, arg, (size_t)(eq - arg));
    name[eq - arg] = 0;
    return emu_stream_set_peer(name, eq + 1);
}

/* Script: `stream <peer> <bytes>` -- hex bytes (01 ff ...) or "text". The
 * peer must be present (--peer / `peer`), and the ESP32 needs its zone and
 * ESP32 Mode as well. */
const char *emu_stream_script_send(const char *args) {
    char name[16];
    int used = 0;
    if (sscanf(args, "%15s %n", name, &used) < 1) return "stream <esp32|cm0|host> <hex bytes or \"text\">";
    int id = peer_by_name(name);
    if (id < 0) return "stream: esp32, cm0 or host";
    const char *s = args + used;
    uint8_t d[OW_STREAM_MTU];
    size_t n = 0;
    if (*s == '"') {
        const char *e = strrchr(s + 1, '"');
        if (!e) return "stream: unterminated \"text\"";
        n = (size_t)(e - s - 1);
        if (n > OW_STREAM_MTU) return "stream: at most 128 bytes";
        memcpy(d, s + 1, n);
    } else {
        while (*s) {
            while (isspace((unsigned char)*s)) s++;
            if (!*s) break;
            char *end;
            long v = strtol(s, &end, 16);
            if (end == s || v < 0 || v > 255) return "stream: bytes are hex, 00 to ff";
            if (n == OW_STREAM_MTU) return "stream: at most 128 bytes";
            d[n++] = (uint8_t)v;
            s = end;
        }
    }
    if (!n) return "stream: a datagram is 1 to 128 bytes";
    if (!peer_present((unsigned)id))
        return id == OW_STREAM_PEER_ESP32 ? "stream: the ESP32 isn't there (--peer esp32=script, its power zone, and ESP32 Mode = OneWili API)"
                                          : "stream: that peer isn't in this run (--peer NAME=script)";
    peer_send((unsigned)id, OW_STREAM_PEER_DISPLAY, d, n, (double)emu_time_us());
    return NULL;
}
