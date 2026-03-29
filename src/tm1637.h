#ifndef _TM1637_H_
#define _TM1637_H_

#include "stm8s.h"

/*
 * TM1637 2-wire bit-bang driver for STM8S103
 *
 * Pin mapping (see README_STM8S.md):
 *   PC5 -> CLK
 *   PC6 -> DIO
 *   PC3 -> Display VCC switch (P-FET gate, active LOW) -- controlled separately
 */

#define TM1637_CLK_PORT     GPIOC
#define TM1637_CLK_PIN      GPIO_PIN_5
#define TM1637_DIO_PORT     GPIOC
#define TM1637_DIO_PIN      GPIO_PIN_6

/* TM1637 commands */
#define TM1637_CMD_DATA     0x40    /* Write data to display */
#define TM1637_CMD_ADDR     0xC0    /* Set starting address (digit 0) */
#define TM1637_CMD_CTRL     0x88    /* Display ON, brightness 1/16 (lowest) */
#define TM1637_BRIGHTNESS_MAX  0x8F /* Display ON, brightness 14/16 (highest) */

/* Segment encoding for digits 0-9 (standard 7-segment) */
extern const uint8_t TM1637_DIGITS[10];

/* Initialize GPIO pins for TM1637 */
void tm1637_init(void);

/* Low-level protocol primitives */
void tm1637_start(void);
void tm1637_stop(void);
uint8_t tm1637_send(uint8_t byte);  /* Returns 0 if ACK received, 1 if NACK */

/* Send a buffer of bytes between start/stop automatically */
void tm1637_data(uint8_t *data, uint8_t size);

/* High-level: write 4 digits to display, set brightness (0x88..0x8F) */
void tm1637_display(uint8_t digit0, uint8_t digit1, uint8_t digit2, uint8_t digit3, uint8_t brightness);

/* High-level: display a 0-9999 integer */
void tm1637_display_number(uint16_t number, uint8_t brightness);

/*
 * High-level: display a battery voltage in XX.X format.
 *
 * voltage_10mv: voltage in units of 10 mV  (e.g. 1200 = 12.00 V)
 *
 * Shows two integer digits, a decimal point, and one fractional digit:
 *   1200 -> "12.0"
 *   1085 -> "10.9"  (10.85 rounded to 10.9 in units of 0.1 V)
 *   Display positions: d0=tens, d1=units (with DP), d2=tenths, d3=blank
 */
void tm1637_display_voltage(uint16_t voltage_10mv, uint8_t brightness);

#endif /* _TM1637_H_ */
