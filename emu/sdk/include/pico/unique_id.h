/* pico/unique_id.h — host shim. The id is the flash chip's 64-bit unique id
 * on the board; here it is fixed (E6 61 64 08 43 2A 7B 15) unless the run
 * gives --board-id HEX16, so two emulators can tell each other apart. */
#ifndef FW2EMU_PICO_UNIQUE_ID_H
#define FW2EMU_PICO_UNIQUE_ID_H
#include "pico.h"
#ifdef __cplusplus
extern "C" {
#endif
#define PICO_UNIQUE_BOARD_ID_SIZE_BYTES 8
typedef struct { uint8_t id[PICO_UNIQUE_BOARD_ID_SIZE_BYTES]; } pico_unique_board_id_t;
void pico_get_unique_board_id(pico_unique_board_id_t *id_out);
void pico_get_unique_board_id_string(char *id_out, uint len);
#ifdef __cplusplus
}
#endif
#endif
