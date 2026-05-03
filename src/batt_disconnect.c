#include "stm8s.h"
#include "stm8s_it.h"

#include "uart.h"
#include "tm1637.h"
#include "adc.h"

#define LED_PORT            GPIOB
#define LED_PIN             GPIO_PIN_5
// Ignition detect on PC4: HIGH = ignition ON, LOW = ignition OFF
#define IGN_PORT            GPIOC
#define IGN_PIN             GPIO_PIN_4
// Solenoid driver on PD6: HIGH = 2N7000 ON -> IRF4905 ON -> solenoid energized -> battery DISCONNECTS
#define SOL_PORT            GPIOD
#define SOL_PIN             GPIO_PIN_6
#define BTN_UP_PORT         GPIOC
#define BTN_UP_PIN          GPIO_PIN_3
#define BTN_DOWN_PORT       GPIOB
#define BTN_DOWN_PIN        GPIO_PIN_4
// Disconnect threshold: 12.1V = 1210 units of 10mV
#define THRESH_10MV         1210U

/* AWU hardware maximum is ~30 s. For longer intervals use a software
 * counter: increment on every wakeup, act every AWU_WAKEUPS_PER_CHECK.
 *
 * AWU period ≈ 2.05 s (AWUTB=13, APR=62, LSI nominal 128 kHz, ±15%).
 * 1 hour = 3600 s / 2.05 s ≈ 1756 wakeups.
 * Use 1800 for a round number (gives ~3690 s ≈ 61.5 min at nominal LSI). */
#define AWU_PERIOD_S            2U
#define CHECK_INTERVAL_S        3600U
#define AWU_WAKEUPS_PER_CHECK   (CHECK_INTERVAL_S / AWU_PERIOD_S)  /* 1800 */

/*
 * TIM4 periodic interrupt -- ~5ms period at 2MHz.
 *
 * TM1637 conflict: ISR entry/exit (~15us at 2MHz) pauses the bit-bang clock
 * mid-transaction. Fix: wrap every tm1637_* call with di/ei.
 *
 * CRITICAL -- SDCC non-reentrant calling convention:
 *   Do NOT call SPL functions from inside the ISR (e.g. GPIO_WriteReverse,
 *   TIM4_ClearITPendingBit). SDCC stores function parameters at fixed static
 *   RAM addresses. If the ISR calls a function while mainline code is mid-call
 *   to another function sharing that RAM slot, the parameter is corrupted ->
 *   wrong register written -> TRAP -> freeze.
 *   Confirmed by bare-metal test (see TIM4_BARE_METAL_EXAMPLE.md).
 *   Fix: use inline struct-member writes in the ISR (TIM4->SR1, GPIOB->ODR).
 *
 * Active-halt / AWU:
 *   MCU sleeps in active-halt between main loop iterations. AWU wakes it every
 *   ~10 seconds (AWU_TIMEBASE_2S x APR=5 internally). Buttons on EXTI_PORTB
 *   (PB4) and EXTI_PORTC (PC3) also wake the MCU mid-cycle.
 *   TIM4 counter freezes during halt and resumes automatically on wake -- the
 *   millis_counter is therefore only valid while the MCU is active.
 */

static void tim4_init(void) {
    TIM4_TimeBaseInit(TIM4_PRESCALER_128, 78);
    TIM4->SR1 &= (uint8_t)~TIM4_FLAG_UPDATE;        /* clear stale UIF (inline)  */
    TIM4->IER |= (uint8_t)TIM4_IT_UPDATE;           /* UIE=1 (inline, no SPL)    */
    enableInterrupts();
    TIM4_Cmd(ENABLE);
}

static void awu_init(void) {
    /* RM 12.3.1 order: disable -> APR -> AWUTB -> AWUEN (enable).
     * Writing AWUTB before APR violates the RM sequence and prevents AWU
     * from firing -- even with correct register values.
     *
     * SPL bug: APR_Array[AWU_TIMEBASE_2S]=23 with TBR=14 (AWUTB=1110) is
     * invalid -- RM requires APR 26-64 for AWUTB=1110. Use AWUTB=1101
     * (TBR=13), APR=62 -> period = 2^12 * 62 / 128000 Hz = ~1.98 s.
     *
     * Step 1 (MSR LSI measurement) skipped: nominal 128 kHz assumed,
     * +/-15% period variation expected.                                     */
    AWU->CSR &= (uint8_t)~AWU_CSR_AWUEN;   /* disable before reconfiguring  */
    AWU->APR  = (uint8_t)62;               /* step 2: APR first             */
    AWU->TBR  = (uint8_t)13;               /* step 3: AWUTB second          */
    (void)AWU->CSR;                        /* clear stale AWUF before enable */
    AWU->CSR |= (uint8_t)AWU_CSR_AWUEN;    /* step 4: enable                */
}

static void exti_init(void) {
    /* Buttons pull to GND when pressed -- detect falling edge.
     * GPIO must be in _IT mode for EXTI to fire. */
    EXTI_SetExtIntSensitivity(EXTI_PORT_GPIOC, EXTI_SENSITIVITY_FALL_ONLY); /* BTN_UP  PC3 */
    EXTI_SetExtIntSensitivity(EXTI_PORT_GPIOB, EXTI_SENSITIVITY_FALL_ONLY); /* BTN_DOWN PB4 */
}

#define DEBOUNCE_COUNT  4U

static inline bool ignition_on(void) {
    return GPIO_ReadInputPin(IGN_PORT, IGN_PIN) != RESET;
}
static inline void solenoid_on(void)  { GPIO_WriteHigh(SOL_PORT, SOL_PIN); }
static inline void solenoid_off(void) { GPIO_WriteLow(SOL_PORT,  SOL_PIN); }
/* LED is active-LOW: cathode on PB5, anode to VCC via resistor. */
static inline void led_on(void)  { GPIO_WriteLow(LED_PORT,  LED_PIN); }
static inline void led_off(void) { GPIO_WriteHigh(LED_PORT, LED_PIN); }

static inline void delay_ms(uint16_t ms) {
    uint32_t i;
    for (i = 0; i < ((F_CPU / 87140UL) * ms); i++)
        __asm__("nop");
}

static inline uint16_t millis(void) {
    return millis_counter;
}

void main(void) {
    uint16_t voltage;

    /* Outputs */
    GPIO_Init(LED_PORT, LED_PIN, GPIO_MODE_OUT_PP_HIGH_SLOW); /* HIGH = LED off (active-low) */
    GPIO_Init(SOL_PORT, SOL_PIN, GPIO_MODE_OUT_PP_LOW_SLOW);

    /* Inputs */
    GPIO_Init(IGN_PORT, IGN_PIN, GPIO_MODE_IN_FL_NO_IT);
    /* Button pins: commented out until 100nF debounce caps are installed.
     * Without caps the high-impedance pins pick up EMI and fire EXTI
     * continuously, preventing Active-halt from sleeping.                   */
    //GPIO_Init(BTN_UP_PORT,   BTN_UP_PIN,   GPIO_MODE_IN_PU_IT);
    //GPIO_Init(BTN_DOWN_PORT, BTN_DOWN_PIN, GPIO_MODE_IN_PU_IT);

    disableInterrupts();

    tm1637_init();
    adc_init();
    tim4_init();
    //exti_init();   /* re-enable together with button GPIO_Init above      */
    awu_init();

    // Self-test: turn test LED on and display voltage for 5 seconds.
    led_on();
    uint16_t millis_start = millis();
    while ((millis() - millis_start) < 5000U)
    {
        /* Read battery voltage (units of 10 mV: 1200 = 12.00 V).       */
        voltage = adc_read_voltage_avg_10mv();
        tm1637_display_voltage(voltage, TM1637_BRIGHTNESS_MAX);
        delay_ms(100);
    }
    led_off();

    /* AWU requires interrupts enabled (I=0) to enter Active-halt mode.
     * With I=1 the HALT instruction falls back to full Halt where AWU has
     * no effect (STM8S RM: Active-halt entered only if AWUEN=1 AND I=0). */
    enableInterrupts();

    while (1) {
        static uint16_t wakeup_count = 0;

        /* Increment wakeup counter; act every AWU_WAKEUPS_PER_CHECK ticks.
         * On the very first boot (count==0) perform an immediate check so
         * the solenoid state is correct from the start.                     */
        if (wakeup_count == 0) {
            /* Read battery voltage (units of 10 mV: 1200 = 12.00 V).       */
            voltage = adc_read_voltage_avg_10mv();

            /* Display voltage on TM1637 (e.g. "12.0"). Wrap with di/ei to
             * prevent TIM4 ISR (if active) from corrupting the bit-bang.    */
            // disableInterrupts();
            // tm1637_display_voltage(voltage, TM1637_BRIGHTNESS_MAX);
            // enableInterrupts();

            /* Activate solenoid when:
             *   - voltage below threshold (battery flat), AND
             *   - ignition is OFF (safety interlock -- never disconnect while
             *     engine is running; alternator spikes can damage ECUs).
             * Deactivate when voltage recovers or ignition turns on.        */
            if (voltage < THRESH_10MV && !ignition_on()) {
                solenoid_on();
            } else {
                solenoid_off();
            }

            /* DIAG: mirror solenoid state on LED for bench testing.
             * LED is active-LOW (cathode on pin, anode to VCC).
             * LED ON = solenoid active (battery disconnect triggered).
             * Remove once solenoid wiring is verified.                      */
            if (voltage < THRESH_10MV && !ignition_on()) {
                led_on();
            } else {
                led_off();
            }
        }

        if (++wakeup_count >= AWU_WAKEUPS_PER_CHECK) {
            wakeup_count = 0;   /* reset -- next wakeup triggers a new check */
        }

        /* Sleep until next AWU wakeup (~2 s). On wake the AWU ISR clears
         * AWUF (read AWU_CSR), then execution resumes here.                 */
        halt();
    }
}