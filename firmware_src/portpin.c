/*
 *
 * portpin.c
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

#include "portpin.h"

void gpio_setup(GPIO_TypeDef *gpio, uint8_t pin, uint8_t mode)
{
    mode &= 0xf;

    if (pin < 8) {
        gpio->CRL &= ~(0xf << (pin*4));
        gpio->CRL |= mode << (pin*4);
    } else {
        pin -= 8;
        gpio->CRH &= ~(0xf << (pin*4));
        gpio->CRH |= mode << (pin*4);
    }
}

