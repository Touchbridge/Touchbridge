/*
 *
 * uart.c
 *
 * This file is part of Touchbridge
 *
 * Copyright 2015 James L Macfarlane
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "stm32f10x.h"

#include "uart.h"
#include "cdev.h"

static USART_TypeDef * const uart_bases[] = {

#ifdef USART1
    USART1,
#elif defined UART1
    UART1,
#else
   NULL,
#endif

#ifdef USART2
    USART2,
#elif defined UART2
    UART2,
#else
   NULL,
#endif

#ifdef USART3
    USART3,
#elif defined UART3
    UART3,
#else
   NULL,
#endif

#ifdef USART4
    USART4,
#elif defined UART4
    UART4,
#else
   NULL,
#endif

#ifdef USART5
    USART5,
#elif defined UART5
    UART5,
#else
   NULL,
#endif

};

static cdev_t *uart_cdevs[UART_NUMOF];

void uart_tx_enable(cdev_t *cdev)
{
    ((USART_TypeDef *)(cdev->device))->CR1 |= USART_CR1_TXEIE;
}

void uart_init(int fd, int uart_num, uint16_t rate)
{
    if (uart_num < 0 || uart_num >= UART_NUMOF || !uart_bases[uart_num])
        return;

    USART_TypeDef *usart = uart_bases[uart_num];

    /* initialise ring buffer */
    cdev_init(fd, uart_tx_enable, usart);

    uart_cdevs[uart_num] = cdev_p(fd);
}

// Common interrupt handler fn for all USARTs.
static inline void usart_irq(uint8_t uart_num)
{
    USART_TypeDef *usart = uart_bases[uart_num];

    volatile uint32_t sr = usart->SR;
 
    if (sr & USART_SR_TXE) {
        int16_t ret = cdev_tx(uart_cdevs[uart_num]);
        if (ret >= 0) {
            usart->DR = ret;
        } else { // disable TX
            usart->CR1 &= ~USART_CR1_TXEIE;
        }
    } else if (sr & USART_SR_RXNE) {
        cdev_rx(uart_cdevs[uart_num], usart->DR);
    }
}

#ifdef USART1
void USART1_IRQHandler(void)
{
    usart_irq(UART_NUM1);
}
#endif

#ifdef UART1
void UART1_IRQHandler(void)
{
    usart_irq(UART_NUM1);
}
#endif

#ifdef USART2
void USART2_IRQHandler(void)
{
    usart_irq(UART_NUM2);
}
#endif

#ifdef UART2
void UART2_IRQHandler(void)
{
    usart_irq(UART_NUM2);
}
#endif

#ifdef USART3
void USART3_IRQHandler(void)
{
    usart_irq(UART_NUM3);
}
#endif

#ifdef UART3
void UART3_IRQHandler(void)
{
    usart_irq(UART_NUM3);
}
#endif

#ifdef USART4
void USART4_IRQHandler(void)
{
    usart_irq(UART_NUM4);
}
#endif

#ifdef UART4
void UART4_IRQHandler(void)
{
    usart_irq(UART_NUM4);
}
#endif

#ifdef USART5
void USART5_IRQHandler(void)
{
    usart_irq(UART_NUM5);
}
#endif

#ifdef UART5
void UART5_IRQHandler(void)
{
    usart_irq(UART_NUM5);
}
#endif

