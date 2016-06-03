/*
 *
 * tbg_hso_board_conf.h
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
#ifndef TBG_HSO_BOARD_CONF_H
#define TBG_HSO_BOARD_CONF_H

#include "portpin.h"

#ifdef OLD_HSO // For R1 of HSO board, LEDs were on different pins
    #define LED_RED_PIN     A,11
    #define LED_GRN_PIN     A,12
#else
    #define LED_RED_PIN     B,2
    #define LED_GRN_PIN     B,1
#endif

#define OUTPUT1_PIN     A,0
#define OUTPUT2_PIN     A,1
#define OUTPUT3_PIN     A,2
#define OUTPUT4_PIN     A,3
#define OUTPUT5_PIN     A,4
#define OUTPUT6_PIN     A,5
#define OUTPUT7_PIN     A,6
#define OUTPUT8_PIN     A,7

#define CAN_TX_PIN      B,8
#define CAN_RX_PIN      B,9

#endif //TBG_HSO_BOARD_CONF_H
