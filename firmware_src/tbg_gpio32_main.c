/*
 *
 * tbg_gpio32_main.c
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

#include <string.h>

#include "stm32f10x.h"
#include "stm32f10x_conf.h"
#include "stm32f10x_it.h"
#include "stm32f10x_can.h"
#include "stm32f10x_gpio.h"

#include "tbg_gpio32_board_conf.h"

#include "cdev.h"
#include "uart.h"
#include "led.h"
#include "delay.h"
#include "tbg_msg.h"
#include "tbg_node.h"
#include "tbg_stm32.h"


#include <stdio.h>
#include <math.h>

#define VREF                    (3.3)
#define ADC_COUNTS              (4096.0)
#define ADC_TO_VOLTS(a)         ((VREF*(float)(a)/ADC_COUNTS)/(3.3/(3.3+22+22)))
#define VOLTS_TO_ADC(v)         (uint16_t)((ADC_COUNTS*(v)/VREF)*(3.3/(3.3+22+22)))

static tbg_node_t node;

static tbg_msg_t rx_msg_bufs[8];
static tbg_msg_fifo_t rx_msg_fifo = { .in = 0, .out = 0, .used = 0, .size = sizeof(rx_msg_bufs)/sizeof(tbg_msg_t), .bufs = rx_msg_bufs };

#define PWM_RELOAD              (1440-1) // 25 KHz
#define LED_BLINK_DURATION      (25) // Milliseconds
#define PWM_PRESCALE            (72-1)

enum {
    LED_GRN = 0,
    LED_RED,
};

led_t leds[] = {
    { .portpin = { PORTPIN(LED_GRN_PIN) } },
    { .portpin = { PORTPIN(LED_RED_PIN) } },
};

typedef struct {
    int32_t posn;
    int32_t rate;
    int32_t max_rate;
    uint8_t dir;
    uint8_t state;
} stepper_t;

static volatile stepper_t stepper1;
static volatile stepper_t stepper2;

#define STEPPER_MIN_RATE       16  // In Hz
#define STEPPER_MAX_RATE    16000  // In Hz
#define STEPPER_ACCEL        8000  // In Hz/s

static volatile uint32_t sys_counter;

void TIM1_UP_IRQHandler(void)
{
    /* Reset the interrupt flag */
    TIM1->SR &= ~TIM_SR_UIF;

    // Trigger ADC conversion
    //ADC1->CR2 |= ADC_CR2_SWSTART;
    //stepper1_posn += (stepper1_dir) ? 1 : -1;
}

void TIM2_IRQHandler(void)
{
    /* Reset the interrupt flag */
    TIM2->SR &= ~TIM_SR_UIF;

    //stepper2_posn += (stepper2_dir) ? 1 : -1;
}

void TIM3_IRQHandler(void)
{
    /* Reset the interrupt flag */
    TIM3->SR &= ~TIM_SR_UIF;

    sys_counter++;
    led_run();
}

static void timer_pwm_freq(TIM_TypeDef *timer, uint16_t prescale, uint16_t reload)
{
    // Set Prescaler
    timer->PSC = prescale;

    // Set Auto-Reload Register
    timer->ARR = reload;

    // Start with PWM duty factor set to 50%.
    timer->CCR1 = reload/2;
    timer->CCR2 = reload/2;
    timer->CCR3 = reload/2;
    timer->CCR4 = reload/2;
}

static void timer_pwm_init(TIM_TypeDef *timer, uint16_t prescale, uint16_t reload)
{
    timer_pwm_freq(timer, prescale, reload);

    // Select PWM1 mode and enable pre-load of CCRx registers.
    timer->CCMR1 = (TIM_CCMR1_OC1PE | TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1M_1);
    timer->CCMR1 |= (TIM_CCMR1_OC2PE | TIM_CCMR1_OC2M_2 | TIM_CCMR1_OC2M_1);
    timer->CCMR2 = (TIM_CCMR2_OC3PE | TIM_CCMR2_OC3M_2 | TIM_CCMR2_OC3M_1);
    timer->CCMR2 |= (TIM_CCMR2_OC4PE | TIM_CCMR2_OC4M_2 | TIM_CCMR2_OC4M_1);

    // Enable all four Capture / Compare outputs.
    //timer->CCER = TIM_CCER_CC1E | TIM_CCER_CC2E | TIM_CCER_CC3E | TIM_CCER_CC4E;
    timer->CCER = TIM_CCER_CC1E;

    // Enable the timer.
    timer->CR1 = TIM_CR1_CEN;

    // Enable the outputs for TIM1/TIM8.
    // For other timers, setting BDTR is harmless since that
    // address is marked "Reserved".
    timer->BDTR = TIM_BDTR_MOE;
}

static void gpio32_setup()
{
    // Setup IO pins for stepper motors
    GPIO_SETUP(STEPPER1_STEP_PIN,       GPIO_TYPE_ALTERNATE_PUSHPULL);
    GPIO_SETUP(STEPPER1_DIR_PIN,        GPIO_TYPE_ALTERNATE_PUSHPULL);
    GPIO_SETUP(STEPPER1_EN_PIN,         GPIO_TYPE_ALTERNATE_PUSHPULL);
    GPIO_SETUP(STEPPER1_BRK_OFF_PIN,    GPIO_TYPE_ALTERNATE_PUSHPULL);
    GPIO_SETUP(STEPPER1_FAULT_PIN,      GPIO_TYPE_INPUT_PULLED);
    SET(STEPPER1_FAULT_PIN); // Pull-up

    GPIO_SETUP(STEPPER2_STEP_PIN,       GPIO_TYPE_ALTERNATE_PUSHPULL);
    GPIO_SETUP(STEPPER2_DIR_PIN,        GPIO_TYPE_ALTERNATE_PUSHPULL);
    GPIO_SETUP(STEPPER2_EN_PIN,         GPIO_TYPE_ALTERNATE_PUSHPULL);
    GPIO_SETUP(STEPPER2_BRK_OFF_PIN,    GPIO_TYPE_ALTERNATE_PUSHPULL);
    GPIO_SETUP(STEPPER2_FAULT_PIN,      GPIO_TYPE_INPUT_PULLED);
    SET(STEPPER2_FAULT_PIN); // Pull-up

    // LEDs.
    GPIO_SETUP(LED_RED_PIN, GPIO_TYPE_OUTPUT_PUSHPULL);
    GPIO_SETUP(LED_GRN_PIN, GPIO_TYPE_OUTPUT_PUSHPULL);

    led_init(leds, sizeof(leds) / sizeof(led_t));

    // Start with LEDs off
    CLR(LED_RED_PIN);
    CLR(LED_GRN_PIN);

    // TIMER1 clock enable
    RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;
 
    // TIMER2 clock enable
    RCC->APB1ENR |= RCC_APB1ENR_TIM2EN;

    // TIMER3 clock enable
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;

    // Remap TIM3 pins onto PC6-PC9
    AFIO->MAPR |= AFIO_MAPR_TIM3_REMAP_FULLREMAP;

    // Set up timers for generating stepper pulses
    timer_pwm_init(TIM1, PWM_PRESCALE, 0xffff);
    timer_pwm_init(TIM2, PWM_PRESCALE, 0xffff);

    // TIM1 Interrupt setup.
    /*
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = TIM1_UP_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
    */
}

/*
 * Set up timer for software timing tasks.
 */
static void timer_setup(void)
{
    // Set up NVIC
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    TIM_TypeDef *timer = TIM3;

    // Set up timer
    timer->PSC = 72-1;   //  1 MHz after prescaler
    timer->ARR = 1000-1; //  1 KHz interrupt rate
    timer->CCMR1 = 0;
    timer->CCMR2 = 0;
    timer->CCER = 0;
    timer->CR1 = TIM_CR1_CEN;    // Enable the timer
    timer->DIER = TIM_DIER_UIE;  // Enable update interrupts
}

static void usart3_setup(void)
{
    uart_init(1, UART_NUM3, 0); // Do this before enabling USART ints

    // USART3 clock enable */
    RCC->APB1ENR |= RCC_APB1ENR_USART3EN;

    // Baud rate is 36MHz divided by this number.
    // (USART2-USART5 fed from  Pckl1)
    USART3->BRR = 312; // 115200 Baud

    // Config register
    USART3->CR1 |= USART_CR1_UE | USART_CR1_TE | USART_CR1_RXNEIE;

    // IO Setup

    // Need to set a remap to get USART 3 on PC10/PC11
    AFIO->MAPR &= ~AFIO_MAPR_USART3_REMAP_PARTIALREMAP;
    AFIO->MAPR |= AFIO_MAPR_USART3_REMAP_PARTIALREMAP;

    GPIO_SETUP(USART_TX_PIN, 0b1001); // USART3 TX: AF push-pull output, 10MHz
    GPIO_SETUP(USART_RX_PIN, 0b1000); // USART3 TX: Input with pull-up/down
    SET(USART_RX_PIN); // Set to pull-up for USART3 RX

    // USART interrupt set-up
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

static int pwm_out_ch(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp, tbg_port_t *port)
{
    uint8_t channel = req->data[0];
    uint16_t value = req->data[1] | req->data[2] << 8;
    switch (channel) {
        case 0:
            TIM1->CCR1 = value;
            break;
        case 1:
            TIM1->CCR2 = value;
            break;
        case 2:
            TIM1->CCR3 = value;
            break;
        case 3:
            TIM1->CCR4 = value;
            break;
        case 4:
            TIM3->CCR1 = value;
            break;
        case 5:
            TIM3->CCR2 = value;
            break;
        case 6:
            TIM3->CCR3 = value;
            break;
        case 7:
            TIM3->CCR4 = value;
            break;
    }
    resp->len = 0;
    return 1;
}

static int pwm_freq(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp, tbg_port_t *port)
{
    uint16_t *value = (uint16_t *)req->data;
    timer_pwm_freq(TIM3, PWM_PRESCALE, value[0]);

    resp->len = 0;
    return 1;
}

static int pwm_out_0_3(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp, tbg_port_t *port)
{
    uint16_t *value = (uint16_t *)req->data;

    TIM1->CCR1 = value[0];
    TIM1->CCR2 = value[1];
    TIM1->CCR3 = value[2];
    TIM1->CCR4 = value[3];

    resp->len = 0;
    return 1;
}

static int pwm_out_4_7(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp, tbg_port_t *port)
{
    uint16_t *value = (uint16_t *)req->data;

    TIM3->CCR1 = value[0];
    TIM3->CCR2 = value[1];
    TIM3->CCR3 = value[2];
    TIM3->CCR4 = value[3];

    resp->len = 0;
    return 1;
}

static int pwm_conf_max_pwm_rd(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp, tbg_port_t *port, tbg_conf_t *conf)
{
    resp->data[0] = (PWM_RELOAD >> 0 ) & 0xff;
    resp->data[1] = (PWM_RELOAD >> 8 ) & 0xff;
    resp->len = sizeof(uint16_t);
    return 1;
}

static tbg_conf_t pwm_confs[] = {
    {
        .wr_fn = NULL,
        .rd_fn = pwm_conf_max_pwm_rd,
        .descr = "Max PWM value",
    },
};

static tbg_port_t gpio321_ports[] = {
    {
        .port_number = 8,
        .port_class = TBG_PORT_CLASS_DIGITAL_OUT,
        //.fn = gpio32_enable,
        .fn = NULL, // FIXME: write generic PWM out fn
        .fn_data = NULL,
        .descr = "PWM Output Enable;{uint32_t data,uint32_t mask}",
        .conf_data = NULL,
        .confs = pwm_confs,
        .num_confs = sizeof(pwm_confs)/sizeof(tbg_conf_t),
    },
    {
        .port_number = 9,
        .port_class = TBG_PORT_CLASS_ANALOGUE_OUT,
        .fn = pwm_freq,
        .fn_data = NULL,
        .descr = "PWM Output, Channels 1-4;{uint16_t[4]}",
        .conf_data = NULL,
        .confs = pwm_confs,
        .num_confs = sizeof(pwm_confs)/sizeof(tbg_conf_t),
    },
    {
        .port_number = 10,
        .port_class = TBG_PORT_CLASS_ANALOGUE_OUT,
        .fn = pwm_out_4_7,
        .fn_data = NULL,
        .descr = "PWM Output, Channels 5-8;{uint16_t[4]}",
        .conf_data = NULL,
        .confs = pwm_confs,
        .num_confs = sizeof(pwm_confs)/sizeof(tbg_conf_t),
    },
    {
        .port_number = 11,
        .port_class = TBG_PORT_CLASS_ANALOGUE_OUT_CH,
        .fn = pwm_out_ch,
        .fn_data = NULL,
        .descr = "PWM Output, Specified Channel;{uint8_t channel_number,uint16_t value}",
        .conf_data = NULL,
        .confs = pwm_confs,
        .num_confs = sizeof(pwm_confs)/sizeof(tbg_conf_t),
    },
};

static tbg_node_t node = {
    .common_ports = tbg_common_ports,
    .ports = gpio321_ports,
    .confs = tbg_global_confs,
    .port_common_confs = tbg_port_common_confs,
    .product_id = "{\"id\":\"TBG-GPIO32\",\"rev\":3,\"opt\":[{\"num\":1,\"descr\":\"braking\"}],\"descr\":\"Touchbridge GPIO Card\",\"mfg\":\"Airborne Engineering Limited\"}",
    .num_ports  = sizeof(gpio321_ports)/sizeof(tbg_port_t),
    .my_addr = TBG_ADDR_UNASSIGNED,
    .shortlist_flag = 0,
    .faults = 0,
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

    usart3_setup();

    // We need this in order that PB4 is usable as GPIO.
    AFIO->MAPR |= AFIO_MAPR_SWJ_CFG_NOJNTRST;

    timer_setup();

    gpio32_setup();

    tbg_can_setup(TBG_CAN_SETUP_RX_IE);

    __enable_irq();

    // Lamp test
    SET(LED_GRN_PIN);
    SET(LED_RED_PIN);
    delay_ms(500);
    CLR(LED_GRN_PIN);
    CLR(LED_RED_PIN);

    printf("Hello World!\n");

    TIM1->DIER = TIM_DIER_UIE; /* Enable TIM1 interrupts */

    while (1) {
        tbg_msg_t msg;

        if (tbg_msg_fifo_out(&rx_msg_fifo, &msg)) {

            if (tbg_check_addr_or_broadcast(&node, &msg) && tbg_msg_check_not_resp(&msg)) {
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

    printf("%s: %ld: assert failed\n", file, line);
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
