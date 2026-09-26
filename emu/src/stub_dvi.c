/* stub_dvi.c — DVI (HSTX) output is not modelled yet.
 *
 * These keep code that references the DVI driver (e.g. agentio's capture of
 * the DVI surface) linking. hstx_dvi_region_base() returns NULL, so callers
 * see "no DVI surface", which is what they also see on hardware before
 * hstx_dvi_init(). A DVI window is planned for a later phase. */
#include "display/hstx_dvi.h"
#include "emu/emu.h"

void hstx_dvi_init(int vid_w, int vid_h) { emu_log("dvi: hstx_dvi_init(%d, %d) — DVI output not emulated yet", vid_w, vid_h); }
void hstx_dvi_set_geometry(int vid_w, int vid_h) { (void)vid_w; (void)vid_h; }
uint16_t *hstx_dvi_video_base(void) { return 0; }
uint16_t *hstx_dvi_region_base(void) { return 0; }
int hstx_dvi_video_stride(void) { return 0; }
int hstx_dvi_video_w(void) { return 0; }
int hstx_dvi_video_h(void) { return 0; }
int hstx_dvi_region_h(void) { return 0; }
void hstx_dvi_enable(bool on) { (void)on; }
