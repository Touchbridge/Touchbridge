/*
 *
 * tbg_input_board_conf.h
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
#ifndef TBG_INPUT_BOARD_CONF_H
#define TBG_INPUT_BOARD_CONF_H

#include "portpin.h"

#define LED_GRN_PIN     B,1
#define LED_RED_PIN     B,2

#define INPUT1_PIN      A,0
#define INPUT2_PIN      A,1
#define INPUT3_PIN      A,2
#define INPUT4_PIN      A,3
#define INPUT5_PIN      A,4
#define INPUT6_PIN      A,5
#define INPUT7_PIN      A,6
#define INPUT8_PIN      A,7

#define USART_TX_PIN    B,6
#define USART_RX_PIN    B,7

#define CAN_TX_PIN      B,8
#define CAN_RX_PIN      B,9

#endif //TBG_INPUT_BOARD_CONF_H
