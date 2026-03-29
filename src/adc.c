#include "adc.h"

/*
 * Voltage divider constants (see adc.h for derivation):
 *   Vbatt_10mv = (adc_raw * 40260UL) / 2253UL
 *
 * Numerator factor:   3300 * 1220 / 100 = 40260
 * Denominator factor: 1024 * 220  / 100 = 2252.8 -> rounded to 2253
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
     *   Channel        : ADC1_CHANNEL_4  (PD3/AIN4)
     *   Prescaler      : FCPU/8 -> 16MHz/8 = 2MHz (within 1-4MHz spec)
     *   ExtTrigger     : ADC1_EXTTRIG_TIM, DISABLED (software trigger)
     *   Align          : RIGHT (10-bit value in [9:0])
     *   SchmittTrigCh  : ADC1_SCHMITTTRIG_CHANNEL4, DISABLE
     *                    (disable Schmitt trigger on analog pin for accuracy)
     */
    ADC1_Init(ADC1_CONVERSIONMODE_SINGLE,
              ADC1_CHANNEL_4,
              ADC1_PRESSEL_FCPU_D8,
              ADC1_EXTTRIG_TIM, DISABLE,
              ADC1_ALIGN_RIGHT,
              ADC1_SCHMITTTRIG_CHANNEL4, DISABLE);

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
    /* Vbatt in units of 10 mV (0.01 V resolution) */
    return (uint16_t)(((uint32_t)raw * ADC_NUM) / ADC_DEN);
}
