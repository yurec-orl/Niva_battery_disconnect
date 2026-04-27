#include "stm8s.h"
#include "stm8s_it.h"

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

// Button pins
#define BTN_UP_PORT     GPIOC
#define BTN_UP_PIN      GPIO_PIN_3
#define BTN_DOWN_PORT   GPIOB
#define BTN_DOWN_PIN    GPIO_PIN_4

// Disconnect threshold: 12.1V = 1210 units of 10mV
#define THRESH_10MV 1210U

/*
 * TIM4 periodic interrupt — intended for button debounce and millis counter.
 *
 * KNOWN CONFLICT: Enabling interrupts (enableInterrupts) while TM1637 is
 * active causes display corruption. Even an empty ISR is enough — the ~15us
 * entry/exit overhead at 2MHz pauses the bit-bang clock mid-transaction,
 * which resets the state machine in cheap TM1637 clone chips.
 *
 * Resolution options (TODO):
 *   A) Wrap every tm1637_* call with disableInterrupts()/enableInterrupts()
 *      inside tm1637.c itself (cleanest long-term fix).
 *   B) Keep polling debounce in the main loop (current approach).
 *   C) Use TIM4 only during Active-Halt sleep, when display is off.
 *
 * tim4_init() and enableInterrupts() are intentionally NOT called for now.
 */
static void tim4_init(void) {
    TIM4_TimeBaseInit(TIM4_PRESCALER_128, 78); /* ~5ms period at 2MHz */
    TIM4_ITConfig(TIM4_IT_UPDATE, ENABLE);
    TIM4_Cmd(ENABLE);
}

// Debounce: require this many consecutive matching samples (~20ms each loop tick)
#define DEBOUNCE_COUNT  4U   /* 4 × 20ms = 80ms */

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

/*
 * Poll-based debounce — call once per loop tick.
 * Returns TRUE on the tick when a stable press is first confirmed.
 * Resets automatically when button is released.
 */
static bool btn_poll(GPIO_TypeDef *port, GPIO_Pin_TypeDef pin, uint8_t *count) {
    if (GPIO_ReadInputPin(port, pin) == RESET) {
        if (*count < DEBOUNCE_COUNT) (*count)++;
    } else {
        *count = 0;
    }
    return (*count == DEBOUNCE_COUNT);
}

void main() {
    uint16_t voltage;
    uint8_t  btn_up_count   = 0;
    uint8_t  btn_down_count = 0;

    // Initialize PB5 as push-pull output, initially high (LED OFF)
    GPIO_Init(LED_PORT, LED_PIN, GPIO_MODE_OUT_PP_HIGH_SLOW);

    // Initialize PC4 as floating input (external divider holds the level)
    GPIO_Init(IGN_PORT, IGN_PIN, GPIO_MODE_IN_FL_NO_IT);

    // Initialize PD6 as push-pull output, initially low (solenoid OFF)
    GPIO_Init(SOL_PORT, SOL_PIN, GPIO_MODE_OUT_PP_LOW_SLOW);

    // Initialize button inputs with internal pull-ups
    GPIO_Init(BTN_UP_PORT,   BTN_UP_PIN,   GPIO_MODE_IN_PU_NO_IT);
    GPIO_Init(BTN_DOWN_PORT, BTN_DOWN_PIN, GPIO_MODE_IN_PU_NO_IT);

    // Initialize TM1637 display and ADC
    tm1637_init();
    adc_init();

    while (1) {
        // Read average battery voltage (10 samples) and display as "XX.X"
        voltage = adc_read_voltage_avg_10mv();
        tm1637_display_voltage(voltage, TM1637_BRIGHTNESS_MAX);

        // Poll buttons (debounced) — returns TRUE only while stably pressed
        bool up_pressed   = btn_poll(BTN_UP_PORT,   BTN_UP_PIN,   &btn_up_count);
        bool down_pressed = btn_poll(BTN_DOWN_PORT, BTN_DOWN_PIN, &btn_down_count);

        // Disconnect battery if voltage below threshold and ignition is OFF
        if (voltage < THRESH_10MV && !ignition_on()) {
            solenoid_on();
            GPIO_WriteLow(LED_PORT, LED_PIN);   // LED ON — disconnected
        } else {
            solenoid_off();
            if (up_pressed || down_pressed) {
                GPIO_WriteLow(LED_PORT, LED_PIN);   // LED ON — button held
            } else {
                GPIO_WriteHigh(LED_PORT, LED_PIN);  // LED OFF — normal
            }
        }

        delay_ms(20);   // ~20ms tick — 4 ticks = 80ms debounce 
    }
}
