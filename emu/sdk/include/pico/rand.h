/* pico/rand.h — host shim. On the board these mix the RP2350's true random
 * number generator and other entropy; here they come from the PC's
 * (getrandom), so every run differs, as on hardware. */
#ifndef FW2EMU_PICO_RAND_H
#define FW2EMU_PICO_RAND_H
#include "pico.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct { uint64_t r[2]; } rng_128_t;
void     get_rand_128(rng_128_t *rand128);
uint64_t get_rand_64(void);
uint32_t get_rand_32(void);
#ifdef __cplusplus
}
#endif
#endif
