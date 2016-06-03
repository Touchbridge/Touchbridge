/*
 *
 * tbg_stm32.c
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

#include <stdio.h>

#include "stm32f10x.h"
#include "tbg_stm32.h"

// STM32 96-bit Unique ID
tbg_stm32_unique_id_t *tbg_stm32_unique_id = (void *)0x1FFFF7E8;

// Transmit a Touchbridge message on the CAN bus.
// Returns 1 if sucessful, 0 if no TX mailboxes
// were free.
int tbg_can_tx(tbg_msg_t *msg)
{
    uint8_t tme = 7 & (CAN1->TSR >> 26); // TME[2:0]
    uint8_t mb_num;

    if (tme & 1) {
        mb_num = 0;
    } else if (tme & 2) {
        mb_num = 1;
    } else if (tme & 4) {
        mb_num = 2;
    } else {
        // No Tx mailboxes free.
        return 0;
    }

    if (msg->len > 8)
        msg->len = 8;

    CAN_TxMailBox_TypeDef *mailbox = CAN1->sTxMailBox + mb_num;
    uint32_t *data = (void *)msg->data;
    mailbox->TIR = TBG_STM32_TBGID2STM(msg->id);
    mailbox->TDTR = msg->len;
    mailbox->TDLR = data[0];
    mailbox->TDHR = data[1];
    mailbox->TIR |= CAN_TI0R_TXRQ; // Trigger transmission.
    return 1;
}

void tbg_can_setup(int ie)
{
    GPIO_InitTypeDef GPIO_InitStructure;
    CAN_InitTypeDef CAN_InitStructure;
     
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_CAN1, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO, ENABLE);
     
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
     
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
     
    GPIO_PinRemapConfig(GPIO_Remap1_CAN1 , ENABLE);
     
    // Set up timing for 500Kb/s CAN bus
    CAN_InitStructure.CAN_Prescaler = 4;
    CAN_InitStructure.CAN_SJW = CAN_SJW_2tq;
    CAN_InitStructure.CAN_BS1 = CAN_BS1_11tq;
    CAN_InitStructure.CAN_BS2 = CAN_BS2_6tq;
    CAN_InitStructure.CAN_Mode = CAN_Mode_Normal;
    //CAN_InitStructure.CAN_Mode = CAN_Mode_LoopBack;
    CAN_InitStructure.CAN_TTCM = DISABLE;

    // Automatic recovery from "Bus-off" state, which is
    // entered if too many errors occur.
    CAN_InitStructure.CAN_ABOM = ENABLE;

    CAN_InitStructure.CAN_AWUM = DISABLE;

    // Allow automatic re-transmission
    CAN_InitStructure.CAN_NART = DISABLE;

    CAN_InitStructure.CAN_RFLM = DISABLE;
    CAN_InitStructure.CAN_TXFP = ENABLE; // Tx MB priority by Tx order not ID.
    CAN_Init(CAN1, &CAN_InitStructure);


    if (ie & TBG_CAN_SETUP_RX_IE) {
        NVIC_InitTypeDef NVIC_InitStructure;

        NVIC_InitStructure.NVIC_IRQChannel = USB_LP_CAN1_RX0_IRQn;
        NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
        NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
        NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
        NVIC_Init(&NVIC_InitStructure);

        CAN_ITConfig(CAN1, CAN_IT_FMP0, ENABLE);
    }

    if (ie & TBG_CAN_SETUP_TX_IE) {
        NVIC_InitTypeDef NVIC_InitStructure;

        NVIC_InitStructure.NVIC_IRQChannel = USB_HP_CAN1_TX_IRQn;
        NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
        NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
        NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
        NVIC_Init(&NVIC_InitStructure);

        CAN_ITConfig(CAN1, CAN_IT_TME, ENABLE);
    }

    // Default "catch-all" filter setup.
    CAN_FilterInitTypeDef CAN_FilterInitStructure;
    CAN_FilterInitStructure.CAN_FilterNumber = 0;
    CAN_FilterInitStructure.CAN_FilterMode = CAN_FilterMode_IdMask;
    CAN_FilterInitStructure.CAN_FilterScale = CAN_FilterScale_32bit;
    CAN_FilterInitStructure.CAN_FilterIdHigh = 0x0000;
    CAN_FilterInitStructure.CAN_FilterIdLow = 0x0000;
    CAN_FilterInitStructure.CAN_FilterMaskIdHigh = 0x0000;
    CAN_FilterInitStructure.CAN_FilterMaskIdLow = 0x0000;
    CAN_FilterInitStructure.CAN_FilterFIFOAssignment = 0;
    CAN_FilterInitStructure.CAN_FilterActivation = ENABLE;

    CAN_FilterInit(&CAN_FilterInitStructure);
}

/*
 * Receive a CAN message. This should be called from
 * the CAN Rx interrupt handler.
 */
void tbg_can_rx(tbg_msg_t *msg)
{
    CAN_FIFOMailBox_TypeDef *fifo = CAN1->sFIFOMailBox + 0;
    if (fifo->RIR & CAN_RI0R_IDE) {
        msg->id = (uint32_t)0x1FFFFFFF & (fifo->RIR >> 3);
    } else {
        msg->id = (uint32_t)0x000007FF & (fifo->RIR >> 21);
    }
    TBG_MSG_SET_EID(msg, (fifo->RIR & CAN_RI0R_IDE) ? 1 : 0);
    TBG_MSG_SET_RTR(msg, (fifo->RIR & CAN_RI0R_RTR) ? 1 : 0);
    msg->len = fifo->RDTR & CAN_RDT0R_DLC;
    uint32_t *data = (void *)msg->data;
    data[0] = fifo->RDLR;
    data[1] = fifo->RDHR;
    // Release the CAN hardware FIFO
    CAN1->RF0R |= CAN_RF0R_RFOM0;
}
