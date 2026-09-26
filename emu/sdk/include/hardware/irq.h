/* hardware/irq.h — host shim. Handlers run synchronously when an emulated
 * peripheral raises the line (e.g. DMA completion). */
#ifndef FW2EMU_HARDWARE_IRQ_H
#define FW2EMU_HARDWARE_IRQ_H
#include "pico.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef void (*irq_handler_t)(void);
#define TIMER0_IRQ_0 0
#define PIO0_IRQ_0 15
#define PIO0_IRQ_1 16
#define PIO1_IRQ_0 17
#define PIO1_IRQ_1 18
#define PIO2_IRQ_0 19
#define PIO2_IRQ_1 20
#define DMA_IRQ_0 10
#define DMA_IRQ_1 11
#define DMA_IRQ_2 12
#define DMA_IRQ_3 13
#define USBCTRL_IRQ 14
#define UART0_IRQ 33
#define UART1_IRQ 34
#define NUM_IRQS 52
#define PICO_SHARED_IRQ_HANDLER_DEFAULT_ORDER_PRIORITY 0x80
#define PICO_SHARED_IRQ_HANDLER_HIGHEST_ORDER_PRIORITY 0xff
#define PICO_SHARED_IRQ_HANDLER_LOWEST_ORDER_PRIORITY 0x00
#define PICO_DEFAULT_IRQ_PRIORITY 0x80
void irq_set_exclusive_handler(uint num, irq_handler_t handler);
void irq_add_shared_handler(uint num, irq_handler_t handler, uint8_t order_priority);
void irq_remove_handler(uint num, irq_handler_t handler);
void irq_set_enabled(uint num, bool enabled);
bool irq_is_enabled(uint num);
static inline void irq_set_priority(uint num, uint8_t p) { (void)num; (void)p; }
static inline void irq_clear(uint num) { (void)num; }
static inline void irq_set_pending(uint num) { (void)num; }
#ifdef __cplusplus
}
#endif
#endif
