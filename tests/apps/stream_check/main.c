/* stream_check — self-test of the emulated MAIN's peer-stream router
 * (emu/src/dev_stream.c), driven through WiliBSP's unmodified OneWili stream
 * client (libs/onewili: ow_stream_write / ow_stream_poll / ow_stream_drops).
 *
 * Every check prints "check: ok ..." or "check: FAIL ..." on RTT and the run
 * ends with "stream_check: N checks, M failed". tests/scripts/stream_check.txt
 * runs it with a scripted ESP32 and CM0 (--peer esp32=script --peer
 * cm0=script) and no host, answers "sc: waiting for peers" with a datagram
 * from each, and sends one while the app only polls and one while the
 * display's link is closed. */
#include "fw2.h"
#include "platform/diag.h"
#include "pico/stdlib.h"
#include "onewili.h"
#include "onewili_fwgui.h"
#include "onewili_stream.h"
#include "input/app_recovery_onewili.h"
#include "input/picpwr.h"
#include <string.h>

static ow_device dev;                   /* ~37 KB of link buffers */
static int n_checks, n_failed;
static uint32_t n_other;                /* datagrams nobody was waiting for */

static void check(bool ok, const char *what) {
    n_checks++;
    if (!ok) n_failed++;
    DIAG("check: %s %s\n", ok ? "ok  " : "FAIL", what);
}

static uint32_t ms_now(void) { return to_ms_since_boot(get_absolute_time()); }

/* Poll for `ms`, or until a datagram from `want` with this content arrives
 * (want < 0: just keep the link serviced). Returns true if it arrived. */
static bool await(int want, const void *data, size_t n, uint32_t ms) {
    uint32_t end = ms_now() + ms;
    uint8_t buf[OW_STREAM_MTU];
    do {
        ow_peer src;
        int got = ow_stream_poll(&dev, &src, buf, sizeof buf);
        if (got > 0) {
            if (want >= 0 && (int)src == want && (size_t)got == n && !memcmp(buf, data, n)) return true;
            n_other++;
            DIAG("sc: datagram from %d (%d bytes), not the one awaited\n", (int)src, got);
            continue;
        }
        fw2_app_recovery_task();                     /* parses the PIC's status frames, too */
        sleep_ms(1);
    } while ((int32_t)(ms_now() - end) < 0);
    return false;
}

static void drops_step(const char *what, ow_peer dst, uint32_t expect_more) {
    uint32_t d0 = ow_stream_drops(&dev);
    ow_status s = ow_stream_write(&dev, dst, what, (uint32_t)strlen(what));
    await(-1, NULL, 0, 100);                         /* MAIN's CREDIT carries the totals */
    uint32_t d1 = ow_stream_drops(&dev);
    char msg[120];
    snprintf(msg, sizeof msg, "%s: write %d, drops +%u (want +%u)", what, (int)s,
             (unsigned)(d1 - d0), (unsigned)expect_more);
    check(s == OW_OK && d1 - d0 == expect_more, msg);
}

int main(void) {
    board_init();
    fw2_app_recovery_init();
    picpwr_keep_awake(picpwr_zone_bit(PICPWR_ZONE_WIFI_BT));   /* the ESP32 */
    while (fw2_app_recovery_open_onewili(&dev) != OW_OK) fw2_app_recovery_sleep_ms(100);

    /* HELLO -> CREDIT: until MAIN answers, every write is refused. */
    ow_fwgui_stats ls;
    uint32_t end = ms_now() + 3000;
    do { await(-1, NULL, 0, 20); ow_fwgui_get_stats(&ls); } while (!ls.stream_confirmed && (int32_t)(ms_now() - end) < 0);
    check(ls.stream_confirmed == 1, "MAIN confirms the link (HELLO answered with a CREDIT)");

    /* Loopback: MAIN routes display -> display. */
    check(ow_stream_write(&dev, OW_PEER_DISPLAY, "loop", 4) == OW_OK && await(OW_PEER_DISPLAY, "loop", 4, 200),
          "a datagram to the display itself comes back");

    /* Datagrams MAIN can't deliver are dropped and counted against the sender. */
    drops_step("to MAIN", OW_PEER_MAIN, 1);
    drops_step("to the host (not in this run)", OW_PEER_HOST, 1);
    drops_step("to the ESP32 in ESP32 Mode 0", OW_PEER_ESP32, 1);

    /* ESP32 Mode = OneWili API, and the ESP32's rail on: now it's routed. */
    check(ow_wireless_e_sp32_mode(&dev, 1) == OW_OK, "w\\e 1 (ESP32 Mode = OneWili API)");
    uint32_t rails = 0;
    end = ms_now() + 3000;
    while (!(picpwr_rails(&rails) && (rails & picpwr_zone_bit(PICPWR_ZONE_WIFI_BT))) && (int32_t)(ms_now() - end) < 0)
        await(-1, NULL, 0, 50);
    check(rails & picpwr_zone_bit(PICPWR_ZONE_WIFI_BT), "the ESP32's power zone is on");
    drops_step("hi esp32", OW_PEER_ESP32, 0);
    drops_step("hi cm0", OW_PEER_CM0, 0);

    /* MAIN's own view of this link (the text clients' h\a\c). */
    int32_t mtu = 0, queued = -1, to = -1, from = -1;
    ow_status s = ow_hardware_system_stream_status(&dev, &mtu, &queued, &to, &from);
    check(s == OW_OK && mtu == 128 && queued == 0 && to == 0 && from == 3,
          "h\\a\\c: mtu 128, nothing queued, 0 dropped to, 3 dropped from");

    /* Datagrams from the other clients, sent by the script. */
    DIAG("sc: waiting for peers\n");
    /* The CM0's link is faster than the ESP32's, so either may come first. */
    bool got_esp = false, got_cm0 = false;
    end = ms_now() + 3000;
    while (!(got_esp && got_cm0) && (int32_t)(ms_now() - end) < 0) {
        uint8_t buf[OW_STREAM_MTU];
        ow_peer src;
        int got = ow_stream_poll(&dev, &src, buf, sizeof buf);
        if (got == 3 && src == OW_PEER_ESP32 && !memcmp(buf, "\x01\x02\x03", 3)) got_esp = true;
        else if (got == 9 && src == OW_PEER_CM0 && !memcmp(buf, "hello cm0", 9)) got_cm0 = true;
        else if (got > 0) n_other++;
        else sleep_ms(1);
    }
    check(got_esp, "a datagram from the ESP32 arrives with src = ESP32");
    check(got_cm0, "a datagram from the CM0 arrives with src = CM0");

    /* A burst of full datagrams: the client keeps within MAIN's 768-unit
     * credit window (138 units each), refusing what doesn't fit rather than
     * overrunning MAIN, and nothing it sends is lost. (MAIN credits each
     * frame as it arrives, so with a fast MAIN few or none are refused.) */
    uint8_t big[OW_STREAM_MTU];
    memset(big, 0x5A, sizeof big);
    uint32_t d0 = ow_stream_drops(&dev);
    ow_fwgui_get_stats(&ls);
    uint32_t refused0 = ls.stream_tx_refused;
    int ok = 0;
    for (int i = 0; i < 40; i++) if (ow_stream_write(&dev, OW_PEER_ESP32, big, sizeof big) == OW_OK) ok++;
    await(-1, NULL, 0, 300);
    ow_fwgui_get_stats(&ls);
    DIAG("sc: burst of 40: %d sent, %u refused, %u lost\n", ok,
         (unsigned)(ls.stream_tx_refused - refused0), (unsigned)ls.stream_tx_lost);
    check(ok + (int)(ls.stream_tx_refused - refused0) == 40 && ok >= 5 && ls.stream_tx_lost == 0 &&
          ow_stream_drops(&dev) - d0 == ls.stream_tx_refused - refused0,
          "a burst of full datagrams: sent or refused at the client, none lost at MAIN");

    /* Keepalive: a client that only polls sends HELLO every second, so MAIN
     * keeps its link open past the 3 s expiry: the script's datagram, sent
     * 3.5 s in, arrives. */
    DIAG("sc: polling\n");
    static const uint8_t kept[1] = { 0xbb };
    check(await(OW_PEER_ESP32, kept, 1, 6000), "a link that only polls stays open (keepalive HELLO)");

    /* Link expiry: no stream frame for 3 s and MAIN closes the display's
     * link; what arrives for it meanwhile is dropped and counted, and the
     * next frame from the display opens it again. */
    await(-1, NULL, 0, 100);
    d0 = ow_stream_drops(&dev);
    DIAG("sc: quiet\n");
    sleep_ms(4500);                                  /* no OneWili calls: no keepalive */
    check(ow_stream_write(&dev, OW_PEER_DISPLAY, "back", 4) == OW_OK && await(OW_PEER_DISPLAY, "back", 4, 500),
          "the link works again after it expired");
    uint32_t d1 = ow_stream_drops(&dev);
    char msg[120];
    snprintf(msg, sizeof msg, "a datagram for the closed link is dropped (drops +%u, want +1)", (unsigned)(d1 - d0));
    check(d1 - d0 == 1, msg);

    check(n_other == 0, "no unexpected datagrams");
    DIAG("stream_check: %d checks, %d failed\n", n_checks, n_failed);
    for (;;) { await(-1, NULL, 0, 100); fw2_app_recovery_task(); }
}
