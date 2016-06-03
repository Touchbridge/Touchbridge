/*
 *
 * portpin.h
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
#ifndef PORTPIN_H
#define PORTPIN_H

/*
 * portpin.h
 *
 * James Macfarlane 2012-2016
 */

/*
 * Macros to simplify port access and prevent errors due to setting
 * right pin on wrong port.
 *
 * NB: These macros are two levels deep in order that the
 * compiler does not generate an error saying there are
 * insufficient variables being passed. This is because the
 * pre-processor will not do macro expansion and stringification
 * in same macro def. Hence the need for SOMEMACRO to call _SOMEMACRO.
 */

#include "stm32f10x_gpio.h"

typedef uint16_t port_word_t;

typedef struct portpin_s {
    GPIO_TypeDef *port;
    uint8_t pin;
} portpin_t;

#define _BV(x)  ((port_word_t)1 << (x))

#define _SET(type,port,bit)         type ## port->BSRR = _BV(bit)
#define _CLEAR(type,port,bit)       type ## port->BRR = _BV(bit)
#define _TOGGLE(type,port,bit)      type ## port->ODR ^= _BV(bit)
#define _GET(type,port,bit)         ((type ## port->IDR >> bit) &  1)
#define _WRT(type,port,bit,value)   type ## port->ODR = ( type ## port->ODR & ( ~ _BV(bit)) ) | ( ( 1 & (port_word_t)value ) << bit )

#define SET(pin)            _SET(GPIO,pin)
#define CLR(pin)            _CLEAR(GPIO,pin)   
#define WRT(pin,value)      _WRT(GPIO,pin,value)   
#define TOGGLE(pin)         _TOGGLE(GPIO,pin)   
#define IS_SET(pin)         _GET(GPIO,pin)
#define IS_CLR(pin)         (_GET(GPIO,pin) ^ 1)

/*
 * These macros are for extracting the port or bit information
 * from the combined port,bin definitions.
 *
 * Terminology:
 * PIN  - combined port & pin info, e.g. A,3
 * PORT - port, e.g. GPIOA
 * BIT  - pin number, e.g. 3
 * MASK - bit mask for pin, e.g. 0x08
 */
#define _PORT(port,bit)     GPIO ## port
#define PORT(pin)           _PORT(pin)
#define _BIT(port,bit)      bit
#define BIT(pin)            _BIT(pin)
#define MASK(pin)           _BV(_BIT(pin))

// Results in, for example, GPOIC,2 for calling functions
// or initialising data structures.
#define _PORTPIN(port,bit)     GPIO ## port, bit
#define PORTPIN(pin)           _PORTPIN(pin)

// Commonly used STM32 Input modes
#define GPIO_TYPE_INPUT_ANALOGUE        (0b0000)
#define GPIO_TYPE_INPUT_FLOATING        (0b0100)
#define GPIO_TYPE_INPUT_PULLED          (0b1000)
#define GPIO_TYPE_OUTPUT_PUSHPULL       (0b0011)
#define GPIO_TYPE_ALTERNATE_PUSHPULL    (0b1011)

// Pin set-up macros to allow port pin defs (e.g. A,2) to be used.
#define _GPIO_SETUP(port, pin, config) gpio_setup(GPIO ## port, pin, config)
#define GPIO_SETUP(portpin, config) _GPIO_SETUP(portpin, config)

void gpio_setup(GPIO_TypeDef *gpio, uint8_t pin, uint8_t mode);

#endif /* PORTPIN_H */
