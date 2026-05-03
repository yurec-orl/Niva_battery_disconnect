#ifndef STM8S_IT_H
#define STM8S_IT_H

#include "stm8s.h"

/*
 * SDCC STM8: interrupt vector table entries are generated ONLY in the
 * compilation unit that contains main(). Declaring handlers here with
 * INTERRUPT_HANDLER (which expands to __interrupt(N)) causes SDCC to emit
 * the correct vector table JP entry when batt_disconnect.c is compiled.
 * Without these declarations, HOME segment stays 7 bytes (reset vector only)
 * and the IRQ slots contain random code bytes -> no interrupt fires.
 */
INTERRUPT_HANDLER(TLI_IRQHandler,                   0);
INTERRUPT_HANDLER(AWU_IRQHandler,                   1);
INTERRUPT_HANDLER(CLK_IRQHandler,                   2);
INTERRUPT_HANDLER(EXTI_PORTA_IRQHandler,            3);
INTERRUPT_HANDLER(EXTI_PORTB_IRQHandler,            4);
INTERRUPT_HANDLER(EXTI_PORTC_IRQHandler,            5);
INTERRUPT_HANDLER(EXTI_PORTD_IRQHandler,            6);
INTERRUPT_HANDLER(EXTI_PORTE_IRQHandler,            7);
INTERRUPT_HANDLER(SPI_IRQHandler,                   10);
INTERRUPT_HANDLER(TIM1_UPD_OVF_TRG_BRK_IRQHandler,  11);
INTERRUPT_HANDLER(TIM1_CAP_COM_IRQHandler,          12);
INTERRUPT_HANDLER(TIM2_UPD_OVF_BRK_IRQHandler,      13);
INTERRUPT_HANDLER(TIM2_CAP_COM_IRQHandler,          14);
INTERRUPT_HANDLER(UART1_TX_IRQHandler,              17);
INTERRUPT_HANDLER(UART1_RX_IRQHandler,              18);
INTERRUPT_HANDLER(I2C_IRQHandler,                   19);
INTERRUPT_HANDLER(ADC1_IRQHandler,                  22);
INTERRUPT_HANDLER(TIM4_UPD_OVF_IRQHandler,          23);
INTERRUPT_HANDLER(EEPROM_EEC_IRQHandler,            24);
INTERRUPT_HANDLER_TRAP(TRAP_IRQHandler);

/* Shared state updated by TIM4 ISR */
extern volatile uint32_t millis_counter;
extern volatile uint8_t  btn_up_hist;
extern volatile uint8_t  btn_down_hist;

/* Button wakeup flags set by EXTI ISRs, cleared by main loop */
extern volatile bool btn_up_pressed;
extern volatile bool btn_down_pressed;

#endif /* STM8S_IT_H */
