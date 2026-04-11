#include "stm8s.h"

#include "uart.h"
#include "tm1637.h"
#include "adc.h"

// LED is on PB5, active low (anode -> 3.3V via resistor, cathode -> PB5)
#define LED_PORT    GPIOB
#define LED_PIN     GPIO_PIN_5

// F_CPU is defined by PlatformIO as 16000000UL for STM8S103
static inline void delay_ms(uint16_t ms) {
    uint32_t i;
    for (i = 0; i < ((F_CPU / 87140UL) * ms); i++)
        __asm__("nop");
}

void main() {
    uint16_t voltage;

    // Initialize PB5 as push-pull output, initially high (LED OFF)
    GPIO_Init(LED_PORT, LED_PIN, GPIO_MODE_OUT_PP_HIGH_SLOW);

    // Initialize TM1637 display and ADC
    tm1637_init();
    adc_init();

    while (1) {
        // Read average battery voltage (10 samples) and display as "XX.X"
        voltage = adc_read_voltage_avg_10mv();
        tm1637_display_voltage(voltage, TM1637_BRIGHTNESS_MAX);

        // Blink LED to show firmware is running (active low)
        GPIO_WriteLow(LED_PORT, LED_PIN);   // LED ON
        delay_ms(500);
        GPIO_WriteHigh(LED_PORT, LED_PIN);  // LED OFF
        delay_ms(500);
    }
}
