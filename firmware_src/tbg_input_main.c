/*
 *
 * tbg_input_main.c
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
#include "stm32f10x_conf.h"
#include "stm32f10x_it.h"
#include "stm32f10x_can.h"
#include "stm32f10x_gpio.h"

#include "tbg_input_board_conf.h"

#include "led.h"
#include "delay.h"
#include "tbg_msg.h"
#include "tbg_node.h"
#include "tbg_stm32.h"


#include <stdio.h>
#include <string.h>

static tbg_msg_t rx_msg_bufs[8];
static tbg_msg_fifo_t rx_msg_fifo = { .in = 0, .out = 0, .used = 0, .size = sizeof(rx_msg_bufs)/sizeof(tbg_msg_t), .bufs = rx_msg_bufs };


#define NUM_INPUTS              (8)
#define DEFAULT_DEBOUNCE_TIME   (20) // Milliseconds
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

static volatile uint32_t debounce_events;
static volatile uint32_t debounce_inputs;

static struct config_s {
    uint32_t rising_edge_mask;
    uint32_t falling_edge_mask;
    uint32_t debounce_enable_mask;
    uint8_t debounce_time;
} din_conf = 
{
    0x00000000,
    0x00000000,
    0xffffffff,
    DEFAULT_DEBOUNCE_TIME
};

// Dynamic state of debounce subsystem
typedef struct {
    uint32_t state;
    uint8_t *timers;
    uint8_t num_contacts;
} debounce_t;

static uint32_t debounce(debounce_t *db, uint32_t input);

uint8_t debounce_timers[NUM_INPUTS];

static debounce_t debouncer = {
    .state = 0,
    .timers = debounce_timers,
    .num_contacts = NUM_INPUTS,
};

// Decrement debounce timers.
static void debounce_timers_run(debounce_t *db)
{
    for (int i = 0; i < db->num_contacts; i++) {
        if (db->timers[i] > 0) {
            db->timers[i]--;
        }
    }
}

// Run debounce on inputs. Returns event bit mask. Any bit set in return
// value indicates a valid (i.e. properly de-bounced) state-change of the
// corresponding bit in the input.
static uint32_t debounce(debounce_t *db, uint32_t input)
{
    uint32_t events = (input ^ db->state) & ((input & din_conf.rising_edge_mask) | (~input & din_conf.falling_edge_mask));
    db->state = input;

    uint32_t enable = 0;
    for (int i = 0; i < db->num_contacts; i++) {
        uint32_t bit = 1 << i;
        if (db->timers[i] == 0) {
            enable |= bit; // Allow event to be propagated
            // Start debounce timer if enabled and event occurred.
            if ((din_conf.debounce_enable_mask & bit) && (events & bit)) {
                db->timers[i] = din_conf.debounce_time;
            }
        }
    }
    return events & enable;
}

volatile uint8_t prescale;

void TIM2_IRQHandler(void)
{
    /* Reset the interrupt flag */
    TIM2->SR &= ~TIM_SR_UIF;

    // Scan inputs & run debouncer at 10KHz.
    uint32_t inputs = GPIOA->IDR & 0xff;
    uint32_t events = debounce(&debouncer, inputs);
    if (events) {
        debounce_events = events;
        debounce_inputs = inputs;
    }

    if (prescale == 0) {
        //1 KHz Tasks
        debounce_timers_run(&debouncer);
        sys_counter++;
        led_run();
        prescale = 10;
    }
    prescale--;
}


static void board_setup()
{
    // Setup IO pins for opto inputs
    GPIO_SETUP(INPUT1_PIN, GPIO_TYPE_INPUT_FLOATING);
    GPIO_SETUP(INPUT2_PIN, GPIO_TYPE_INPUT_FLOATING);
    GPIO_SETUP(INPUT3_PIN, GPIO_TYPE_INPUT_FLOATING);
    GPIO_SETUP(INPUT4_PIN, GPIO_TYPE_INPUT_FLOATING);
    GPIO_SETUP(INPUT5_PIN, GPIO_TYPE_INPUT_FLOATING);
    GPIO_SETUP(INPUT6_PIN, GPIO_TYPE_INPUT_FLOATING);
    GPIO_SETUP(INPUT7_PIN, GPIO_TYPE_INPUT_FLOATING);
    GPIO_SETUP(INPUT8_PIN, GPIO_TYPE_INPUT_FLOATING);

    // LEDs
    GPIO_SETUP(LED_GRN_PIN, GPIO_TYPE_OUTPUT_PUSHPULL);
    GPIO_SETUP(LED_RED_PIN, GPIO_TYPE_OUTPUT_PUSHPULL);

    led_init(leds, sizeof(leds) / sizeof(led_t));

    // Start with LEDs off
    CLR(LED_GRN_PIN);
    CLR(LED_RED_PIN);
}

static void timer2_setup(int reload)
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
    TIM2->ARR = reload-1; //  1 KHz interrupt rate
    TIM2->CCMR1 = 0;
    TIM2->CCMR2 = 0;
    TIM2->CCER = 0;
    TIM2->CR1 = TIM_CR1_CEN;    // Enable the timer
    TIM2->DIER = TIM_DIER_UIE;  // Enable update interrupts
}

static int opto_in(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp, tbg_port_t *port)
{
    resp->len = 1;
    resp->data[0] = GPIOA->IDR & 0xff;
    return 1;
}

static int din_conf_rising_edge_mask_wr(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp, tbg_port_t *port, tbg_conf_t *conf)
{
    if (req->len < TBG_PORTCONF_REQ_LEN_MIN + sizeof(uint32_t)) {
        return tbg_err_resp(req, resp, TBG_ERR_LENGTH);
    }
    uint32_t *data = (void *)(req->data + TBG_PORTCONF_REQ_LEN_MIN);
    din_conf.rising_edge_mask = data[0];
    return 1;
}

static int din_conf_rising_edge_mask_rd(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp, tbg_port_t *port, tbg_conf_t *conf)
{
    uint32_t *data = (void *)resp->data;
    data[0] = din_conf.rising_edge_mask;
    resp->len = sizeof(uint32_t);
    return 1;
}

static int din_conf_falling_edge_mask_wr(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp, tbg_port_t *port, tbg_conf_t *conf)
{
    if (req->len < TBG_PORTCONF_REQ_LEN_MIN + sizeof(uint32_t)) {
        return tbg_err_resp(req, resp, TBG_ERR_LENGTH);
    }
    uint32_t *data = (void *)(req->data + TBG_PORTCONF_REQ_LEN_MIN);
    din_conf.falling_edge_mask = data[0];
    return 1;
}

static int din_conf_falling_edge_mask_rd(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp, tbg_port_t *port, tbg_conf_t *conf)
{
    uint32_t *data = (void *)resp->data;
    data[0] = din_conf.falling_edge_mask;
    resp->len = sizeof(uint32_t);
    return 1;
}

static int din_conf_debounce_enable_mask_wr(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp, tbg_port_t *port, tbg_conf_t *conf)
{
    if (req->len < TBG_PORTCONF_REQ_LEN_MIN + sizeof(uint32_t)) {
        return tbg_err_resp(req, resp, TBG_ERR_LENGTH);
    }
    uint32_t *data = (void *)(req->data + TBG_PORTCONF_REQ_LEN_MIN);
    din_conf.debounce_enable_mask = data[0];
    return 1;
}

static int din_conf_debounce_enable_mask_rd(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp, tbg_port_t *port, tbg_conf_t *conf)
{
    uint32_t *data = (void *)resp->data;
    data[0] = din_conf.debounce_enable_mask;
    resp->len = sizeof(uint32_t);
    return 1;
}

static int din_conf_debounce_time_wr(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp, tbg_port_t *port, tbg_conf_t *conf)
{
    if (req->len < TBG_PORTCONF_REQ_LEN_MIN + sizeof(uint8_t)) {
        return tbg_err_resp(req, resp, TBG_ERR_LENGTH);
    }
    din_conf.debounce_time = req->data[2];
    return 1;
}

static int din_conf_debounce_time_rd(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp, tbg_port_t *port, tbg_conf_t *conf)
{
    resp->data[0] = din_conf.debounce_time;
    resp->len = sizeof(uint8_t);
    return 1;
}

static tbg_conf_t din_confs[] = {
    {
        .wr_fn = din_conf_rising_edge_mask_wr,
        .rd_fn = din_conf_rising_edge_mask_rd,
        .descr = "Rising Edge Event Mask",
    },
    {
        .wr_fn = din_conf_falling_edge_mask_wr,
        .rd_fn = din_conf_falling_edge_mask_rd,
        .descr = "Falling Edge Event Mask",
    },
    {
        .wr_fn = din_conf_debounce_enable_mask_wr,
        .rd_fn = din_conf_debounce_enable_mask_rd,
        .descr = "Debounce Enable Mask",
    },
    {
        .wr_fn = din_conf_debounce_time_wr,
        .rd_fn = din_conf_debounce_time_rd,
        .descr = "Debounce Time",
    },
};

static tbg_port_t input1_ports[] = {
    {
        .port_number = 8,
        .port_class = TBG_PORT_CLASS_DIGITAL_IN,
        .fn = opto_in,
        .fn_data = NULL,
        .descr = "Digital Input;{uint32_t data,uin32_t mask}",
        .conf_data = NULL,
        .confs = din_confs,
        .num_confs = sizeof(din_confs)/sizeof(tbg_conf_t),
    },
};

static tbg_node_t node = {
    .common_ports = tbg_common_ports,
    .ports = input1_ports,
    .confs = tbg_global_confs,
    .port_common_confs = tbg_port_common_confs,
    .product_id = "{\"id\":\"TBG-INPUT\",\"rev\":3,\"opt\":[],\"descr\":\"Touchbridge 8 Channel Opto-Isolated Input Card\",\"mfg\":\"Airborne Engineering Limited\"}",
    .num_ports  = sizeof(input1_ports)/sizeof(tbg_port_t),
    .my_addr = TBG_ADDR_UNASSIGNED,
    .shortlist_flag = 0,
};

static uint32_t get_events(uint32_t *in)
{
    uint32_t events;
    uint32_t inputs;
    __disable_irq();
    events = debounce_events;
    inputs = debounce_inputs;
    debounce_events = 0;
    __enable_irq();
    if (events) {
        *in = inputs;
    }

    return events;
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

    timer2_setup(100);

    board_setup();

    //uint8_t inputs = GPIOA->IDR & 0xff;

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
        uint32_t evt;
        uint32_t inp;

        if (tbg_msg_fifo_out(&rx_msg_fifo, &msg)) {

            if (tbg_check_addr_or_broadcast(&node, &msg) && tbg_msg_check_not_resp(&msg)) {
                led_pulse(LED_GRN, LED_BLINK_DURATION);
                tbg_msg_t resp;
                if (tbg_port_mux(&node, &msg, &resp)) {
                    tbg_msg_tx(&resp);
                }
            }
        }
        if ((evt = get_events(&inp))) {
            led_pulse(LED_RED, 10);
            // Send a broadcast message with the event data
            tbg_msg_t ind;
            tbg_msg_init(&ind);
            TBG_MSG_SET_TYPE(&ind, TBG_MSG_TYPE_IND);
            TBG_MSG_SET_SRC_ADDR(&ind, node.my_addr);
            TBG_MSG_SET_SRC_PORT(&ind, 8); // Mark it as from the din port
            TBG_MSG_SET_DST_ADDR(&ind, TBG_ADDR_BROADCAST);
            TBG_MSG_SET_DST_PORT(&ind, 0);
            ind.len = 8;
            memcpy(ind.data + 0, &evt, sizeof(uint32_t));
            memcpy(ind.data + 4, &inp, sizeof(uint32_t));
            tbg_msg_tx(&ind);
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
