/* SEGGER_RTT.h — host replacement for SEGGER RTT.
 *
 * Same API as the real library. Channel 0 (DIAG) prints to stdout and, with
 * --rtt, is served on TCP 127.0.0.1:9090; channel 1 (agentio) is served on
 * 127.0.0.1:9091 — the ports openocd uses for `fw rtt`, so WiliBSP's own
 * `fw press` / `fw screenshot` tooling can drive the emulator unchanged. */
#ifndef SEGGER_RTT_H
#define SEGGER_RTT_H
#include <stdarg.h>
#ifdef __cplusplus
extern "C" {
#endif
#define SEGGER_RTT_MODE_NO_BLOCK_SKIP      (0)
#define SEGGER_RTT_MODE_NO_BLOCK_TRIM      (1)
#define SEGGER_RTT_MODE_BLOCK_IF_FIFO_FULL (2)
#define SEGGER_RTT_MODE_MASK               (3)
#define SEGGER_RTT_MODE_DEFAULT            SEGGER_RTT_MODE_NO_BLOCK_SKIP
#define SEGGER_RTT_MAX_NUM_UP_BUFFERS   3
#define SEGGER_RTT_MAX_NUM_DOWN_BUFFERS 3
void     SEGGER_RTT_Init(void);
int      SEGGER_RTT_ConfigUpBuffer(unsigned BufferIndex, const char *sName, void *pBuffer, unsigned BufferSize, unsigned Flags);
int      SEGGER_RTT_ConfigDownBuffer(unsigned BufferIndex, const char *sName, void *pBuffer, unsigned BufferSize, unsigned Flags);
int      SEGGER_RTT_SetFlagsUpBuffer(unsigned BufferIndex, unsigned Flags);
unsigned SEGGER_RTT_Write(unsigned BufferIndex, const void *pBuffer, unsigned NumBytes);
unsigned SEGGER_RTT_WriteNoLock(unsigned BufferIndex, const void *pBuffer, unsigned NumBytes);
unsigned SEGGER_RTT_WriteString(unsigned BufferIndex, const char *s);
unsigned SEGGER_RTT_PutChar(unsigned BufferIndex, char c);
unsigned SEGGER_RTT_Read(unsigned BufferIndex, void *pBuffer, unsigned BufferSize);
unsigned SEGGER_RTT_ReadNoLock(unsigned BufferIndex, void *pBuffer, unsigned BufferSize);
unsigned SEGGER_RTT_HasData(unsigned BufferIndex);
unsigned SEGGER_RTT_HasDataUp(unsigned BufferIndex);
unsigned SEGGER_RTT_GetAvailWriteSpace(unsigned BufferIndex);
unsigned SEGGER_RTT_GetBytesInBuffer(unsigned BufferIndex);
int      SEGGER_RTT_HasKey(void);
int      SEGGER_RTT_GetKey(void);
int      SEGGER_RTT_printf(unsigned BufferIndex, const char *sFormat, ...);
int      SEGGER_RTT_vprintf(unsigned BufferIndex, const char *sFormat, va_list *pParamList);
#ifdef __cplusplus
}
#endif
#endif
