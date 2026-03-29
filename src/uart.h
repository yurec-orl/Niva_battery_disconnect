#ifndef _UART_H_
#define _UART_H_

#include "stm8s.h"

/*
 * UART1 on STM8S103:
 *   PD5 -> TX
 *   PD6 -> RX
 */

void uart_init(uint32_t baudrate);
void uart_write(uint8_t data);
void uart_write_str(const char *str);
uint8_t uart_read(void);

#endif //_UART_H_