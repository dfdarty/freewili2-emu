/* hardware/structs/uart.h — host shim: the PL011 register block is
 * uart_hw_t in hardware/uart.h; these are the register bits code names.
 * Register reads here are plain memory: a read of DR does not pop the FIFO.
 * The emulator builds OneWili's FwGUI transport in its host mode, which
 * drains the UART with uart_getc() instead (emu/src/onewili_fwgui_emu.c). */
#ifndef FW2EMU_HARDWARE_STRUCTS_UART_H
#define FW2EMU_HARDWARE_STRUCTS_UART_H

#include "hardware/uart.h"

#define UART_UARTDR_DATA_BITS          0x000000ffu
#define UART_UARTRSR_OE_BITS           0x00000008u
#define UART_UARTFR_TXFE_BITS          0x00000080u
#define UART_UARTFR_RXFF_BITS          0x00000040u
#define UART_UARTFR_TXFF_BITS          0x00000020u
#define UART_UARTFR_RXFE_BITS          0x00000010u
#define UART_UARTFR_BUSY_BITS          0x00000008u
#define UART_UARTIFLS_RXIFLSEL_LSB     3u
#define UART_UARTIFLS_RXIFLSEL_BITS    0x00000038u
#define UART_UARTIFLS_TXIFLSEL_LSB     0u
#define UART_UARTIFLS_TXIFLSEL_BITS    0x00000007u
#define UART_UARTIMSC_RXIM_BITS        0x00000010u
#define UART_UARTIMSC_RTIM_BITS        0x00000040u

#endif
