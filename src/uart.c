#include "uart.h"

void uart_init(uint32_t baudrate) {
    UART1_Init(baudrate,
               UART1_WORDLENGTH_8D,
               UART1_STOPBITS_1,
               UART1_PARITY_NO,
               UART1_SYNCMODE_CLOCK_DISABLE,
               UART1_MODE_TXRX_ENABLE);
    UART1_Cmd(ENABLE);
}

void uart_write(uint8_t data) {
    while (UART1_GetFlagStatus(UART1_FLAG_TXE) == RESET);
    UART1_SendData8(data);
    while (UART1_GetFlagStatus(UART1_FLAG_TC) == RESET);
}

void uart_write_str(const char *str) {
    while (*str) {
        uart_write((uint8_t)*str++);
    }
}

uint8_t uart_read(void) {
    while (UART1_GetFlagStatus(UART1_FLAG_RXNE) == RESET);
    return UART1_ReceiveData8();
}