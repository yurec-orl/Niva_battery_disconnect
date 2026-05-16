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
// Door switch on PC3: LOW = door open (EXTI_PORTC, vector 5)
#define DOOR_PORT           GPIOC
#define DOOR_PIN            GPIO_PIN_3
#define SOLENOID_MAX_RETRIES  3U

/* Display power: PC7, P-FET gate -- LOW = display ON, HIGH = display OFF */
#define DISP_PWR_PORT       GPIOC
#define DISP_PWR_PIN        GPIO_PIN_7

/* Voltage disconnect threshold stored in EEPROM as tenths of volt (121 = 12.1V) */
#define EEPROM_THRESH_ADDR  ((uint32_t)0x4000)
#define THRESH_DEFAULT      121U    /* 12.1 V */
#define THRESH_MIN          100U    /* 10.0 V */
#define THRESH_MAX          130U    /* 13.0 V */

/* AWU hardware maximum is ~30 s. For longer intervals use a software
 * counter: increment on every wakeup, act every AWU_WAKEUPS_PER_CHECK.
 *
 * AWU period ≈ 2.05 s (AWUTB=13, APR=62, LSI nominal 128 kHz, ±15%).
 * 1 hour = 3600 s / 2.05 s ≈ 1756 wakeups.
 * Use 1800 for a round number (gives ~3690 s ≈ 61.5 min at nominal LSI). */
#define AWU_PERIOD_S            2U
#define CHECK_INTERVAL_S        3600U
#define AWU_WAKEUPS_PER_CHECK   (CHECK_INTERVAL_S / AWU_PERIOD_S)  /* 1800 */

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

    /* Power off main voltage regulator during Active-Halt. */
    CLK->ICKR |= (uint8_t)CLK_ICKR_SWUAH;

    /* Flash power-down in Active-Halt/Halt (FLASH_CR1 bit 3). */
    FLASH->CR1 |= (uint8_t)0x08;
}

static void exti_init(void) {
    /* Door switch on PC3 (EXTI_PORTC, vector 5): wake on falling edge
     * (door opens = pin goes LOW).
     * Both buttons on PORTD (EXTI_PORTD, vector 6): BTN_UP=PD3, BTN_DOWN=PD2.
     * ISR distinguishes pins by reading IDR. */
    EXTI_SetExtIntSensitivity(EXTI_PORT_GPIOC, EXTI_SENSITIVITY_FALL_ONLY);
    EXTI_SetExtIntSensitivity(EXTI_PORT_GPIOD, EXTI_SENSITIVITY_FALL_ONLY);
}

#define DEBOUNCE_COUNT  4U

static bool ignition_on(void) {
    return GPIO_ReadInputPin(IGN_PORT, IGN_PIN) != RESET;
}
static void solenoid_on(void)  { GPIO_WriteHigh(SOL_PORT, SOL_PIN); }
static void solenoid_off(void) { GPIO_WriteLow(SOL_PORT,  SOL_PIN); }
/* LED is active-LOW */
static void led_on(void)  { GPIO_WriteLow(LED_PORT,  LED_PIN); }
static void led_off(void) { GPIO_WriteHigh(LED_PORT, LED_PIN); }

static void delay_ms(uint16_t ms) {
    uint32_t i;
    for (i = 0; i < ((F_CPU / 697120UL) * ms); i++)
        __asm__("nop");
}

static uint16_t wakeup_count            = 0;
static uint8_t  solenoid_pulse_count    = 0;
static uint8_t  threshold_x10           = THRESH_DEFAULT;

/* --- Display power (PC7, P-FET gate: LOW = ON, HIGH = OFF) --- */
static void display_power_on(void)  { GPIO_WriteHigh(DISP_PWR_PORT, DISP_PWR_PIN); }
static void display_power_off(void) { GPIO_WriteLow(DISP_PWR_PORT,  DISP_PWR_PIN); }

/* --- Threshold persistence (bare-metal EEPROM at 0x4000) --- */
static void load_threshold(void) {
    uint8_t val = FLASH_ReadByte(EEPROM_THRESH_ADDR);
    threshold_x10 = (val >= THRESH_MIN && val <= THRESH_MAX) ? val : (uint8_t)THRESH_DEFAULT;
}

static void save_threshold(void) {
    FLASH_Unlock(FLASH_MEMTYPE_DATA);
    FLASH_ProgramByte(EEPROM_THRESH_ADDR, threshold_x10);
    FLASH_Lock(FLASH_MEMTYPE_DATA);
}

/* --- Display helpers --- */
static void display_show_threshold(void) {
    disableInterrupts();
    tm1637_display_voltage((uint16_t)threshold_x10 * 10U, TM1637_BRIGHTNESS_MAX);
    enableInterrupts();
}

static void display_blink_boundary(void) {
    uint8_t b;
    for (b = 0; b < 3; b++) {
        display_power_off(); delay_ms(25);
        display_power_on();  delay_ms(25);
    }
}

/* --- Settings UI --- */
static void ui_run(void) {
    uint8_t  old_threshold_x10 = threshold_x10;
    uint16_t timeout = 0;

    /* First press: wake display and show current value; do NOT change threshold. */
    btn_up_pressed   = FALSE;
    btn_down_pressed = FALSE;
    display_power_on();
    display_show_threshold();

    while (timeout < 5000U) {
        delay_ms(50);
        timeout += 50U;

        if (btn_up_pressed || btn_down_pressed) {
            timeout = 0U;       /* reset on any press, including boundary */

            if (btn_up_pressed) {           /* UP wins if both pressed */
                if (threshold_x10 < THRESH_MAX) {
                    threshold_x10++;
                    display_show_threshold();
                } else {
                    display_blink_boundary();
                    display_show_threshold();
                }
            } else {
                if (threshold_x10 > THRESH_MIN) {
                    threshold_x10--;
                    display_show_threshold();
                } else {
                    display_blink_boundary();
                    display_show_threshold();
                }
            }
            btn_up_pressed   = FALSE;
            btn_down_pressed = FALSE;
        }
    }

    if (old_threshold_x10 != threshold_x10) {
        save_threshold();
    }
    display_power_off();
    wakeup_count = 0;   /* full AWU interval before next solenoid check */
}

void main(void) {
    uint16_t voltage;
    uint8_t  i;

    /* Outputs */
    GPIO_Init(LED_PORT,      LED_PIN,      GPIO_MODE_OUT_PP_HIGH_SLOW); /* HIGH = LED off (active-low) */
    GPIO_Init(SOL_PORT,      SOL_PIN,      GPIO_MODE_OUT_PP_LOW_SLOW);
    GPIO_Init(DISP_PWR_PORT, DISP_PWR_PIN, GPIO_MODE_OUT_PP_LOW_SLOW); /* LOW = display OFF */

    /* Inputs */
    GPIO_Init(IGN_PORT,  IGN_PIN,  GPIO_MODE_IN_FL_NO_IT);
    GPIO_Init(GPIOD, GPIO_PIN_4, GPIO_MODE_IN_PU_NO_IT);

    disableInterrupts();

    /* IT-mode pins must be initialised AFTER disableInterrupts(). */
    GPIO_Init(DOOR_PORT,     DOOR_PIN,     GPIO_MODE_IN_PU_IT);
    GPIO_Init(BTN_UP_PORT,   BTN_UP_PIN,   GPIO_MODE_IN_PU_IT);
    GPIO_Init(BTN_DOWN_PORT, BTN_DOWN_PIN, GPIO_MODE_IN_PU_IT);

    exti_init();        /* set FALL_ONLY sensitivity before re-enabling IRQs  */

    tm1637_init();      /* Init display. */
    adc_init();
    awu_init();
    load_threshold();   /* Load threshold from EEPROM. */

    /* Self-test: LED on, display voltage for ~5 s (50 x 100 ms). */
    led_on();
    display_power_on();
    for (i = 0; i < 50; i++) {
        voltage = adc_read_voltage_avg_10mv();
        disableInterrupts();
        tm1637_display_voltage(voltage, TM1637_BRIGHTNESS_MAX);
        enableInterrupts();
        delay_ms(100);
    }
    display_power_off();

    /* AWU requires interrupts enabled (I=0) to enter Active-halt mode.
     * With I=1 the HALT instruction falls back to full Halt where AWU has
     * no effect (STM8S RM: Active-halt entered only if AWUEN=1 AND I=0). */
    enableInterrupts();

    while (1) {

        // Make sure LED does not stay on.
        led_off();

        /* Increment wakeup counter; act every AWU_WAKEUPS_PER_CHECK ticks.
         * On the very first boot (count==0) perform an immediate check so
         * the solenoid state is correct from the start.                     */
        if (wakeup_count == 0) {
            /* Read battery voltage (units of 10 mV: 1200 = 12.00 V).       */
            voltage = adc_read_voltage_avg_10mv();

            /* Pulse solenoid when voltage is low and ignition is off.
             * Up to SOLENOID_MAX_RETRIES attempts; retries are spaced one
             * full AWU interval (~1 hour) apart.
             * Normally the MCU loses power once the disconnect fires.
             * The retry cap protects the solenoid and gate if the switch
             * malfunctions and the battery stays connected.                 */
            if (voltage < (uint16_t)threshold_x10 * 10U && !ignition_on()) {
                led_on();   // For indication - will stay on until next main loop iteration after halt.
                if (solenoid_pulse_count < SOLENOID_MAX_RETRIES) {
                    solenoid_on();
                    delay_ms(1000);
                    solenoid_off();
                    solenoid_pulse_count++;
                }
            }
        }

        if (++wakeup_count >= AWU_WAKEUPS_PER_CHECK) {
            wakeup_count = 0;   /* reset -- next wakeup triggers a new check */
        }

        /* Button press: open settings UI.
         * btn_up_pressed / btn_down_pressed are set by the EXTI ISRs
         * (stm8s_it.c). ui_run() consumes and clears them internally.      */
        if (btn_up_pressed || btn_down_pressed) {
            ui_run();
        }

        /* Sleep until next AWU wakeup (~2 s). On wake the AWU ISR clears
         * AWUF (read AWU_CSR), then execution resumes here.                 */
        halt();
    }
}