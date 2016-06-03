/*
 *
 * tbg_hat_board_conf.h
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
#ifndef TBG_HAT_BOARD_CONF_H
#define TBG_HAT_BOARD_CONF_H

#include "portpin.h"

#ifdef OLD_HAT // :-)
    #define LED_RED_PIN     B,4
    #define LED_GRN_PIN     B,5

    #define USART_TX_PIN    A,9
    #define USART_RX_PIN    A,10

    #define RPI_PIN_D0      C,0
    #define RPI_PIN_ADSEL   C,8
    #define RPI_PIN_WRSEL   C,9
    #define RPI_PIN_EN      C,10
    #define RPI_PIN_ACK     C,11
    #define RPI_PIN_INT     C,12
    #define RPI_PIN_SPARE   C,13
#else
    #define LED_RED_PIN     B,5
    #define LED_GRN_PIN     B,6

    #define RPI_PIN_D0      A,0
    #define RPI_PIN_ADSEL   A,8
    #define RPI_PIN_WRSEL   A,9
    #define RPI_PIN_EN      A,10
    #define RPI_PIN_ACK     A,11
    #define RPI_PIN_INT     A,12
#endif

#define CAN_TX_PIN      B,8
#define CAN_RX_PIN      B,9

#endif //TBG_HAT_BOARD_CONF_H
