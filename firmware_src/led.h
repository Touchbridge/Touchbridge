/*
 *
 * led.h
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
#ifndef LED_H
#define LED_H

#include "stm32f10x_gpio.h"
#include "portpin.h"

typedef struct led_s {
    portpin_t portpin;
    uint16_t counter;
} led_t;

void led_init(led_t *l, uint8_t n);
void led_pulse(uint8_t led, uint16_t time);
void led_run(void);

#endif // LED_H

