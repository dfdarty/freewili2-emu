/* sd_example — write a file on the SD card (owned by the MAIN CPU) in
 * pieces, read it back and list its folder, over WiliBSP's OneWili client.
 * The snippet in docs/main-link.md is this file's main(). */
#include "fw2.h"
#include "platform/diag.h"
#include "input/app_recovery_onewili.h"     /* onewili.h, onewili_sd.h + recovery-aware link */
#include <stdio.h>
#include <string.h>

static ow_device dev;                       /* ~37 KB: static, never on the stack */
static char buf[4096];

static void show(const char *name, bool is_dir, uint32_t size, void *user) {
    (void)user;
    DIAG("  %s%s %lu\n", name, is_dir ? "/" : "", (unsigned long)size);
}

int main(void) {
    board_init();
    fw2_app_recovery_init();                /* waits for the SDCARD power zone */
    while (fw2_app_recovery_open_onewili(&dev) != OW_OK) fw2_app_recovery_sleep_ms(100);
    fw2_app_recovery_wrap_sd();

    ow_sd_mkdir(&dev, "/data");             /* fails harmlessly if it exists */
    ow_sd_file f;
    size_t written = 0;
    if (ow_sd_open(&dev, &f, "/data/log.csv", OW_SD_WRITE) == OW_OK) {
        for (int i = 0; i < 200; i++) {     /* one small write per line: each <= 1 KB */
            char line[32];
            int n = snprintf(line, sizeof line, "%d,%d\n", i, i * i);
            if (ow_sd_write(&f, line, (size_t)n) == OW_OK) written += (size_t)n;
        }
        if (ow_sd_close(&f) != OW_OK)       /* a lost write chunk shows up here */
            DIAG("sd_example: close failed (sdfs %d)\n", (int)ow_sd_last_error());
    }
    size_t got = 0;
    if (ow_sd_get_mem(&dev, "/data/log.csv", buf, sizeof buf, &got) == OW_OK)
        DIAG("sd_example: wrote %u bytes, read back %u\n", (unsigned)written, (unsigned)got);
    ow_sd_list(&dev, "/data", show, 0);
    DIAG("sd_example: done\n");
    for (;;) { fw2_app_recovery_task(); fw2_app_recovery_sleep_ms(100); }
}
