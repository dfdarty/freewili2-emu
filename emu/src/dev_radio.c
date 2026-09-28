/* The ESP32-C5's scans, as the MAIN processor reports them to an app.
 *
 * A WiliBSP app has no radio of its own: it asks MAIN, which asks the
 * ESP32-C5 running FREE-WILi's stock firmware, and the results come back
 * as OneWili text events on the same link as every command reply:
 *
 *   w\w\s          Wi-Fi access-point scan, then one event per network:
 *                  [*wifiscan <ts> <seq> <bssid> <rssi> <channel> <band> <authmode> <ssid> 1]
 *   w\b\s <ms>     Bluetooth LE scan for <ms>, then one event per device:
 *                  [*btscan <ts> <seq> <name> <mac> <rssi> 1]
 *
 * Events are framed like replies (timestamp, sequence number, trailing ok
 * flag: the OneWili Python client's framing.py and its tests), with the
 * fields in the order the generated clients document, MACs in lowercase
 * (docs/bluetooth_le.md). What the documentation doesn't settle is modelled
 * as follows, and listed in docs/main-link.md: band is 2 or 5 (GHz),
 * authmode is ESP-IDF's wifi_auth_mode_t number, a nameless device's name
 * and a hidden network's SSID are empty, and a Wi-Fi scan takes 1.2 s.
 *
 * What is "in the air" is a scene of virtual access points and devices,
 * from --radio FILE, the built-in @town, or script `radio` commands:
 *
 *   ap  <bssid> <channel> <authmode> <rssi> <ssid...>
 *   ble <mac> <rssi> [name...]
 *   remove <mac>        rssi <mac> <dBm>        clear
 *
 * Each scan reports every entry once, with up to +/-3 dB of jitter from a
 * fixed-seed generator, so runs repeat exactly. */
#include "emu/emu.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_AP  96
#define MAX_BLE 96
#define WIFI_SCAN_US 1200000.0      /* all channels, active */
#define EVENT_GAP_US 2000.0         /* MAIN forwards one record at a time */

typedef struct { uint8_t mac[6]; int ch, auth, rssi; char ssid[33]; } ap_t;
typedef struct { uint8_t mac[6]; int rssi; char name[32]; } ble_t;

static ap_t  s_ap[MAX_AP];
static int   s_nap;
static ble_t s_ble[MAX_BLE];
static int   s_nble;

/* Pending events, in time order. A new scan of the same kind replaces what
 * the last one had not yet delivered, as a restarted scan does. */
typedef struct { double t; bool wifi; char body[112]; } pending_t;
#define MAX_PENDING (MAX_AP + MAX_BLE)
static pending_t s_q[MAX_PENDING];
static int s_nq;
static uint32_t s_rng = 0x5EED1234u;

static int jitter(void) {                         /* -3..+3, reproducible */
    s_rng = s_rng * 1664525u + 1013904223u;
    return (int)((s_rng >> 24) % 7) - 3;
}

static bool parse_mac(const char *s, uint8_t out[6]) {
    unsigned v[6];
    char tail;
    if (sscanf(s, "%x:%x:%x:%x:%x:%x%c", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &tail) != 6) return false;
    for (int i = 0; i < 6; i++) {
        if (v[i] > 255) return false;
        out[i] = (uint8_t)v[i];
    }
    return true;
}

static void fmt_mac(const uint8_t m[6], char *out) {
    sprintf(out, "%02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
}

static const char *skip_ws(const char *s) { while (*s == ' ' || *s == '\t') s++; return s; }

/* The next word into out; returns the rest of the line. */
static const char *word(const char *s, char *out, size_t cap) {
    s = skip_ws(s);
    size_t n = 0;
    while (*s && *s != ' ' && *s != '\t') { if (n + 1 < cap) out[n++] = *s; s++; }
    out[n] = 0;
    return s;
}

static void rest_of_line(const char *s, char *out, size_t cap) {
    s = skip_ws(s);
    size_t n = strlen(s);
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) n--;
    if (n >= cap) n = cap - 1;
    memcpy(out, s, n);
    out[n] = 0;
}

static int find_ap(const uint8_t m[6]) { for (int i = 0; i < s_nap; i++) if (!memcmp(s_ap[i].mac, m, 6)) return i; return -1; }
static int find_ble(const uint8_t m[6]) { for (int i = 0; i < s_nble; i++) if (!memcmp(s_ble[i].mac, m, 6)) return i; return -1; }

/* One scene line (or script `radio` command). Returns an error, or NULL. */
const char *emu_radio_line(const char *line) {
    char w[40], a[40], b[40], c[40], d[40];
    const char *p = word(line, w, sizeof w);
    if (!w[0] || w[0] == '#') return NULL;
    if (!strcmp(w, "clear")) { s_nap = s_nble = 0; return NULL; }
    if (!strcmp(w, "ap")) {
        uint8_t m[6];
        p = word(p, a, sizeof a); p = word(p, b, sizeof b); p = word(p, c, sizeof c); p = word(p, d, sizeof d);
        if (!parse_mac(a, m)) return "ap: BSSID like 00:11:22:33:44:55";
        int ch = atoi(b), auth = atoi(c), rssi = atoi(d);
        if (ch < 1 || ch > 196) return "ap: channel 1-196";
        if (auth < 0 || auth > 12) return "ap: authmode 0-12 (0 open, 3 WPA2, 6 WPA3, ...)";
        if (rssi > -1 || rssi < -110) return "ap: RSSI -110 to -1 dBm";
        int i = find_ap(m);
        if (i < 0) { if (s_nap >= MAX_AP) return "ap: scene full"; i = s_nap++; }
        memcpy(s_ap[i].mac, m, 6);
        s_ap[i].ch = ch; s_ap[i].auth = auth; s_ap[i].rssi = rssi;
        rest_of_line(p, s_ap[i].ssid, sizeof s_ap[i].ssid);
        return NULL;
    }
    if (!strcmp(w, "ble")) {
        uint8_t m[6];
        p = word(p, a, sizeof a); p = word(p, b, sizeof b);
        if (!parse_mac(a, m)) return "ble: MAC like 00:11:22:33:44:55";
        int rssi = atoi(b);
        if (rssi > -1 || rssi < -110) return "ble: RSSI -110 to -1 dBm";
        int i = find_ble(m);
        if (i < 0) { if (s_nble >= MAX_BLE) return "ble: scene full"; i = s_nble++; }
        memcpy(s_ble[i].mac, m, 6);
        s_ble[i].rssi = rssi;
        rest_of_line(p, s_ble[i].name, sizeof s_ble[i].name);
        return NULL;
    }
    if (!strcmp(w, "remove") || !strcmp(w, "rssi")) {
        uint8_t m[6];
        p = word(p, a, sizeof a);
        if (!parse_mac(a, m)) return "remove/rssi: MAC like 00:11:22:33:44:55";
        int i = find_ap(m), j = find_ble(m);
        if (i < 0 && j < 0) return "no such device in the scene";
        if (w[0] == 'r' && w[1] == 'e') {
            if (i >= 0) memmove(&s_ap[i], &s_ap[i + 1], sizeof s_ap[0] * (size_t)(--s_nap - i));
            if (j >= 0) memmove(&s_ble[j], &s_ble[j + 1], sizeof s_ble[0] * (size_t)(--s_nble - j));
            return NULL;
        }
        word(p, b, sizeof b);
        int rssi = atoi(b);
        if (rssi > -1 || rssi < -110) return "rssi: -110 to -1 dBm";
        if (i >= 0) s_ap[i].rssi = rssi;
        if (j >= 0) s_ble[j].rssi = rssi;
        return NULL;
    }
    return "radio: ap, ble, remove, rssi or clear";
}

/* A small neighbourhood: ordinary networks, and one of each kind of thing
 * a surveillance detector looks for (addresses from real vendor prefixes). */
static const char *const TOWN[] = {
    "ap 3C:37:86:12:34:56 6 3 -52 NETGEAR42",
    "ap 44:D9:E7:AB:CD:01 1 7 -64 Hernandez Family",
    "ap 44:D9:E7:AB:CD:02 36 7 -71 Hernandez Family 5G",
    "ap F0:9F:C2:10:20:30 11 0 -80 xfinitywifi",
    "ap 90:9A:4A:55:66:77 149 3 -58 CoffeeShop",
    "ap B4:1E:52:00:11:22 6 3 -69 FLOCK-CAM-2291",       /* Flock Safety's registered prefix, setup SSID */
    "ap 00:25:DF:44:55:66 1 3 -74 AB3-X7K2",              /* Axon prefix, body-camera SSID */
    "ap AC:9F:C3:77:88:99 11 3 -66 Ring-8c1e",            /* Ring doorbell (Ring LLC prefix) */
    "ap 02:13:37:13:37:00 6 0 -61 Pineapple_1337",        /* a WiFi Pineapple's setup network */
    "ap 90:9A:4A:55:66:78 149 0 -55 CoffeeShop",          /* same name, open: an evil twin */
    "ble 5C:F3:70:A1:B2:C3 -63 Galaxy Buds2",
    "ble 7D:4C:21:9E:0A:11 -77",
    "ble D4:3A:2C:10:20:30 -70 Flock-2291",
    "ble 0C:FA:22:DE:AD:01 -59 Flipper Nimbus",
    NULL,
};

const char *emu_radio_load(const char *path) {
    static char err[160];
    if (!strcmp(path, "@town")) {
        for (int i = 0; TOWN[i]; i++) emu_radio_line(TOWN[i]);
        return NULL;
    }
    FILE *f = fopen(path, "r");
    if (!f) { snprintf(err, sizeof err, "can't open %s", path); return err; }
    char line[256];
    int n = 0;
    while (fgets(line, sizeof line, f)) {
        n++;
        const char *e = emu_radio_line(line);
        if (e) { snprintf(err, sizeof err, "%s:%d: %s", path, n, e); fclose(f); return err; }
    }
    fclose(f);
    return NULL;
}

static void queue_clear(bool wifi) {
    int k = 0;
    for (int i = 0; i < s_nq; i++) if (s_q[i].wifi != wifi) s_q[k++] = s_q[i];
    s_nq = k;
}

static void queue_add(double t, bool wifi, const char *line) {
    if (s_nq >= MAX_PENDING) return;
    int i = s_nq++;
    while (i > 0 && s_q[i - 1].t > t) { s_q[i] = s_q[i - 1]; i--; }   /* keep time order */
    s_q[i].t = t;
    s_q[i].wifi = wifi;
    snprintf(s_q[i].body, sizeof s_q[i].body, "%s", line);
}

void emu_radio_wifi_scan(double now_us) {
    queue_clear(true);
    double t = now_us + WIFI_SCAN_US;
    for (int i = 0; i < s_nap; i++, t += EVENT_GAP_US) {
        const ap_t *a = &s_ap[i];
        char mac[18], line[128];
        fmt_mac(a->mac, mac);
        int rssi = a->rssi + jitter();
        if (rssi > -1) rssi = -1;
        snprintf(line, sizeof line, "%s %d %d %d %d %s", mac, rssi, a->ch, a->ch > 14 ? 5 : 2, a->auth, a->ssid);
        queue_add(t, true, line);
    }
    if (emu_verbose) emu_log("radio: Wi-Fi scan, %d network%s in range", s_nap, s_nap == 1 ? "" : "s");
}

void emu_radio_ble_scan(double now_us, long ms) {
    queue_clear(false);
    if (ms < 0) ms = 0;
    double span = (double)ms * 1000.0;
    for (int i = 0; i < s_nble; i++) {
        const ble_t *d = &s_ble[i];
        char mac[18], line[128];
        fmt_mac(d->mac, mac);
        int rssi = d->rssi + jitter();
        if (rssi > -1) rssi = -1;
        /* heard somewhere in the window, strongest first-ish, as adverts go */
        double t = now_us + span * (double)(i + 1) / (double)(s_nble + 1) + EVENT_GAP_US;
        snprintf(line, sizeof line, "%s %s %d", d->name, mac, rssi);
        queue_add(t, false, line);
    }
    if (emu_verbose) emu_log("radio: BLE scan %ld ms, %d device%s in range", ms, s_nble, s_nble == 1 ? "" : "s");
}

/* The next event due by now_us: its name, and its fields into out. */
const char *emu_radio_poll(double now_us, char *out, size_t cap) {
    if (!s_nq || s_q[0].t > now_us) return NULL;
    const char *name = s_q[0].wifi ? "wifiscan" : "btscan";
    snprintf(out, cap, "%s", s_q[0].body);
    memmove(&s_q[0], &s_q[1], sizeof s_q[0] * (size_t)(--s_nq));
    return name;
}
