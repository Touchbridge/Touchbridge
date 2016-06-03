/*
 *
 * tbg_hat_main.c
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
/*
 * Touchbridge Raspberry Pi HAT firmware
 */ 

#include "stm32f10x.h"
#include "stm32f10x_conf.h"
#include "stm32f10x_it.h"
#include "stm32f10x_can.h"
#include "stm32f10x_gpio.h"

#include "tbg_hat_board_conf.h"

#include "led.h"
#include "delay.h"
#include "tbg_node.h"
#include "tbg_rpi.h"
#include "portpin.h"
#include "tbg_stm32.h"
#include "led.h"


#include <stdio.h>
#include <math.h>

#define LED_BLINK_DURATION  (40)

enum {
    LED_GRN = 0,
    LED_RED,
};

led_t leds[] = {
    { .portpin = { PORTPIN(LED_GRN_PIN) } },
    { .portpin = { PORTPIN(LED_RED_PIN) } },
};

static volatile uint32_t sys_counter;

void TIM2_IRQHandler(void)
{
    /* Reset the interrupt flag */
    TIM2->SR &= ~TIM_SR_UIF;
    sys_counter++;

    led_run();
}

static void rpi_bus_setup(void)
{
    NVIC_InitTypeDef NVIC_InitStructure;

    // Make data bus an input (default state.)
    PORT(RPI_PIN_D0)->CRL = 0x44444444;

    GPIO_SETUP(RPI_PIN_ADSEL, GPIO_TYPE_INPUT_PULLED); // Input with pull-up/down
    GPIO_SETUP(RPI_PIN_WRSEL, GPIO_TYPE_INPUT_PULLED); // Input with pull-up/down
    CLR(RPI_PIN_ADSEL); // Pull-down for ADSEL.
    CLR(RPI_PIN_WRSEL); // Pull-down for WRSEL.

    // /EN input
    GPIO_SETUP(RPI_PIN_EN, GPIO_TYPE_INPUT_PULLED); // Input with pull-up/down
    SET(RPI_PIN_EN); // Pull-up for EN.

    // /ACK output
    GPIO_SETUP(RPI_PIN_ACK, GPIO_TYPE_OUTPUT_PUSHPULL);

    // /INT output
    GPIO_SETUP(RPI_PIN_INT, GPIO_TYPE_OUTPUT_PUSHPULL);
 
    // Enable external ints for /EN pin on PC10
    EXTI->IMR = MASK(RPI_PIN_EN);
    EXTI->RTSR = MASK(RPI_PIN_EN); // Rising edge trigger
    EXTI->FTSR = MASK(RPI_PIN_EN); // Falling edge trigger
#ifdef OLD_HAT
    AFIO->EXTICR[2] = (2 << 8); // Use Port C for ext int 10
#else
    AFIO->EXTICR[2] = (0 << 8); // Use Port A for ext int 10
#endif

    NVIC_InitStructure.NVIC_IRQChannel = EXTI15_10_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

#ifdef OLD_HAT
static void usart_setup(void)
{
    /*
     * UASART Setup
     *
     */
 
    uart_init(1, UART_NUM1, 0); // Do this before enabling USART ints

    // USART1 clock enable */
    RCC->APB2ENR |= RCC_APB2ENR_USART1EN;

    // Baud rate is 72MHz divided by this number.
    // (USART1 fed from  Pckl2
    USART1->BRR = 625; // 115200 Baud

    // Config register
    USART1->CR1 |= USART_CR1_UE | USART_CR1_TE | USART_CR1_RXNEIE;

    // TX & RX pin mappings (PA9, PA10 are defailt no alt fn remap needed.)
    gpio_setup(GPIOA,  9, 0b1001); // USART1 TX on  PA9: AF push-pull output, 10MHz
    gpio_setup(GPIOA, 10, 0b1000); // USART1 TX on PA10: Input with pull-up/down
    GPIOA->BSRR = (1 << 10); // Set to pull-up for USART1 RX on PA10

    // USART interrupt set-up
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}
#endif // OLD_HAT

static void board_setup(void)
{
    // LEDs
    GPIO_SETUP(LED_GRN_PIN, GPIO_TYPE_OUTPUT_PUSHPULL);
    GPIO_SETUP(LED_RED_PIN, GPIO_TYPE_OUTPUT_PUSHPULL);

    led_init(leds, sizeof(leds) / sizeof(led_t));

    // Start with LEDs off
    CLR(LED_GRN_PIN);
    CLR(LED_RED_PIN);
}

static void timer2_setup(void)
{
    // Enable clock
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;

    // Set up NVIC
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    // Set up timer
    TIM2->PSC = 72-1;   //  1 MHz after prescaler
    TIM2->ARR = 1000-1; //  1 KHz interrupt rate
    TIM2->CCMR1 = 0;
    TIM2->CCMR2 = 0;
    TIM2->CCER = 0;
    TIM2->CR1 = TIM_CR1_CEN;    // Enable the timer
    TIM2->DIER = TIM_DIER_UIE;  // Enable update interrupts
}


int main(void)
{
    /* GPIOC & GPIOA & GPIOB Periph clock enable */
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;
    RCC->APB2ENR |= RCC_APB2ENR_IOPBEN;
    RCC->APB2ENR |= RCC_APB2ENR_IOPCEN;
    RCC->APB2ENR |= RCC_APB2ENR_IOPDEN;
    RCC->APB2ENR |= RCC_APB2ENR_AFIOEN;

    GPIOA->CRL = 0;
    GPIOA->CRH = 0;
    GPIOB->CRL = 0;
    GPIOB->CRH = 0;
    GPIOC->CRL = 0;
    GPIOC->CRH = 0;
    GPIOD->CRL = 0;
    GPIOD->CRH = 0;

    // We need this in order that PB4 is usable as GPIO.
    AFIO->MAPR |= AFIO_MAPR_SWJ_CFG_NOJNTRST;

    timer2_setup();

    board_setup();

#ifdef OLD_HAT
    usart_setup();    
#endif // OLD_HAT

    rpi_bus_setup();

    tbg_can_setup(TBG_CAN_SETUP_RX_IE | TBG_CAN_SETUP_TX_IE);

    __enable_irq();

    // Lamp test
    SET(LED_GRN_PIN);
    SET(LED_RED_PIN);
    delay_ms(1000);
    CLR(LED_GRN_PIN);
    CLR(LED_RED_PIN);

    while (1) {
        /* Un-comment for interrupt test.
        if (sys_counter & 0x40) {
            SET(RPI_PIN_INT);
        } else {
            CLR(RPI_PIN_INT);
        }
        */
    }

}

#ifdef  USE_FULL_ASSERT

/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t* file, uint32_t line)
{ 
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */

    //printf("%s: %ld: assert failed\n", file, line);
    /* Infinite loop */
    while (1);
}

#endif


void EXTI15_10_IRQHandler(void)
{

    /*
     * We must reset the int src at start of ISR not end
     * otherwise you get a double interrupt.
     *  See: http://false.ekta.is/2012/06/interrupt-service-routines-double-firing-on-stm32/
     */
    EXTI->PR = MASK(RPI_PIN_EN);
    uint8_t state = IS_SET(RPI_PIN_EN);
    if (state == 0) {
        led_pulse(LED_RED, LED_BLINK_DURATION);
        uint8_t data;
        uint8_t adsel = IS_SET(RPI_PIN_ADSEL);
        uint8_t wrsel = IS_SET(RPI_PIN_WRSEL);
        if (wrsel) {
            data = PORT(RPI_PIN_D0)->IDR & 0xff;   // Get the data.
            CLR(RPI_PIN_ACK); // Assert ACK.
            if (adsel == TBGRPI_BUS_ADSEL_DATA) {
                tbg_rpi_wr_data(data);
            } else {
                tbg_rpi_wr_addr_config(data);
            }
        } else {
            // Pi is reading. Make data bus an output.
            PORT(RPI_PIN_D0)->CRL = 0x33333333;
            if (adsel == TBGRPI_BUS_ADSEL_DATA) {
                data = tbg_rpi_rd_data();
            } else {
                data = tbg_rpi_rd_addr_status();
            }
            // Put the data on the bus.
            PORT(RPI_PIN_D0)->BRR = 0xff;
            PORT(RPI_PIN_D0)->BSRR = data;
            // Assert ACK.
            CLR(RPI_PIN_ACK);
        }
    } else {
        // Make data bus an input (default state.)
        PORT(RPI_PIN_D0)->CRL = 0x44444444;
        SET(RPI_PIN_ACK); // De-assert ACK
   }
}

void USB_LP_CAN1_RX0_IRQHandler(void)
{
    tbg_msg_t msg;

    // Get the message from the CAN hardware
    tbg_can_rx(&msg);

    // Put message into Rx FIFO, update status reg
    // and raise RX interrupt on Pi (if enabled)
    tbg_rpi_rxda_int(&msg);

    led_pulse(LED_GRN, LED_BLINK_DURATION);
}

void USB_HP_CAN1_TX_IRQHandler(void)
{
    uint32_t tsr = CAN1->TSR;
    if (tsr & CAN_TSR_RQCP2) CAN1->TSR |= CAN_TSR_RQCP2;
    if (tsr & CAN_TSR_RQCP1) CAN1->TSR |= CAN_TSR_RQCP1;
    if (tsr & CAN_TSR_RQCP0) CAN1->TSR |= CAN_TSR_RQCP0;

    // Update status reg & raise interrupt (if enabled)
    tbg_rpi_txe_int();
}
