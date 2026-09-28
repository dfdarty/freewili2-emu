/* main_link_check — self-test of the emulated MAIN CPU (emu/src/dev_main.c),
 * driven entirely through WiliBSP's unmodified OneWili client (libs/onewili).
 *
 * Every check prints "check: ok ..." or "check: FAIL ..." on RTT and the run
 * ends with "main_link_check: N checks, M failed". tests/scripts/
 * main_link_check.txt drives header GPIO 9 high from outside before the
 * checks start. Expects a fresh SD card folder (tests/smoke.sh gives it one),
 * which the emulator seeds with README.TXT and samples/. */
#include "fw2.h"
#include "platform/diag.h"
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "onewili.h"
#include "onewili_fwgui.h"
#include "onewili_sd.h"
#include "onewili_binary.h"
#include "input/app_recovery_onewili.h"
#include <stdio.h>
#include <string.h>

static ow_device dev;                   /* ~37 KB of link buffers */
static int n_checks, n_failed;
static uint8_t big[20 * 1024];
static uint8_t back[1024];

static void check(bool ok, const char *what) {
    n_checks++;
    if (!ok) n_failed++;
    DIAG("check: %s %s\n", ok ? "ok  " : "FAIL", what);
}

static void checkf(bool ok, const char *fmt, long a, long b) {
    char msg[160];
    snprintf(msg, sizeof msg, fmt, a, b);
    check(ok, msg);
}

/* ------------------------------------------------------------------ SD */
static bool seen_samples, seen_readme;
static void on_root(const char *name, bool is_dir, uint32_t size, void *user) {
    (void)user;
    if (!strcmp(name, "samples") && is_dir) seen_samples = true;
    if (!strcmp(name, "README.TXT") && !is_dir && size > 0) seen_readme = true;
}

static int list_count;
static void on_count(const char *name, bool is_dir, uint32_t size, void *user) {
    (void)name; (void)is_dir; (void)size; (void)user;
    list_count++;
}

static void sd_checks(void) {
    bool is_dir = true;
    uint32_t size = 0;
    size_t got = 0;
    char buf[128];
    ow_sd_file f, g, h;

    check(ow_sd_stat(&dev, "/README.TXT", &is_dir, &size) == OW_OK && !is_dir && size > 0,
          "stat of a seeded sample file");
    check(ow_sd_stat(&dev, "/readme.txt", &is_dir, &size) == OW_OK, "FAT names are case-insensitive");
    check(ow_sd_stat(&dev, "/", &is_dir, &size) == OW_OK && is_dir, "stat of the root is a directory");
    check(ow_sd_list(&dev, "/", on_root, 0) == OW_OK && seen_samples && seen_readme, "list of the root");
    check(ow_sd_get_mem(&dev, "/samples/hello.txt", buf, sizeof buf - 1, &got) == OW_OK &&
          got == 33 && !memcmp(buf, "Hello from the emulated SD card!\n", 33), "whole-file read");
    check(ow_sd_get_mem(&dev, "/nope.txt", buf, sizeof buf, &got) == OW_ERR_FAILED &&
          ow_sd_last_error() == SDFS_ERR_NOT_FOUND, "read of a missing file: NOT_FOUND");
    check(ow_sd_put_mem(&dev, "/t/a.txt", "x", 1, false) == OW_ERR_FAILED &&
          ow_sd_last_error() == SDFS_ERR_NOT_FOUND, "write into a missing folder: NOT_FOUND");
    check(ow_sd_mkdir(&dev, "/t") == OW_OK, "mkdir");
    check(ow_sd_mkdir(&dev, "/t") == OW_ERR_FAILED, "mkdir of an existing folder fails");
    check(ow_sd_put_mem(&dev, "/t/a.txt", "abc", 3, false) == OW_OK, "whole-file write");
    check(ow_sd_put_mem(&dev, "/t/a.txt", "def", 3, true) == OW_OK, "whole-file append");
    check(ow_sd_stat(&dev, "/t/a.txt", &is_dir, &size) == OW_OK && size == 6, "size after append");
    check(ow_sd_get_mem(&dev, "/t/a.txt", buf, sizeof buf, &got) == OW_OK && got == 6 &&
          !memcmp(buf, "abcdef", 6), "read back after append");
    check(ow_sd_get_mem(&dev, "/t/a.txt", buf, 4, &got) != OW_OK &&
          ow_sd_last_error() == SDFS_ERR_TOO_BIG, "read into a too-small buffer: TOO_BIG");

    check(ow_sd_open(&dev, &f, "/t/a.txt", OW_SD_READ) == OW_OK, "open for reading");
    check(ow_sd_seek(&f, 2) == OW_OK, "seek");
    check(ow_sd_read(&f, buf, 10, &got) == OW_OK && got == 4 && !memcmp(buf, "cdef", 4), "handle read to EOF");
    check(ow_sd_read(&f, buf, 10, &got) == OW_OK && got == 0, "handle read at EOF returns 0 bytes");
    check(ow_sd_close(&f) == OW_OK, "close read handle");

    for (unsigned i = 0; i < 300; i++) big[i] = (uint8_t)(i * 7u);
    check(ow_sd_open(&dev, &f, "/t/b.bin", OW_SD_WRITE) == OW_OK, "open for writing");
    check(ow_sd_write(&f, big, 300) == OW_OK, "handle write of 300 bytes (4 chunks)");
    check(ow_sd_close(&f) == OW_OK, "close write handle verifies the byte count");
    check(ow_sd_open(&dev, &f, "/t/b.bin", OW_SD_READ) == OW_OK &&
          ow_sd_read(&f, back, sizeof back, &got) == OW_OK && got == 300 && !memcmp(back, big, 300) &&
          ow_sd_close(&f) == OW_OK, "binary data reads back intact");

    check(ow_sd_open(&dev, &f, "/t/a.txt", OW_SD_READ) == OW_OK &&
          ow_sd_open(&dev, &g, "/t/b.bin", OW_SD_READ) == OW_OK, "two handles open");
    check(ow_sd_open(&dev, &h, "/samples/hello.txt", OW_SD_READ) == OW_ERR_FAILED &&
          ow_sd_last_error() == SDFS_ERR_BUSY, "a third handle: BUSY");
    check(ow_sd_close(&f) == OW_OK && ow_sd_close(&g) == OW_OK, "close both");
    check(ow_sd_open(&dev, &f, "/missing.txt", OW_SD_READ) == OW_ERR_FAILED &&
          ow_sd_last_error() == SDFS_ERR_NOT_FOUND, "open a missing file for reading: NOT_FOUND");

    check(ow_sd_rename(&dev, "/t/b.bin", "/t/c.bin") == OW_OK, "rename");
    check(ow_sd_stat(&dev, "/t/b.bin", &is_dir, &size) == OW_ERR_FAILED &&
          ow_sd_last_error() == SDFS_ERR_NOT_FOUND, "old name is gone");
    check(ow_sd_remove(&dev, "/t") == OW_ERR_FAILED, "remove of a non-empty folder fails");
    list_count = 0;
    check(ow_sd_list(&dev, "/t", on_count, 0) == OW_OK && list_count == 2, "list shows the two files");
    check(ow_sd_remove(&dev, "/t/a.txt") == OW_OK && ow_sd_remove(&dev, "/t/c.bin") == OW_OK &&
          ow_sd_remove(&dev, "/t") == OW_OK, "remove files, then the folder");

    /* Large writes. The vendored client sends a whole ow_sd_write as one
     * burst; MAIN's 2048-byte receive ring overruns if the card goes busy
     * during it (fixed upstream in freewili/onewili by batching). Written in
     * batches of 1 KB, the same data must arrive intact. */
    for (unsigned i = 0; i < sizeof big; i++) big[i] = (uint8_t)(i ^ (i >> 8));
    ow_status w = OW_ERR_FAILED, c = OW_ERR_FAILED;
    if (ow_sd_open(&dev, &f, "/big1.bin", OW_SD_WRITE) == OW_OK) {
        w = ow_sd_write(&f, big, sizeof big);
        c = ow_sd_close(&f);
    }
    DIAG("check: note one 20 KB ow_sd_write: write %d, close %d (sdfs %d)%s\n", (int)w, (int)c,
         (int)ow_sd_last_error(), (w == OW_OK && c == OW_OK) ? "" : " -- the upstream burst overrun");
    bool ok = ow_sd_open(&dev, &f, "/big2.bin", OW_SD_WRITE) == OW_OK;
    for (unsigned off = 0; ok && off < sizeof big; off += 1024) ok = ow_sd_write(&f, big + off, 1024) == OW_OK;
    ok = ow_sd_close(&f) == OW_OK && ok;
    check(ok && ow_sd_stat(&dev, "/big2.bin", &is_dir, &size) == OW_OK && size == sizeof big,
          "20 KB written in 1 KB batches arrives complete");
    ok = ow_sd_open(&dev, &f, "/big2.bin", OW_SD_READ) == OW_OK && ow_sd_seek(&f, 12345) == OW_OK &&
         ow_sd_read(&f, back, 1000, &got) == OW_OK && got == 1000 && !memcmp(back, big + 12345, 1000);
    check(ok && ow_sd_close(&f) == OW_OK, "seek + read in the middle of the large file");
}

/* ---------------------------------------------------------- board clock */
static void clock_checks(void) {
    int32_t y, mo, d, wd, h, mi, se;
    check(ow_hardware_get_time(&dev, &y, &mo, &d, &wd, &h, &mi, &se) == OW_OK && y >= 2000 && mo >= 1 &&
          mo <= 12 && d >= 1 && wd >= 0 && wd <= 6, "the board clock reads a date");
    check(ow_hardware_set_time(&dev, 2027, 5, 16, 9, 59, 58) == OW_OK, "set the clock");
    sleep_ms(2100);
    bool ok = ow_hardware_get_time(&dev, &y, &mo, &d, &wd, &h, &mi, &se) == OW_OK;
    checkf(ok && y == 2027 && mo == 5 && d == 16 && h == 10 && mi == 0 && se <= 1,
           "it keeps time (10:00:%02ld after 2 s)", (long)se, 0);
    check(ok && wd == 0, "the weekday comes from the date (16 May 2027 is a Sunday)");
    check(ow_hardware_set_time(&dev, 2027, 13, 1, 0, 0, 0) == OW_ERR_FAILED, "month 13 is refused");
}

/* ------------------------------------------------- Wi-Fi and BLE scans */
/* An event's fields: after "<ts> <seq> ", before " <ok>". */
static char *event_fields(char *args) {
    char *p = strchr(args, ' ');
    p = p ? strchr(p + 1, ' ') : NULL;
    if (!p) return NULL;
    size_t n = strlen(++p);
    if (n >= 2 && p[n - 2] == ' ' && (p[n - 1] == '0' || p[n - 1] == '1')) p[n - 2] = 0;
    return p;
}

/* The script puts two networks and two devices in range (one nameless). */
static void radio_checks(void) {
    char id[24], args[200];
    int wifi = 0, ble = 0;
    bool home = false, nameless = false;
    check(ow_wireless_wifi_on_scan_for_access_points(&dev) == OW_OK, "Wi-Fi scan starts");
    absolute_time_t end = make_timeout_time_ms(1800);
    while (!time_reached(end)) {
        while (ow_poll_text_line(&dev, id, sizeof id, args, sizeof args) == 1) {
            char *f = event_fields(args);
            if (strcmp(id, "wifiscan") || !f) continue;
            wifi++;
            char bssid[20], ssid[40];
            int rssi, ch, band, auth;
            if (sscanf(f, "%19s %d %d %d %d %39[^\n]", bssid, &rssi, &ch, &band, &auth, ssid) == 6 &&
                !strcmp(bssid, "44:d9:e7:ab:cd:02") && ch == 36 && band == 5 && auth == 7 &&
                !strcmp(ssid, "Home Net 5G") && rssi <= -67 && rssi >= -73)
                home = true;
        }
        sleep_ms(2);
    }
    checkf(wifi == 2, "wifiscan events, one per network (%ld)", wifi, 0);
    check(home, "a wifiscan record: BSSID, RSSI, channel, band, auth, SSID with spaces");
    check(ow_wireless_bluetooth_le_on_scan_bt_devices(&dev, 400) == OW_OK, "BLE scan starts");
    end = make_timeout_time_ms(700);
    while (!time_reached(end)) {
        while (ow_poll_text_line(&dev, id, sizeof id, args, sizeof args) == 1) {
            char *f = event_fields(args);
            if (strcmp(id, "btscan") || !f) continue;
            ble++;
            if (!strncmp(f, " 7d:4c:21:9e:0a:11 ", 19)) nameless = true;   /* no name: empty, then the MAC */
        }
        sleep_ms(2);
    }
    checkf(ble == 2, "btscan events, one per device (%ld)", ble, 0);
    check(nameless, "a nameless device's btscan has an empty name");
    check(ow_wireless_bluetooth_le_on_scan_bt_devices(&dev, -1) == OW_ERR_FAILED, "a negative BLE scan time is refused");
}

/* ---------------------------------------------------------------- GPIO */
static uint32_t read_all(void) {
    uint32_t v = 0;
    if (ow_io_gpio_read_all(&dev, &v) != OW_OK) return 0xFFFFFFFFu;
    return v;
}

static uint32_t vio_mv(void) {
    adc_select_input(5);
    return (uint32_t)adc_read() * 6600u / 4095u;
}

static uint32_t vout_mv(void) {
    adc_select_input(1);
    return (uint32_t)adc_read() * 6600u / 4095u;
}

static void gpio_checks(void) {
    uint32_t v;
    check(ow_io_gpio_set_io_high(&dev, 12) == OW_OK && ((read_all() >> 12) & 1u), "set high reads back 1");
    check(ow_io_gpio_set_io_low(&dev, 12) == OW_OK && !((read_all() >> 12) & 1u), "set low reads back 0");
    check(ow_io_gpio_set_io_toggle(&dev, 12) == OW_OK && ((read_all() >> 12) & 1u), "toggle reads back 1");
    check(ow_io_gpio_set_io_high(&dev, 5) == OW_ERR_FAILED, "a pin that is not on the header is refused");
    v = read_all();
    checkf(((v >> 9) & 1u) == 1u, "externally driven input GPIO 9 reads high (bitfield 0x%lx)", (long)v, 0);

    /* PWM: sampling a 10 Hz 50 %% square wave sees both levels. */
    check(ow_io_gpio_set_pwm(&dev, 14, 10.0, 50.0) == OW_OK, "PWM on GPIO 14");
    int hi = 0, lo = 0;
    for (int i = 0; i < 20; i++) {
        if ((read_all() >> 14) & 1u) hi++; else lo++;
        sleep_ms(13);
    }
    checkf(hi > 3 && lo > 3, "PWM output toggles (%ld high, %ld low samples)", hi, lo);
    check(ow_io_gpio_set_io_low(&dev, 14) == OW_OK && !((read_all() >> 14) & 1u), "set low stops PWM");

    /* Streamed gpioReport binary events. (The parser holds 4.6 KB: static,
     * like the ow_device, to stay off the stack.) */
    static ow_binary_device bdev;
    static ow_event ev;
    ow_transport bt = ow_fwgui_binary_transport();
    ow_binary_open(&bdev, &bt);
    check(ow_io_gpio_stream_io(&dev, 20) == OW_OK, "stream GPIO reports every 20 ms");
    int reports = 0;
    uint32_t last = 0;
    absolute_time_t end = make_timeout_time_ms(200);
    while (!time_reached(end)) {
        while (ow_binary_poll(&bdev, &ev) == 1)
            if (ev.kind == OW_EV_GPIO_REPORT) { reports++; last = ev.u.gpio_report.gpio_bitfield; }
        sleep_ms(2);
    }
    check(ow_io_gpio_stream_io(&dev, 0) == OW_OK, "stop streaming");
    checkf(reports >= 5 && reports <= 12 && ((last >> 12) & 1u), "gpioReport events arrive (%ld in 200 ms)", reports, 0);

    /* Unmodelled command: a well-formed failure, quickly (no 5 s timeout). */
    absolute_time_t t0 = get_absolute_time();
    ow_status s = ow_io_gpio_toggle_hsbdio(&dev, 1);
    long ms = (long)(absolute_time_diff_us(t0, get_absolute_time()) / 1000);
    checkf(s == OW_ERR_FAILED && ms < 100, "unmodelled command fails cleanly (status %ld, %ld ms)", (long)s, ms);

    /* EPOWERZONE: refused once the display reports FPGA (zone 6) off. */
    ow_fwgui_send_power_zones(0x00043u);              /* SENSORS, DISPLAY, SDCARD */
    check(ow_io_gpio_set_io_toggle(&dev, 12) == OW_ERR_FAILED, "GPIO refused with FPGA zone reported off");
    ow_fwgui_send_power_zones(0x00063u);              /* + FPGA */
    check(ow_io_gpio_set_io_toggle(&dev, 12) == OW_OK, "GPIO accepted with FPGA zone reported on");
    ow_fwgui_send_power_zones(0x1FFFFu);

    /* VIO follows the display's expander; Vout comes from MAIN. */
    ioexp_vref(VREF_NONE);
    v = vio_mv();
    checkf(v < 60, "VREF_NONE: VIO %ld mV", (long)v, 0);
    ioexp_vref(VREF_3V3);
    v = vio_mv();
    checkf(v > 3200 && v < 3400, "VREF_3V3: VIO %ld mV", (long)v, 0);
    check(ow_io_analog_out_set_v_prog_vout(&dev, 1, 2.5) == OW_OK, "programmable Vout on at 2.5 V");
    v = vout_mv();
    checkf(v > 2400 && v < 2600, "Vout monitor %ld mV", (long)v, 0);
    ioexp_vref(VREF_PROG_VOUT);
    v = vio_mv();
    checkf(v > 2400 && v < 2600, "VREF_PROG_VOUT: VIO %ld mV", (long)v, 0);
    check(ow_io_analog_out_set_v_prog_vout(&dev, 1, 9.0) == OW_ERR_FAILED, "Vout out of range is refused");
    check(ow_io_analog_out_set_v_prog_vout(&dev, 0, 0) == OW_OK && vout_mv() < 60, "Vout off");
    ioexp_vref(VREF_3V3);
}

int main(void) {
    board_init();
    fw2_app_recovery_init();
    st7796_init();
    fw2_app_about_use_lcd();
    st7796_fill_screen(0x0000);
    board_backlight_set(1);
    st7796_draw_text(8, 8, 2, 0xFFFF, 0x0000, "MAIN LINK CHECK");
    adc_init();
    adc_gpio_init(41);
    adc_gpio_init(45);
    ioexp_vref(VREF_3V3);

    while (fw2_app_recovery_open_onewili(&dev) != OW_OK) fw2_app_recovery_sleep_ms(100);
    if (fw2_app_recovery_wrap_sd() != OW_OK) check(false, "SD transport");

    sd_checks();
    gpio_checks();
    clock_checks();
    radio_checks();

    char line[64];
    snprintf(line, sizeof line, "%d CHECKS, %d FAILED", n_checks, n_failed);
    st7796_draw_text(8, 48, 2, n_failed ? 0x00F8 : 0xE007, 0x0000, line);
    DIAG("main_link_check: %d checks, %d failed\n", n_checks, n_failed);
    for (;;) {
        fw2_app_recovery_task();
        fw2_app_recovery_sleep_ms(50);
    }
}
