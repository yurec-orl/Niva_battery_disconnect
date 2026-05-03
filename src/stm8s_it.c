#include "stm8s.h"
#include "stm8s_it.h"

volatile uint32_t millis_counter = 0;
volatile uint8_t  btn_up_hist    = 0xFF;
volatile uint8_t  btn_down_hist  = 0xFF;
volatile bool     btn_up_pressed   = FALSE;
volatile bool     btn_down_pressed = FALSE;

INTERRUPT_HANDLER_TRAP(TRAP_IRQHandler) {
    /* Blink LED rapidly to signal TRAP (illegal opcode / bad vector) */
    while (1) {
        GPIOB->ODR ^= (uint8_t)GPIO_PIN_5;
        { volatile uint32_t d; for (d = 0; d < 2300UL; d++); }
    }
}

INTERRUPT_HANDLER(TLI_IRQHandler,                    0) {}
INTERRUPT_HANDLER(AWU_IRQHandler,                    1) {
    /* AWUF flag is READ-TO-CLEAR (not write-to-clear).
     * Per STM8S RM and article: reading AWU_CSR clears AWUF.
     * Must use a volatile local so SDCC doesn't optimise the read away. */
    volatile uint8_t csr = AWU->CSR;
    (void)csr;
}
INTERRUPT_HANDLER(CLK_IRQHandler,                    2) {}
INTERRUPT_HANDLER(EXTI_PORTA_IRQHandler,             3) {}
INTERRUPT_HANDLER(EXTI_PORTB_IRQHandler,             4) {
    /* BTN_DOWN on PB4 -- verify pin is still LOW after the edge.
     * A capacitive glitch causes a brief dip then returns HIGH immediately.
     * A real button press holds the pin LOW. Direct IDR read -- no SPL call. */
    if ((GPIOB->IDR & (uint8_t)GPIO_PIN_4) == 0) {
        btn_down_pressed = TRUE;
        //GPIOB->ODR ^= (uint8_t)GPIO_PIN_5;
    }
}
INTERRUPT_HANDLER(EXTI_PORTC_IRQHandler, 5) {
    /* BTN_UP on PC3 -- same glitch filter. */
    if ((GPIOC->IDR & (uint8_t)GPIO_PIN_3) == 0) {
        btn_up_pressed = TRUE;
        //GPIOB->ODR ^= (uint8_t)GPIO_PIN_5;
    }
}
INTERRUPT_HANDLER(EXTI_PORTD_IRQHandler,             6) {}
INTERRUPT_HANDLER(EXTI_PORTE_IRQHandler,             7) {}
INTERRUPT_HANDLER(SPI_IRQHandler,                    10) {}
INTERRUPT_HANDLER(TIM1_UPD_OVF_TRG_BRK_IRQHandler,   11) {}
INTERRUPT_HANDLER(TIM1_CAP_COM_IRQHandler,           12) {}
INTERRUPT_HANDLER(TIM2_UPD_OVF_BRK_IRQHandler,       13) {}
INTERRUPT_HANDLER(TIM2_CAP_COM_IRQHandler,           14) {}
INTERRUPT_HANDLER(UART1_TX_IRQHandler,               17) {}
INTERRUPT_HANDLER(UART1_RX_IRQHandler,               18) {}
INTERRUPT_HANDLER(I2C_IRQHandler,                    19) {}
INTERRUPT_HANDLER(ADC1_IRQHandler,                   22) {}

/* TIM4 update/overflow -- fires every ~5ms at 2MHz */
INTERRUPT_HANDLER(TIM4_UPD_OVF_IRQHandler,           23) {
    /* Inline register write -- no SPL call, no SDCC parameter slot */
    TIM4->SR1 &= (uint8_t)~TIM4_FLAG_UPDATE;

    millis_counter++;

    /* Shift-register debounce (8-sample history).
     * Direct IDR reads -- no GPIO_ReadInputPin() SPL call (SDCC non-reentrant). */
    btn_up_hist   = (uint8_t)((btn_up_hist   << 1) | ((GPIOC->IDR & (uint8_t)GPIO_PIN_3) ? 1u : 0u));
    btn_down_hist = (uint8_t)((btn_down_hist << 1) | ((GPIOB->IDR & (uint8_t)GPIO_PIN_4) ? 1u : 0u));
}

INTERRUPT_HANDLER(EEPROM_EEC_IRQHandler,             24) {}