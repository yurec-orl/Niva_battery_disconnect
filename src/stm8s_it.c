#include "stm8s.h"
#include "stm8s_it.h"

/* Definitions of shared ISR state (declared extern in stm8s_it.h) */
volatile uint32_t millis_counter = 0;
volatile uint8_t  btn_up_hist    = 0xFF;   /* 0xFF = released */
volatile uint8_t  btn_down_hist  = 0xFF;

/* Non-maskable interrupts */
INTERRUPT_HANDLER_TRAP(TRAP_IRQHandler)                 { while (1); }
INTERRUPT_HANDLER(TLI_IRQHandler,          0)           {}
INTERRUPT_HANDLER(AWU_IRQHandler,          1)           {}
INTERRUPT_HANDLER(CLK_IRQHandler,          2)           {}
INTERRUPT_HANDLER(EXTI_PORTA_IRQHandler,   3)           {}
INTERRUPT_HANDLER(EXTI_PORTB_IRQHandler,   4)           {}
INTERRUPT_HANDLER(EXTI_PORTC_IRQHandler,   5)           {}
INTERRUPT_HANDLER(EXTI_PORTD_IRQHandler,   6)           {}
INTERRUPT_HANDLER(EXTI_PORTE_IRQHandler,   7)           {}
INTERRUPT_HANDLER(SPI_IRQHandler,          10)          {}
INTERRUPT_HANDLER(TIM1_UPD_OVF_TRG_BRK_IRQHandler, 11)  {}
INTERRUPT_HANDLER(TIM1_CAP_COM_IRQHandler, 12)          {}
INTERRUPT_HANDLER(TIM2_UPD_OVF_BRK_IRQHandler, 13)      {}
INTERRUPT_HANDLER(TIM2_CAP_COM_IRQHandler, 14)          {}
INTERRUPT_HANDLER(UART1_TX_IRQHandler,     17)          {}
INTERRUPT_HANDLER(UART1_RX_IRQHandler,     18)          {}
INTERRUPT_HANDLER(I2C_IRQHandler,          19)          {}
INTERRUPT_HANDLER(ADC1_IRQHandler,         22)          {}

/* TIM4 overflow — fires every ~5ms */
INTERRUPT_HANDLER(TIM4_UPD_OVF_IRQHandler, 23) {
    TIM4_ClearITPendingBit(TIM4_IT_UPDATE);

    millis_counter++;

    /* Shift-register debounce: shift current pin state in from the right.
       After 8 samples (~40ms) of stable LOW  -> btn_*_hist == 0x00 = pressed.
       After 8 samples (~40ms) of stable HIGH -> btn_*_hist == 0xFF = released.
       NOTE: not currently used — see tim4_init() comment in batt_disconnect.c
       about the TIM4 ISR / TM1637 display conflict. */
    btn_up_hist   = (uint8_t)((btn_up_hist   << 1) | (GPIO_ReadInputPin(GPIOC, GPIO_PIN_3) ? 1u : 0u));
    btn_down_hist = (uint8_t)((btn_down_hist << 1) | (GPIO_ReadInputPin(GPIOB, GPIO_PIN_4) ? 1u : 0u));
}

INTERRUPT_HANDLER(EEPROM_EEC_IRQHandler,   24)       {}
