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

// Solenoid driver on PD6: HIGH = 2N7000 ON -> IRF4905 ON -> solenoid energized -> battery DISCONNECTS
#define SOL_PORT    GPIOD
#define SOL_PIN     GPIO_PIN_6

// Disconnect threshold: 11.9V = 1190 units of 10mV
#define THRESH_10MV 1190U

static inline bool ignition_on(void) {
    return GPIO_ReadInputPin(IGN_PORT, IGN_PIN) != RESET;
}

static inline void solenoid_on(void) {
    GPIO_WriteHigh(SOL_PORT, SOL_PIN);
}

static inline void solenoid_off(void) {
    GPIO_WriteLow(SOL_PORT, SOL_PIN);
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

    // Initialize PD6 as push-pull output, initially low (solenoid OFF)
    GPIO_Init(SOL_PORT, SOL_PIN, GPIO_MODE_OUT_PP_LOW_SLOW);

    // Initialize TM1637 display and ADC
    tm1637_init();
    adc_init();

    while (1) {
        // Read average battery voltage (10 samples) and display as "XX.X"
        voltage = adc_read_voltage_avg_10mv();
        tm1637_display_voltage(voltage, TM1637_BRIGHTNESS_MAX);

        // Disconnect battery if voltage below threshold and ignition is OFF
        if (voltage < THRESH_10MV && !ignition_on()) {
            solenoid_on();
            GPIO_WriteLow(LED_PORT, LED_PIN);   // LED ON — disconnected
        } else {
            solenoid_off();
            GPIO_WriteHigh(LED_PORT, LED_PIN);  // LED OFF — connected
        }

        delay_ms(100);
    }
}
