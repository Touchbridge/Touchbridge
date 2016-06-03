/*
 *
 * led.c
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
#include "stm32f10x_it.h"

#include "led.h"

static led_t *leds;
static uint8_t leds_numof;

void led_init(led_t *l, uint8_t n)
{
    leds = l;
    leds_numof = n;
}

void led_pulse(uint8_t led, uint16_t time)
{
    if (led > leds_numof) {
        return;
    }

    __disable_irq();
    leds[led].portpin.port->BSRR = 1 << leds[led].portpin.pin;
    // Set counter
    leds[led].counter = time;
    __enable_irq();
}

void led_run(void)
{
    __disable_irq();
    for (int i = 0; i < leds_numof; i++) {
        volatile led_t *led = leds + i;
        if (led->counter > 0) {
            led->counter--;
            if (led->counter == 0) {
                // Turn off LED
                led->portpin.port->BRR = 1 << led->portpin.pin;
            }
        }
    }
    __enable_irq();
}

