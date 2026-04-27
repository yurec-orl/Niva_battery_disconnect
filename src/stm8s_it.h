#ifndef STM8S_IT_H
#define STM8S_IT_H

#include "stm8s.h"

/* Shared state updated by TIM4 ISR */
extern volatile uint32_t millis_counter;
extern volatile uint8_t  btn_up_hist;
extern volatile uint8_t  btn_down_hist;

#endif /* STM8S_IT_H */
