#ifndef _ADC_H_
#define _ADC_H_

#include "stm8s.h"

/*
 * ADC driver for battery voltage sensing (STM8S103)
 *
 * Hardware:
 *   PD5 / AIN5  -- ADC channel 5 (battery voltage divider)
 *   Voltage divider: R1 = 1M, R2 = 220k
 *   Ratio = 220k / (1000k + 220k) = 220/1220 = 0.18033
 *
 * Conversion formula:
 *   Vadc  = adc_raw * 3.3V / 1024
 *   Vbatt = Vadc / ratio = adc_raw * 3.3 / 1024 / 0.18033
 *         = adc_raw * 3.3 * 1220 / (1024 * 220)
 *
 * To avoid floating point we work in units of 10 mV (0.01 V):
 *   Vbatt_10mv = adc_raw * (3300 * 1220) / (1024 * 220)
 *              = adc_raw * 4026000 / 225280
 *
 * Simplified integer ratio (divide then adjust):
 *   Vbatt_10mv = (adc_raw * 40260UL) / 2253UL   (pre-divided by 100)
 *   (error < 0.01% compared to full formula)
 *
 * Example:
 *   12.0V -> Vadc = 2.164V -> adc_raw = 671 -> Vbatt_10mv = 1200 -> 12.00V
 */

/* Initialize ADC1, single conversion, right-aligned, channel 4 */
void adc_init(void);

/*
 * Perform one ADC conversion on channel 4 (battery voltage divider).
 * Returns raw 10-bit ADC count (0..1023).
 */
uint16_t adc_read_raw(void);

/*
 * Read battery voltage in units of 10 mV.
 * E.g. returns 1200 for 12.00V, 1080 for 10.80V.
 */
uint16_t adc_read_voltage_10mv(void);

/*
 * Take ADC_AVG_SAMPLES consecutive raw readings, average them,
 * then convert to voltage in units of 10 mV.
 * Reduces noise compared to a single reading.
 */
#define ADC_AVG_SAMPLES  10
uint16_t adc_read_voltage_avg_10mv(void);

#endif /* _ADC_H_ */
