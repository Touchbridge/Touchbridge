/*
 *
 * tbg_hco_board_conf.h
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

#ifndef TBG_HCO_BOARD_CONF_H
#define TBG_HCO_BOARD_CONF_H

#include "portpin.h"

#define LED_GRN_PIN     B,4
#define LED_RED_PIN     B,5

#define USART_TX_PIN    A,9
#define USART_RX_PIN    A,10

#define CAN_TX_PIN      B,8
#define CAN_RX_PIN      B,9

#define SYNC_OUT_PIN    C,10
#define SYNC_IN_PIN     C,11
#define OPTO_IN_PIN     C,13

#define STEPPER1_STEP_PIN       A,0 // TIM2_CH1
#define STEPPER1_DIR_PIN        C,3
#define STEPPER1_EN_PIN         C,2
#define STEPPER1_FAULT_PIN      C,5
#define STEPPER1_BRK_OFF_PIN    A,4

#define STEPPER2_STEP_PIN       A,8 // TIM1_CH1
#define STEPPER2_DIR_PIN        C,8
#define STEPPER2_EN_PIN         C,7
#define STEPPER2_FAULT_PIN      B,0
#define STEPPER2_BRK_OFF_PIN    B,12

#endif //TBG_HCO_BOARD_CONF_H
