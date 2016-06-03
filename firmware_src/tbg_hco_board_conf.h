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

#define LED_RED_PIN     B,4
#define LED_GRN_PIN     B,5

#define USART_TX_PIN    C,10
#define USART_RX_PIN    C,11

#define CAN_TX_PIN      B,8
#define CAN_RX_PIN      B,9

#define INH1_PIN        C,12
#define INH2_PIN        D,2
#define INH3_PIN        B,6
#define INH4_PIN        B,7
#define INH5_PIN        B,15
#define INH6_PIN        B,14
#define INH7_PIN        B,13
#define INH8_PIN        B,12

#define PWM1_PIN        A,8
#define PWM2_PIN        A,9
#define PWM3_PIN        A,10
#define PWM4_PIN        A,11
#define PWM5_PIN        C,6
#define PWM6_PIN        C,7
#define PWM7_PIN        C,8
#define PWM8_PIN        C,9

#define AN0_PIN         A,0
#define AN1_PIN         A,1
#define AN2_PIN         A,2
#define AN3_PIN         A,3
#define AN4_PIN         A,4
#define AN5_PIN         A,5
#define AN6_PIN         A,6
#define AN7_PIN         A,7
#define AN8_PIN         B,0
#define AN9_PIN         B,1
#define AN10_PIN        C,0

#define VBUS_CH         (8)
#define BRAKE_TEMP_CH   (9)
#define VIN_CH          (10)

#define ADC_CHANNELS_NUMOF  (11)

#define BRAKE_PIN       A,12

#endif //TBG_HCO_BOARD_CONF_H
