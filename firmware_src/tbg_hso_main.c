/*
 *
 * tbg_hso_main.c
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
 *
 */ 

#include "stm32f10x.h"
#include "stm32f10x_conf.h"
#include "stm32f10x_it.h"
#include "stm32f10x_can.h"
#include "stm32f10x_gpio.h"

#include "tbg_hso_board_conf.h"

#include "led.h"
#include "delay.h"
#include "tbg_msg.h"
#include "tbg_node.h"
#include "tbg_stm32.h"


#include <stdio.h>
#include <math.h>

static tbg_msg_t rx_msg_bufs[8];
static tbg_msg_fifo_t rx_msg_fifo = { .in = 0, .out = 0, .used = 0, .size = sizeof(rx_msg_bufs)/sizeof(tbg_msg_t), .bufs = rx_msg_bufs };

#define LED_BLINK_DURATION      (25) // Milliseconds

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

static void board_setup()
{
    // Setup IO pins for opto inputs
    GPIO_SETUP(OUTPUT1_PIN, GPIO_TYPE_OUTPUT_PUSHPULL);
    GPIO_SETUP(OUTPUT2_PIN, GPIO_TYPE_OUTPUT_PUSHPULL);
    GPIO_SETUP(OUTPUT3_PIN, GPIO_TYPE_OUTPUT_PUSHPULL);
    GPIO_SETUP(OUTPUT4_PIN, GPIO_TYPE_OUTPUT_PUSHPULL);
    GPIO_SETUP(OUTPUT5_PIN, GPIO_TYPE_OUTPUT_PUSHPULL);
    GPIO_SETUP(OUTPUT6_PIN, GPIO_TYPE_OUTPUT_PUSHPULL);
    GPIO_SETUP(OUTPUT7_PIN, GPIO_TYPE_OUTPUT_PUSHPULL);
    GPIO_SETUP(OUTPUT8_PIN, GPIO_TYPE_OUTPUT_PUSHPULL);

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

// Because of PCB layout, we need to re-map the output byte so bit0 corresponds
// to output 1, etc. To do this, we just swap the top & bottom half-bytes.
uint8_t io_remap(uint8_t x)
{
#ifdef OLD_HSO
    x = (x & 0xf0) >> 4 | (x & 0x0f) << 4;
#endif
    return x;
}

static int hso_out(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp, tbg_port_t *port)
{
    if (req->len != 4 && req->len != 8) {
        return tbg_err_resp(req, resp, TBG_ERR_LENGTH);
    }
    uint8_t value = io_remap(req->data[0]);
    uint8_t mask;
    if (req->len > 4) {
        mask  = io_remap(req->data[4]);
    } else {
        mask = 0xff;
    }
    GPIOA->BRR = (~value) & mask;
    GPIOA->BSRR = value & mask;
    resp->len = 1;
    resp->data[0] = GPIOA->ODR & 0xff;
    return 1;
}

static tbg_port_t hso1_ports[] = {
    {
        .port_number = 8,
        .port_class = TBG_PORT_CLASS_DIGITAL_OUT,
        .fn = hso_out,
        .fn_data = NULL,
        .descr = "Digital Output;{uint32_t data,uint32_t mask}",
        .conf_data = NULL,
        .confs = NULL,
        .num_confs = 0,
    },
};

static tbg_node_t node = {
    .common_ports = tbg_common_ports,
    .ports = hso1_ports,
    .confs = tbg_global_confs,
    .port_common_confs = tbg_port_common_confs,
    .product_id = "{\"id\":\"TBG-HSO\",\"rev\":3,\"opt\":[],\"descr\":\"Touchbridge 8 Channel High-Side Output Card\",\"mfg\":\"Airborne Engineering Limited\"}",
    .num_ports  = sizeof(hso1_ports)/sizeof(tbg_port_t),
    .my_addr = TBG_ADDR_UNASSIGNED,
    .shortlist_flag = 0,
};

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

    __enable_irq();

    // Lamp test
    SET(LED_GRN_PIN);
    SET(LED_RED_PIN);
    delay_ms(500);
    CLR(LED_GRN_PIN);
    CLR(LED_RED_PIN);

    tbg_can_setup(TBG_CAN_SETUP_RX_IE);

    while (1) {
        tbg_msg_t msg;

        if (tbg_msg_fifo_out(&rx_msg_fifo, &msg) && tbg_msg_check_not_resp(&msg)) {

            if (tbg_check_addr_or_broadcast(&node, &msg)) {
                led_pulse(LED_GRN, LED_BLINK_DURATION);
                tbg_msg_t resp;
                if (tbg_port_mux(&node, &msg, &resp)) {
                    tbg_msg_tx(&resp);
                }
            }
        }
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


void USB_LP_CAN1_RX0_IRQHandler(void)
{
    tbg_msg_t msg;

    // Get the message from the CAN hardware
    tbg_can_rx(&msg);

    // Put message in our software FIFO.
    tbg_msg_fifo_in(&rx_msg_fifo, &msg);
}
