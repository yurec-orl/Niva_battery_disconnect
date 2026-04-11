#include "stm8s.h"

#include "uart.h"
#include "tm1637.h"
#include "adc.h"

// LED is on PB5, active low (anode -> 3.3V via resistor, cathode -> PB5)
#define LED_PORT    GPIOB
#define LED_PIN     GPIO_PIN_5

// Ignition detect on PC4: HIGH = ignition ON, LOW = ignition OFF
#define IGN_PORT    GPIOC
#define IGN_PIN     GPIO_PIN_4

static inline bool ignition_on(void) {
    return GPIO_ReadInputPin(IGN_PORT, IGN_PIN) != RESET;
}

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

    // Initialize PC4 as floating input (external divider holds the level)
    GPIO_Init(IGN_PORT, IGN_PIN, GPIO_MODE_IN_FL_NO_IT);

    // Initialize TM1637 display and ADC
    tm1637_init();
    adc_init();

    while (1) {
        // Read average battery voltage (10 samples) and display as "XX.X"
        voltage = adc_read_voltage_avg_10mv();
        tm1637_display_voltage(voltage, TM1637_BRIGHTNESS_MAX);

        // LED ON when ignition is detected, OFF otherwise (active low)
        if (ignition_on()) {
            GPIO_WriteLow(LED_PORT, LED_PIN);   // LED ON
        } else {
            GPIO_WriteHigh(LED_PORT, LED_PIN);  // LED OFF
        }

        delay_ms(100);
    }
}
