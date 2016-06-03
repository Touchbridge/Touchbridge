/*
 *
 * tbg_stm32.h
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
#ifndef TBG_STM32_H
#define TBG_STM32_H

#include "tbg_protocol.h"

/*
 * STM32 CAN FIFO ID format is:
 * ID left-justified
 * IDE (EID) bit
 * RTR bit
 * TXRQ bit
 */
#define BITVAL(x, bit_mask)             (((x) & (bit_mask)) ? 1 : 0)
#define TBG_STM32_TBGID2STM(t)          ( (BITVAL(t, TBG_MSG_ID_BIT_EID) << 2) | (BITVAL(t, TBG_MSG_ID_BIT_RTR) << 1) | (((t) & TBG_MSG_ID_BITS_ID) << (((t) & TBG_MSG_ID_BIT_EID) ?  3 : 21)) )

#define TBG_CAN_SETUP_RX_IE         (0x01)
#define TBG_CAN_SETUP_TX_IE         (0x02)

typedef union tbg_stm32_unique_id_u {
    uint32_t word32[3];
    uint8_t bytes[12];
} tbg_stm32_unique_id_t;

extern tbg_stm32_unique_id_t *tbg_stm32_unique_id;

int tbg_can_tx(tbg_msg_t *msg);
void tbg_can_rx(tbg_msg_t *msg);
void tbg_can_setup(int ie);


#endif // TBG_STM32_H

