/*
 *
 * tbg_hco_main.c
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

#include "tbg_hco_board_conf.h"

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

#define DELTA_BRAKE_ON          (VOLTS_TO_ADC(2.0))
#define DELTA_BRAKE_OFF         (VOLTS_TO_ADC(1.0))

#define THR_BUS_OVERVOLT        (VOLTS_TO_ADC(40.0))
#define THR_BUS_UNDERVOLT       (VOLTS_TO_ADC(8.0))

#define THR_BRAKE_MIN_VBUS      (VOLTS_TO_ADC(9.0))

#define R_TH_80C                (1256.0) // Resistance of thermistor at 80C in ohms
#define R_TH_60C                (2490.0) // Resistance of thermistor at 60C in ohms

#define R_TH_TO_ADC(R)          (ADC_COUNTS * (3300.0 / (3300.0 + (R))))

#define THR_BRAKE_OVERTEMP_TRIP (R_TH_TO_ADC(R_TH_80C))
#define THR_BRAKE_OVERTEMP_RST  (R_TH_TO_ADC(R_TH_60C))

#define UNDERVOLT_INHIBIT_TIME  (50000) // pwm cycles


static tbg_node_t node;

volatile uint16_t adc_data[ADC_CHANNELS_NUMOF];

volatile uint16_t inhibit_faults; // Disable outputs if non-zero

volatile uint16_t undervolt_inhibit = UNDERVOLT_INHIBIT_TIME; // Inhibit undervolt time counter

static tbg_msg_t rx_msg_bufs[8];
static tbg_msg_fifo_t rx_msg_fifo = { .in = 0, .out = 0, .used = 0, .size = sizeof(rx_msg_bufs)/sizeof(tbg_msg_t), .bufs = rx_msg_bufs };

#define PWM_RELOAD              (1440-1) // 25 KHz
#define LED_BLINK_DURATION      (25) // Milliseconds

enum {
    LED_GRN = 0,
    LED_RED,
};

led_t leds[] = {
    { .portpin = { PORTPIN(LED_GRN_PIN) } },
    { .portpin = { PORTPIN(LED_RED_PIN) } },
};

static void set_enables(uint8_t x, uint8_t msk);
static uint8_t get_enables(void);
static void setup_enables(void);

static volatile uint32_t sys_counter;

void TIM1_UP_IRQHandler(void)
{
    /* Reset the interrupt flag */
    TIM1->SR &= ~TIM_SR_UIF;

    // Trigger ADC conversion
    ADC1->CR2 |= ADC_CR2_SWSTART;

    uint16_t v_in = adc_data[VIN_CH];
    uint16_t v_bus = adc_data[VBUS_CH];
    uint16_t T_brake = adc_data[BRAKE_TEMP_CH];

    if (v_bus > (v_in + DELTA_BRAKE_ON) && v_bus > THR_BRAKE_MIN_VBUS ) {
        SET(BRAKE_PIN);
    }
    if (v_bus < (v_in + DELTA_BRAKE_OFF) || v_bus < THR_BRAKE_MIN_VBUS ){
        CLR(BRAKE_PIN);
    }

    // Brake over-temp detection - set flag & disable all outputs.
    if (T_brake > THR_BRAKE_OVERTEMP_TRIP) {
        node.faults |= TBG_FAULT_OVERTEMP;
        inhibit_faults |= TBG_FAULT_OVERTEMP;
        set_enables(0x00, 0xff);
    } 

    // When temperature decreases, allow outputs to be
    // re-enabled.
    if (T_brake < THR_BRAKE_OVERTEMP_RST) {
        inhibit_faults &= ~TBG_FAULT_OVERTEMP;
    } 

    // Bus over-volt results in output being disabled
    // until power-cycle.
    if (v_bus > THR_BUS_OVERVOLT) {
        node.faults |= TBG_FAULT_OVERVOLT;
        inhibit_faults |= TBG_FAULT_OVERVOLT;
        set_enables(0x00, 0xff);
    }

    // Bus over-volt results in output being disabled
    // until power-cycle.
    if (v_bus < THR_BUS_UNDERVOLT) {
        if (undervolt_inhibit > 0) 
            undervolt_inhibit--;
        else {
            node.faults |= TBG_FAULT_UNDERVOLT;
            inhibit_faults |= TBG_FAULT_UNDERVOLT;
            set_enables(0x00, 0xff);
        }
    } else {
        undervolt_inhibit = UNDERVOLT_INHIBIT_TIME;
    }

    // Allow undervolt inhibit to be reset when fault flag reset.
    if (!(node.faults & TBG_FAULT_UNDERVOLT) && (inhibit_faults & TBG_FAULT_UNDERVOLT)) {
        inhibit_faults &= ~TBG_FAULT_UNDERVOLT;
    }

    if (inhibit_faults) {
        SET(LED_RED_PIN);
    } else {
        CLR(LED_RED_PIN);
    }
}

void TIM2_IRQHandler(void)
{
    /* Reset the interrupt flag */
    TIM2->SR &= ~TIM_SR_UIF;

    sys_counter++;
    led_run();

    if (undervolt_inhibit > 0) {
        undervolt_inhibit--;
    }
}

static void timer_pwm_init(TIM_TypeDef *timer)
{
    // Set Prescaler
    timer->PSC = 2-1;

    // Set Auto-Reload Register
    timer->ARR = PWM_RELOAD; // 25KHz

    // Start with PWM duty factor set to zero.
    timer->CCR1 = 0;
    timer->CCR2 = 0;
    timer->CCR3 = 0;
    timer->CCR4 = 0;

    // Select PWM1 mode and enable pre-load of CCRx registers.
    timer->CCMR1 = (TIM_CCMR1_OC1PE | TIM_CCMR1_OC1M_2 | TIM_CCMR1_OC1M_1);
    timer->CCMR1 |= (TIM_CCMR1_OC2PE | TIM_CCMR1_OC2M_2 | TIM_CCMR1_OC2M_1);
    timer->CCMR2 = (TIM_CCMR2_OC3PE | TIM_CCMR2_OC3M_2 | TIM_CCMR2_OC3M_1);
    timer->CCMR2 |= (TIM_CCMR2_OC4PE | TIM_CCMR2_OC4M_2 | TIM_CCMR2_OC4M_1);

    // Enable all four Capture / Compare outputs.
    timer->CCER = TIM_CCER_CC1E | TIM_CCER_CC2E | TIM_CCER_CC3E | TIM_CCER_CC4E;

    // Enable the timer.
    timer->CR1 = TIM_CR1_CEN;

    // Enable the outputs for TIM1/TIM8.
    // For other timers, setting BDTR is harmless since that
    // address is marked "Reserved".
    timer->BDTR = TIM_BDTR_MOE;
}

void adc_set_seq_len(ADC_TypeDef *adc, int i)
{
    adc->SQR1 &= ~((uint32_t)0xf << 20);
    adc->SQR1 |= (uint32_t)((i-1) & 0xf) << 20;
}

void adc_set_seq_ch(ADC_TypeDef *adc, int seq_num, int chan)
{
    volatile uint32_t *seq_reg;
    int shift;

    if (seq_num < 0) {
        return;
    } else if (seq_num < 6) {
        seq_reg = &(adc->SQR3);
        shift = seq_num * 5;
    } else if (seq_num < 12) {
        seq_reg = &(adc->SQR2);
        shift = (seq_num - 6) * 5;
    } else if (seq_num < 16) {
        seq_reg = &(adc->SQR1);
        shift = (seq_num - 12) * 5;
    } else {
        return;
    }

    *seq_reg &= ~((uint32_t)0x1f << shift);
    *seq_reg |= (uint32_t)(chan & 0x1f) << shift;
}

/*
 * Set up ADC channels for sequential conversion.
 * We need to use DMA to move the data out of the ADC after
 * each channel in the sequence is converted.
 * We are using 11 channels:
 * AN0-AN7: output current sensors
 * AN8:  V+ rail
 * AN9: Rbrake Temperature
 * AN10: Vin rail
 */
void adc_dma_init()
{

    // Enable ADC clock
    RCC->APB2ENR |= RCC_APB2ENR_ADC1EN;  

    // Ensure DMA clock is enabled
    RCC->AHBENR |= RCC_AHBENR_DMA1EN;

    GPIO_SETUP(AN0_PIN, GPIO_TYPE_INPUT_ANALOGUE);
    GPIO_SETUP(AN1_PIN, GPIO_TYPE_INPUT_ANALOGUE);
    GPIO_SETUP(AN2_PIN, GPIO_TYPE_INPUT_ANALOGUE);
    GPIO_SETUP(AN3_PIN, GPIO_TYPE_INPUT_ANALOGUE);
    GPIO_SETUP(AN4_PIN, GPIO_TYPE_INPUT_ANALOGUE);
    GPIO_SETUP(AN5_PIN, GPIO_TYPE_INPUT_ANALOGUE);
    GPIO_SETUP(AN6_PIN, GPIO_TYPE_INPUT_ANALOGUE);
    GPIO_SETUP(AN7_PIN, GPIO_TYPE_INPUT_ANALOGUE);
    GPIO_SETUP(AN8_PIN, GPIO_TYPE_INPUT_ANALOGUE);
    GPIO_SETUP(AN9_PIN, GPIO_TYPE_INPUT_ANALOGUE);
    GPIO_SETUP(AN10_PIN, GPIO_TYPE_INPUT_ANALOGUE);

    ADC1->SMPR1 = 0; // Fastest sample time (~1.17us/channel)
    ADC1->SMPR2 = 0;

    ADC1->CR1 = ADC_CR1_SCAN;
    //ADC1->CR2 = ADC_CR2_DMA | ADC_CR2_CONT | ADC_CR2_EXTSEL | ADC_CR2_EXTTRIG;
    ADC1->CR2 = ADC_CR2_DMA | ADC_CR2_EXTSEL | ADC_CR2_EXTTRIG;

    // Set channels 0-10 to be converted in order
    int i;
    for (i = 0; i < ADC_CHANNELS_NUMOF; i++) {
        adc_set_seq_ch(ADC1, i, i);
    }
    adc_set_seq_len(ADC1, i);

    // DMA Setup

    // We have to use DMA1, Channel 1, as this is the only
    // one connected to ADC1.

    // Configuration Register
    DMA1_Channel1->CCR &= ~DMA_CCR1_EN;

    // Number of data units to transfer.
    DMA1_Channel1->CNDTR = ADC_CHANNELS_NUMOF;

    // Set peripheral data register address (source.)
    DMA1_Channel1->CPAR = ((uint32_t)0x4001244C);

    // Memory address (destination.)
    DMA1_Channel1->CMAR = ((uint32_t)adc_data);

    // Channel control register.
    DMA1_Channel1->CCR = 
        DMA_CCR1_MSIZE_0 |  // 16-bits
        DMA_CCR1_PSIZE_0 |  // 16-bits
        DMA_CCR1_MINC |     // Increment memory
        DMA_CCR1_CIRC;      // Circular mode (i.e. don't quite after one xfer)

    DMA1_Channel1->CCR |= DMA_CCR1_EN;        // Enable channel

    // Start ADC
    ADC1->CR2 |= ADC_CR2_ADON;

    // Calibrate ADC. This makes a massive difference to accuracy.

    // We need to wait for at least one ADC clock cycle
    // before calibration. 1us is massively overkill but
    // what the heck.
    delay_us(1);

    // Reset calibration registers
    ADC1->CR2 |= ADC_CR2_RSTCAL;
    // Wait for reset to complete
    while (ADC1->CR2 & ADC_CR2_RSTCAL);
    // Start calibration
    ADC1->CR2 |= ADC_CR2_CAL;
    // Wait for calibration to complete
    while (ADC1->CR2 & ADC_CR2_CAL);
}

static void hco_setup()
{
    // Brake resistor
    CLR(BRAKE_PIN);
    GPIO_SETUP(BRAKE_PIN, GPIO_TYPE_OUTPUT_PUSHPULL);
    CLR(BRAKE_PIN);
    
    // Setup IO pins for TIMER1 PWM
    GPIO_SETUP(PWM1_PIN, GPIO_TYPE_ALTERNATE_PUSHPULL);
    GPIO_SETUP(PWM2_PIN, GPIO_TYPE_ALTERNATE_PUSHPULL);
    GPIO_SETUP(PWM3_PIN, GPIO_TYPE_ALTERNATE_PUSHPULL);
    GPIO_SETUP(PWM4_PIN, GPIO_TYPE_ALTERNATE_PUSHPULL);

    // Setup IO pins for TIMER3 PWM
    GPIO_SETUP(PWM5_PIN, GPIO_TYPE_ALTERNATE_PUSHPULL);
    GPIO_SETUP(PWM6_PIN, GPIO_TYPE_ALTERNATE_PUSHPULL);
    GPIO_SETUP(PWM7_PIN, GPIO_TYPE_ALTERNATE_PUSHPULL);
    GPIO_SETUP(PWM8_PIN, GPIO_TYPE_ALTERNATE_PUSHPULL);

    // Set enable pins to correct mode and disable all
    setup_enables();
    set_enables(0x00, 0xff);

    // LEDs.
    GPIO_SETUP(LED_RED_PIN, GPIO_TYPE_OUTPUT_PUSHPULL);
    GPIO_SETUP(LED_GRN_PIN, GPIO_TYPE_OUTPUT_PUSHPULL);

    led_init(leds, sizeof(leds) / sizeof(led_t));

    // Start with LEDs off
    CLR(LED_RED_PIN);
    CLR(LED_GRN_PIN);

    // TIMER1 clock enable
    RCC->APB2ENR |= RCC_APB2ENR_TIM1EN;

    // TIMER3 clock enable
    RCC->APB1ENR |= RCC_APB1ENR_TIM3EN;

    // Remap TIM3 pins onto PC6-PC9
    AFIO->MAPR |= AFIO_MAPR_TIM3_REMAP_FULLREMAP;

    timer_pwm_init(TIM1);
    timer_pwm_init(TIM3);

    adc_dma_init();

    // TIM1 Interrupt setup. We use TIM1 Update INTs to
    // trigger ADC conversion and run the braking resistor
    // so they are in sync with PWM.
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = TIM1_UP_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
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

static struct enable_s {
    GPIO_TypeDef *gpio;
    uint8_t pin;
} enable_pins[] = {
    { PORTPIN(INH1_PIN) },
    { PORTPIN(INH2_PIN) },
    { PORTPIN(INH3_PIN) },
    { PORTPIN(INH4_PIN) },
    { PORTPIN(INH5_PIN) },
    { PORTPIN(INH6_PIN) },
    { PORTPIN(INH7_PIN) },
    { PORTPIN(INH8_PIN) },
};

static void setup_enables(void)
{
    for (int i = 0; i < 8; i++) {
        GPIO_TypeDef *gpio = enable_pins[i].gpio;
        uint8_t pin = enable_pins[i].pin;
        gpio_setup(gpio, pin, GPIO_TYPE_OUTPUT_PUSHPULL);
    }
}

static void set_enables(uint8_t x, uint8_t msk)
{
    for (int i = 0; i < 8; i++) {
        GPIO_TypeDef *gpio = enable_pins[i].gpio;
        uint8_t pin = enable_pins[i].pin;
        if (msk & (1 << i)) {
            gpio->ODR = (gpio->ODR & ~(1 << pin)) | (((x >> i) & 1) << pin);
        }
    }
}

static uint8_t get_enables(void)
{
    uint8_t data = 0;

    for (int i = 0; i < 8; i++) {
        GPIO_TypeDef *gpio = enable_pins[i].gpio;
        uint8_t pin = enable_pins[i].pin;
        data |= ((gpio->ODR >> pin) & 1) << i;
    }
    return data;
}

static int hco_enable(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp, tbg_port_t *port)
{
    if (inhibit_faults) {
        return tbg_err_resp(req, resp, TBG_ERR_FAULT);
    }

    if (req->len != 4 && req->len != 8) {
        return tbg_err_resp(req, resp, TBG_ERR_LENGTH);
    }
    uint8_t value = req->data[0];
    uint8_t mask;
    if (req->len > 4) {
        mask  = req->data[4];
    } else {
        mask = 0xff;
    }
    set_enables(value, mask);
    resp->len = 1;
    resp->data[0] = get_enables();
    return 1;
}

static int analogue_in(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp, tbg_port_t *port)
{
    if (req->len != 2) {
        return tbg_err_resp(req, resp, TBG_ERR_LENGTH);
    }

    uint8_t start_ch = req->data[0];
    uint8_t num_chs = req->data[1];

    if (num_chs > 4 || (start_ch + num_chs) > ADC_CHANNELS_NUMOF ) {
        return tbg_err_resp(req, resp, TBG_ERR_RANGE);
    }

    memcpy(resp->data, (void *)(adc_data + start_ch), num_chs * sizeof(uint16_t));

    resp->len = num_chs * sizeof(uint16_t);

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

static tbg_port_t hco1_ports[] = {
    {
        .port_number = 8,
        .port_class = TBG_PORT_CLASS_DIGITAL_OUT,
        .fn = hco_enable,
        .fn_data = NULL,
        .descr = "PWM Output Enable;{uint32_t data,uint32_t mask}",
        .conf_data = NULL,
        .confs = pwm_confs,
        .num_confs = sizeof(pwm_confs)/sizeof(tbg_conf_t),
    },
    {
        .port_number = 9,
        .port_class = TBG_PORT_CLASS_ANALOGUE_OUT,
        .fn = pwm_out_0_3,
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
    {
        .port_number = 12,
        .port_class = TBG_PORT_CLASS_ANALOGUE_IN,
        .fn = analogue_in,
        .fn_data = NULL,
        .descr = "Analogue Input;req:{uint8_t channel_start,uint8_t num_channels};resp:{uint16_t[]}",
        .conf_data = NULL,
        .confs = NULL,
        .num_confs = 0,
    },
};

static tbg_node_t node = {
    .common_ports = tbg_common_ports,
    .ports = hco1_ports,
    .confs = tbg_global_confs,
    .port_common_confs = tbg_port_common_confs,
    .product_id = "{\"id\":\"TBG-HCO\",\"rev\":3,\"opt\":[{\"num\":1,\"descr\":\"braking\"}],\"descr\":\"Touchbridge 8 Channel High Current Output Card\",\"mfg\":\"Airborne Engineering Limited\"}",
    .num_ports  = sizeof(hco1_ports)/sizeof(tbg_port_t),
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

    timer2_setup();

    hco_setup();

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
