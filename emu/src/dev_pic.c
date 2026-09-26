/* dev_pic.c — board-manager coprocessor model ("keyboard controller", PIC).
 *
 * On FREE-WILi 2 a small PIC owns the 14 buttons, the charger and the 17
 * power zones, and talks to the DISPLAY RP2350 over UART1 at 62500 baud.
 * This model speaks that wire protocol byte for byte, so WiliBSP's own
 * uartkbd / picpwr / app_recovery code runs unmodified:
 *
 *  status frame, PIC -> MCU, 23 bytes, on change and ~every 500 ms:
 *    [0]=0xBD [1]=0x1D  sync
 *    [2..5]  buttons, active-low (bit map per uartkbd_parse.c)
 *    [3],[4] connection flags (audio, hot-plug, USB)
 *    [5..9]  rail state (positions per picpwr_frame.c)
 *    [10..21] charger telemetry
 *    [22]    additive checksum of bytes 0..21
 *
 *  command, MCU -> PIC: UART break wakes the PIC, it answers 0xC9, then an
 *  11-byte power frame follows: 0xB0 0x00 awake[23:0] sleep[23:0] wake wake2
 *  checksum. The PIC then walks the rails (~1 s) to the new awake mask.
 *
 * Positions and constants come from WiliBSP (MIT, (c) 2026 Dave Robins):
 * bsp/input/uartkbd_parse.c, bsp/input/picpwr_frame.c, bsp/input/uartkbd.c.
 */
#include "emu/emu.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#define FRAME_LEN 23
#define STATUS_PERIOD_US 500000u
#define RAIL_WALK_US 1000000u

static const struct { uint8_t byte, mask; } RAIL_POS[17] = {   /* payload index */
    { 3, 0x08 }, { 3, 0x10 }, { 3, 0x20 }, { 3, 0x40 }, { 3, 0x80 },
    { 4, 0x01 }, { 4, 0x02 },
    { 5, 0x02 }, { 5, 0x04 }, { 5, 0x08 }, { 5, 0x10 }, { 5, 0x20 },
    { 6, 0x01 }, { 6, 0x02 }, { 6, 0x04 }, { 6, 0x10 },
    { 7, 0x08 },
};

static const char *const BTN_NAMES[EMU_BTN_COUNT] = {
    "GREY", "YELLOW", "GREEN", "BLUE", "RED", "CENTER", "UP", "DOWN", "LEFT", "RIGHT",
    "HOME", "OK", "CANCEL", "PAGE",
};

static const char *const ZONE_NAMES[17] = {
    "SENSORS", "DISPLAY", "AUDIO", "SUBGHZ", "WIFI_BT", "FPGA", "SDCARD", "USB_HUB",
    "STATUS_LED", "RGB_LEDS", "ANALOG", "AUX_DISPLAY", "NFC_RFID", "USB_SERIAL", "CAN",
    "DEBUG_PROBE", "COMPUTE",
};

static struct {
    uint16_t buttons, sent_buttons;
    uint32_t rails, pending_rails;
    bool     walking;
    uint64_t walk_done_us, last_frame_us;
    bool     in_break, listening;
    uint8_t  cmd[16];
    int      ncmd;
    uint32_t frames_sent;
} P;

const char *emu_btn_name(unsigned b) { return b < EMU_BTN_COUNT ? BTN_NAMES[b] : "?"; }

int emu_btn_from_name(const char *n) {
    for (int i = 0; i < EMU_BTN_COUNT; i++) if (!strcasecmp(n, BTN_NAMES[i])) return i;
    if (!strcasecmp(n, "GRAY")) return EMU_BTN_GREY;
    if (!strcasecmp(n, "ENTER") || !strcasecmp(n, "SELECT")) return EMU_BTN_CENTER;
    return -1;
}

const char *emu_zone_name(unsigned zone) { return zone >= 1 && zone <= 17 ? ZONE_NAMES[zone - 1] : "?"; }

static void describe_rails(char *out, size_t cap, uint32_t rails) {
    size_t n = 0;
    out[0] = 0;
    for (unsigned z = 1; z <= 17; z++)
        if (rails & (1u << (z - 1)))
            n += (size_t)snprintf(out + n, n < cap ? cap - n : 0, "%s%s", n ? "," : "", ZONE_NAMES[z - 1]);
}

static void send_status(void) {
    uint8_t f[FRAME_LEN];
    memset(f, 0, sizeof f);
    f[0] = 0xBD; f[1] = 0x1D;
    f[2] = 0x3F; f[3] = 0x39; f[4] = 0x80; f[5] = 0x07;   /* all released */
    uint16_t b = P.buttons;
    if (b & (1u << EMU_BTN_GREY))   f[2] &= (uint8_t)~0x01;
    if (b & (1u << EMU_BTN_YELLOW)) f[2] &= (uint8_t)~0x02;
    if (b & (1u << EMU_BTN_GREEN))  f[2] &= (uint8_t)~0x04;
    if (b & (1u << EMU_BTN_BLUE))   f[2] &= (uint8_t)~0x08;
    if (b & (1u << EMU_BTN_RED))    f[2] &= (uint8_t)~0x10;
    if (b & (1u << EMU_BTN_CENTER)) f[2] &= (uint8_t)~0x20;
    if (b & (1u << EMU_BTN_DOWN))   f[3] &= (uint8_t)~0x01;
    if (b & (1u << EMU_BTN_RIGHT))  f[3] &= (uint8_t)~0x08;
    if (b & (1u << EMU_BTN_UP))     f[3] &= (uint8_t)~0x10;
    if (b & (1u << EMU_BTN_LEFT))   f[3] &= (uint8_t)~0x20;
    if (b & (1u << EMU_BTN_HOME))   f[4] &= (uint8_t)~0x80;
    if (b & (1u << EMU_BTN_OK))     f[5] &= (uint8_t)~0x01;
    if (b & (1u << EMU_BTN_CANCEL)) f[5] &= (uint8_t)~0x02;
    if (b & (1u << EMU_BTN_PAGE))   f[5] &= (uint8_t)~0x04;
    f[4] |= 0x04;                                         /* USB connected */
    for (int z = 0; z < 17; z++)
        if (P.rails & (1u << z)) f[2 + RAIL_POS[z].byte] |= RAIL_POS[z].mask;
    /* charger: 5.0 V USB host, VSYS 4.1 V, battery 3.94 V, 450 mA fast charge */
    f[10] = 24;  f[11] = 90;  f[12] = 82;  f[13] = 9;  f[14] = 62;
    f[15] = 2;   f[16] = 1;   f[17] = 0;   f[18] = 0;  f[19] = (1u << 3) | 0x01;
    f[20] = 0;   f[21] = 0;
    uint8_t sum = 0;
    for (int i = 0; i < FRAME_LEN - 1; i++) sum = (uint8_t)(sum + f[i]);
    f[FRAME_LEN - 1] = sum;
    emu_uart_to_mcu(1, f, sizeof f);
    P.sent_buttons = P.buttons;
    P.last_frame_us = emu_time_us();
    P.frames_sent++;
}

static void command_frame(void) {
    uint8_t sum = 0;
    for (int i = 0; i < 10; i++) sum = (uint8_t)(sum + P.cmd[i]);
    if (P.cmd[0] != 0xB0 || sum != P.cmd[10]) {
        emu_log("pic: bad command frame (sync %02x, checksum %02x vs %02x)", P.cmd[0], sum, P.cmd[10]);
        return;
    }
    if (P.cmd[1] != 0x00) { emu_log("pic: command id 0x%02x not modelled", P.cmd[1]); return; }
    uint32_t awake = ((uint32_t)P.cmd[2] << 16) | ((uint32_t)P.cmd[3] << 8) | P.cmd[4];
    awake &= 0x1FFFFu;
    char have[256], want[256];
    describe_rails(have, sizeof have, P.rails);
    describe_rails(want, sizeof want, awake);
    emu_log("pic: power zones %s -> %s (rail walk ~1 s)", have, want);
    P.pending_rails = awake;
    P.walking = true;
    P.walk_done_us = emu_time_us() + RAIL_WALK_US;
}

static void rx_from_mcu(emu_uart_device_t *d, const uint8_t *b, size_t n) {
    (void)d;
    for (size_t i = 0; i < n; i++) {
        if (!P.listening) continue;                 /* command-gated receiver */
        if (P.ncmd < (int)sizeof P.cmd) P.cmd[P.ncmd++] = b[i];
        if (P.ncmd == 11) { command_frame(); P.listening = false; P.ncmd = 0; }
    }
}

static void break_changed(emu_uart_device_t *d, bool on) {
    (void)d;
    if (on) { P.in_break = true; return; }
    if (P.in_break) {
        P.in_break = false;
        P.listening = true;
        P.ncmd = 0;
        static const uint8_t activity = 0xC9;
        emu_uart_to_mcu(1, &activity, 1);
    }
}

static emu_uart_device_t s_dev = { .name = "pic", .rx_from_mcu = rx_from_mcu, .break_changed = break_changed };

void emu_pic_init(uint32_t rails) {
    memset(&P, 0, sizeof P);
    P.rails = rails & 0x1FFFFu;
    emu_uart_attach(1, &s_dev);
}

void emu_pic_task(void) {
    uint64_t now = emu_time_us();
    if (P.walking && now >= P.walk_done_us) {
        P.walking = false;
        P.rails = P.pending_rails;
        send_status();
        return;
    }
    if (P.buttons != P.sent_buttons || now - P.last_frame_us >= STATUS_PERIOD_US)
        send_status();
}

/* The real PIC reports on every change, so a press and release that land
 * between two emulator frames still reach the app as two frames. */
void emu_pic_set_buttons(uint16_t mask) {
    if (mask == P.buttons) return;
    P.buttons = mask;
    send_status();
}
uint16_t emu_pic_buttons(void) { return P.buttons; }
uint32_t emu_pic_rails(void) { return P.rails; }
bool emu_rail_on(unsigned zone) { return zone >= 1 && zone <= 17 && (P.rails & (1u << (zone - 1))); }
