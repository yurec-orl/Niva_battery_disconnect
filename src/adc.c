#include "adc.h"

/*
 * Voltage divider constants (see adc.h for derivation):
 *   Vbatt_mV    = (adc_raw * ADC_NUM) / ADC_DEN
 *   Vbatt_10mv  = (adc_raw * ADC_NUM) / ADC_DEN / 10
 *
 * Numerator factor:   3300 * 1220 / 100 = 40260   (Vref_mV * (R1+R2) / 100)
 * Denominator factor: 1024 * 220  / 100 = 2252    (ADC_BITS * R2 / 100)
 *
 * The /100 pre-scaling keeps intermediate values within uint32_t.
 * The result of (raw * NUM / DEN) is in mV; divide by 10 for 10mV units.
 *
 * Example: raw=791 (2.55V node, ~14.1V battery)
 *   791 * 40260 / 2252 / 10 = 31,845,660 / 2252 / 10 = 14141 / 10 = 1414
 *   -> 1414 units of 10mV = 14.14 V  ✓
 */
#define ADC_VREF_MV     3300UL   /* Reference voltage in mV */
#define ADC_R1          1000UL   /* Upper resistor in kΩ    */
#define ADC_R2          220UL    /* Lower resistor in kΩ    */
#define ADC_BITS        1024UL   /* 10-bit resolution       */

/* Pre-scaled numerator and denominator (divided by 100 to stay in uint32_t) */
#define ADC_NUM  ((ADC_VREF_MV * (ADC_R1 + ADC_R2)) / 100UL)  /* 40260 */
#define ADC_DEN  ((ADC_BITS    *  ADC_R2)            / 100UL)  /* 2252  */

void adc_init(void) {
    /*
     * ADC1_Init parameters:
     *   ConversionMode : SINGLE  (software-triggered, one shot)
     *   Channel        : ADC1_CHANNEL_5  (PD5/AIN5)
     *   Prescaler      : FCPU/2 -> 2MHz/2 = 1MHz (minimum valid per RM: 1-4MHz)
     *   ExtTrigger     : ADC1_EXTTRIG_TIM, DISABLED (software trigger)
     *   Align          : RIGHT (10-bit value in [9:0])
     *   SchmittTrigCh  : ADC1_SCHMITTTRIG_CHANNEL5, DISABLE
     *                    (disable Schmitt trigger on analog pin for accuracy)
     */
    ADC1_Init(ADC1_CONVERSIONMODE_SINGLE,
              ADC1_CHANNEL_5,
              ADC1_PRESSEL_FCPU_D2,
              ADC1_EXTTRIG_TIM, DISABLE,
              ADC1_ALIGN_RIGHT,
              ADC1_SCHMITTTRIG_CHANNEL5, DISABLE);

    /* Power on the ADC */
    ADC1_Cmd(ENABLE);
}

uint16_t adc_read_raw(void) {
    /* Trigger a single conversion */
    ADC1_StartConversion();

    /* Wait for End-Of-Conversion flag */
    while (ADC1_GetFlagStatus(ADC1_FLAG_EOC) == RESET)
        ;

    /* Clear the EOC flag and return the result */
    ADC1_ClearFlag(ADC1_FLAG_EOC);
    return ADC1_GetConversionValue();
}

uint16_t adc_read_voltage_10mv(void) {
    uint16_t raw = adc_read_raw();
    /*
     * Formula: Vbatt_mV = raw * Vref_mV * (R1+R2) / (ADC_BITS * R2)
     *                   = raw * ADC_NUM / ADC_DEN
     * ADC_NUM and ADC_DEN are both pre-divided by 100, so the ratio is
     * correct but the result is in mV. Divide by 10 to get 10mV units.
     */
    return (uint16_t)(((uint32_t)raw * ADC_NUM) / ADC_DEN / 10UL);
}

uint16_t adc_read_voltage_avg_10mv(void) {
    uint8_t  i;
    uint32_t sum = 0;

    // Reset ADC power after wake up from halt.
    ADC1_Cmd(DISABLE);                          /* ADON=0: power off         */
    ADC1_Cmd(ENABLE);                           /* ADON=1: power on, tSTAB   */
    { volatile uint8_t t = 15; while (t--); }   /* wait ≥3µs                 */

    for (i = 0; i < ADC_AVG_SAMPLES; i++)
        sum += adc_read_raw();

    ADC1_Cmd(DISABLE);  /* ADON=0: power off ADC -- keeps HSI from running in Active-Halt */

    return (uint16_t)((sum * ADC_NUM) / ((uint32_t)ADC_AVG_SAMPLES * ADC_DEN * 10UL));
}
