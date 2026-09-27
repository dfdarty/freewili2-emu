/* hardware/adc.h — host shim. The RP2350B has eight ADC inputs on GPIO 40-47
 * (input n = GPIO 40+n) plus the temperature sensor (input 8). The voltage
 * on each input comes from the board model (emu_adc_input_volts), so a
 * conversion returns what the real 12-bit ADC would with a 3.3 V reference. */
#ifndef FW2EMU_HARDWARE_ADC_H
#define FW2EMU_HARDWARE_ADC_H

#include "pico.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NUM_ADC_CHANNELS 9u
#define ADC_BASE_PIN 40u
#define ADC_TEMPERATURE_CHANNEL_NUM 8u

void     adc_init(void);
void     adc_gpio_init(uint gpio);
void     adc_select_input(uint input);
uint     adc_get_selected_input(void);
uint16_t adc_read(void);
void     adc_set_round_robin(uint input_mask);
void     adc_set_temp_sensor_enabled(bool enable);
void     adc_run(bool run);
void     adc_set_clkdiv(float clkdiv);
void     adc_fifo_setup(bool en, bool dreq_en, uint16_t dreq_thresh, bool err_in_fifo, bool byte_shift);
bool     adc_fifo_is_empty(void);
uint8_t  adc_fifo_get_level(void);
uint16_t adc_fifo_get(void);
uint16_t adc_fifo_get_blocking(void);
void     adc_fifo_drain(void);
void     adc_irq_set_enabled(bool enabled);

#ifdef __cplusplus
}
#endif

#endif
