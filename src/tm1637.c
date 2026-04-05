#include "tm1637.h"

/* 7-segment encoding for digits 0-9 */
const uint8_t TM1637_DIGITS[10] = {
    0x3F, /* 0 */
    0x06, /* 1 */
    0x5B, /* 2 */
    0x4F, /* 3 */
    0x66, /* 4 */
    0x6D, /* 5 */
    0x7D, /* 6 */
    0x07, /* 7 */
    0x7F, /* 8 */
    0x6F  /* 9 */
};

/* Small bit-delay to respect TM1637 timing (>= 1us between edges) */
static void tm1637_delay(void) {
    volatile uint8_t i;
    for (i = 0; i < 10; i++)
        __asm__("nop");
}

static void clk_high(void)  { GPIO_WriteHigh(TM1637_CLK_PORT, TM1637_CLK_PIN); tm1637_delay(); }
static void clk_low(void)   { GPIO_WriteLow(TM1637_CLK_PORT,  TM1637_CLK_PIN); tm1637_delay(); }
static void dio_high(void)  { GPIO_WriteHigh(TM1637_DIO_PORT, TM1637_DIO_PIN); tm1637_delay(); }
static void dio_low(void)   { GPIO_WriteLow(TM1637_DIO_PORT,  TM1637_DIO_PIN); tm1637_delay(); }
static void dio_input(void) { GPIO_Init(TM1637_DIO_PORT, TM1637_DIO_PIN, GPIO_MODE_IN_FL_NO_IT); }
static void dio_output(void){ GPIO_Init(TM1637_DIO_PORT, TM1637_DIO_PIN, GPIO_MODE_OUT_OD_LOW_FAST); }

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void tm1637_init(void) {
    /* CLK: open-drain output, initially high */
    GPIO_Init(TM1637_CLK_PORT, TM1637_CLK_PIN, GPIO_MODE_OUT_OD_LOW_FAST);
    GPIO_WriteHigh(TM1637_CLK_PORT, TM1637_CLK_PIN);

    /* DIO: open-drain output, initially high */
    GPIO_Init(TM1637_DIO_PORT, TM1637_DIO_PIN, GPIO_MODE_OUT_OD_LOW_FAST);
    GPIO_WriteHigh(TM1637_DIO_PORT, TM1637_DIO_PIN);

    tm1637_delay();
}

void tm1637_start(void) {
    /* Idle: CLK HIGH, DIO HIGH
     * Start: DIO goes LOW while CLK is HIGH */
    dio_high();
    clk_high();
    dio_low();
    clk_low();
}

void tm1637_stop(void) {
    /* Stop: CLK goes HIGH, then DIO goes HIGH while CLK is HIGH */
    clk_low();
    dio_low();
    clk_high();
    dio_high();
}

uint8_t tm1637_send(uint8_t byte) {
    uint8_t i;
    uint8_t ack;

    /* Send 8 bits, LSB first */
    for (i = 0; i < 8; i++) {
        clk_low();
        if (byte & 0x01)
            dio_high();
        else
            dio_low();
        byte >>= 1;
        clk_high();
    }

    /* Read ACK:
     * CLK low -> release DIO (switch to input) -> CLK high -> read DIO
     * Display pulls DIO low to ACK */
    clk_low();
    dio_input();
    clk_high();

    ack = GPIO_ReadInputPin(TM1637_DIO_PORT, TM1637_DIO_PIN) ? 1 : 0;

    clk_low();
    dio_output();

    return ack; /* 0 = ACK (display pulled low), 1 = NACK */
}

void tm1637_data(uint8_t *data, uint8_t size) {
    uint8_t i;
    tm1637_start();
    for (i = 0; i < size; i++)
        tm1637_send(data[i]);
    tm1637_stop();
}

void tm1637_display(uint8_t digit0, uint8_t digit1, uint8_t digit2, uint8_t digit3, uint8_t brightness) {
    uint8_t cmd;
    uint8_t digits[5];

    /* Step 1: send data write command */
    cmd = TM1637_CMD_DATA;
    tm1637_data(&cmd, 1);

    /* Step 2: send address + all 4 digit bytes */
    digits[0] = TM1637_CMD_ADDR; /* starting address = digit 0 */
    digits[1] = digit0;
    digits[2] = digit1;
    digits[3] = digit2;
    digits[4] = digit3;
    tm1637_data(digits, 5);

    /* Step 3: send brightness/display-on command */
    cmd = brightness;
    tm1637_data(&cmd, 1);
}

void tm1637_display_number(uint16_t number, uint8_t brightness) {
    tm1637_display(
        TM1637_DIGITS[(number / 1000) % 10],
        TM1637_DIGITS[(number / 100)  % 10],
        TM1637_DIGITS[(number / 10)   % 10],
        TM1637_DIGITS[ number         % 10],
        brightness
    );
}

void tm1637_display_voltage(uint16_t voltage_10mv, uint8_t brightness) {
    /*
     * voltage_10mv is in units of 10 mV, so 1450 = 14.50 V.
     * Divide by 10 (with rounding) to get units of 0.1 V:
     *
     *   v = (voltage_10mv + 5) / 10    e.g. 1450 -> 145, 1085 -> 109
     *
     * Display as "XX.X" (leading digit can be 0 for voltages < 10 V):
     *
     *   d0 = v / 100              (tens:  0 or 1 for 8–16 V range)
     *   d1 = (v / 10) % 10        (units digit)
     *   d2 = v % 10               (tenths digit)
     *
     * Decimal point (bit 7 = 0x80) is set on the units digit (d1).
     * d3 (rightmost) is left blank (0x00).
     *
     * Examples:
     *   1200 -> v=120 -> d0=1, d1=2, d2=0 -> "12.0"
     *   1085 -> v=109 -> d0=1, d1=0, d2=9 -> "10.9"  (rounded)
     *   1450 -> v=145 -> d0=1, d1=4, d2=5 -> "14.5"
     *    800 -> v= 80 -> d0=0, d1=8, d2=0 ->  "8.0"
     */
    uint16_t v = (voltage_10mv + 5) / 10; /* round to 0.1 V steps */
    /*
     * v is in units of 0.1 V.  For car voltages (8–16 V), v = 80–160.
     * We need to display "XX.X", e.g. v=145 -> "14.5".
     *
     *   d0 (leftmost) = tens digit  = v / 100          (0 or 1 for car voltages)
     *   d1             = units digit = (v / 10) % 10
     *   d2             = tenths digit = v % 10
     *   d3             = blank (rightmost, unused)
     *
     * Decimal point (bit 7 = 0x80) is set on the units digit (d1).
     */
    uint8_t d0     = (uint8_t)(v / 100);
    uint8_t d1     = (uint8_t)((v / 10) % 10);
    uint8_t tenths = (uint8_t)(v % 10);

    tm1637_display(
        TM1637_DIGITS[d0],
        TM1637_DIGITS[d1] | 0x80,  /* decimal point after units digit */
        TM1637_DIGITS[tenths],
        0x00,                       /* rightmost digit blank */
        brightness
    );
}
