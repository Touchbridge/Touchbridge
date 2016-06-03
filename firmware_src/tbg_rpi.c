/*
 *
 * tbg_rpi.c
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

#include <stdlib.h>
#include <string.h>
#include "stm32f10x_gpio.h"

#include "tbg_msg.h"
#include "tbg_stm32.h"
#include "led.h"
#include "tbg_rpi.h"
#include "portpin.h"
#include "tbg_hat_board_conf.h"


#define TBG_MSG_RX_FIFO_SIZE            (8)

static tbg_msg_t tbg_rx_msg_bufs[TBG_MSG_RX_FIFO_SIZE];

uint32_t tbg_rpi_cfg1;

tbg_msg_fifo_t tbg_rx_msg_fifo = { .in = 0, .out = 0, .used = 0, .size = TBG_MSG_RX_FIFO_SIZE, .bufs = tbg_rx_msg_bufs };

typedef void reg_fn_t(uint8_t addr, uint8_t *buf, int size);

typedef struct tbg_rpi_reg_s {
    reg_fn_t *wr_fn;
    reg_fn_t *rd_fn;
    uint8_t size;
} tbg_rpi_reg_t;

static void msg_put(uint8_t reg, uint8_t *buf, int size);
static void msg_get(uint8_t reg, uint8_t *buf, int size);
static void filter_set(uint8_t reg, uint8_t *buf, int size);
static void cfg_reg_write(uint8_t reg, uint8_t *buf, int size);
static void cfg_reg_read(uint8_t reg, uint8_t *buf, int size);

static const tbg_rpi_reg_t tbg_rpi_regs[TBGRPI_REG_NUMOF] = {
    [TBGRPI_REG_ADDR_CAN] =  { .size = TBG_MSG_SIZE,     .wr_fn = msg_put, .rd_fn = msg_get },
    [TBGRPI_REG_ADDR_FILT1] = { .size = 8, .wr_fn = filter_set, .rd_fn = NULL },
    [TBGRPI_REG_ADDR_FILT2] = { .size = 8, .wr_fn = filter_set, .rd_fn = NULL },
    [TBGRPI_REG_ADDR_CFG1] = { .size = 8, .wr_fn = cfg_reg_write, .rd_fn = cfg_reg_read },
};

typedef struct tbg_rpi_s {
    uint8_t addr;
    uint8_t conf;
    volatile uint8_t stat;
    uint8_t rd_ptr;
    uint8_t wr_ptr;
    uint8_t *rd_buf;
    uint8_t *wr_buf;
} tbg_rpi_t;

#define TBGRPI_REG_BUF_SIZE             (16)

uint8_t tbg_rpi_wr_buf[TBGRPI_REG_BUF_SIZE];
uint8_t tbg_rpi_rd_buf[TBGRPI_REG_BUF_SIZE];

static tbg_rpi_t tbgrpi = {
    .addr = 0,
    .conf = 0,
    .stat = 0,
    .rd_ptr = 0,
    .wr_ptr = 0,
    .rd_buf = tbg_rpi_wr_buf,
    .wr_buf = tbg_rpi_rd_buf,
};

static void msg_put(uint8_t reg, uint8_t *buf, int size)
{
    tbg_can_tx((tbg_msg_t *)buf);
}

static void msg_get(uint8_t reg, uint8_t *buf, int size)
{
    tbg_msg_fifo_out(&tbg_rx_msg_fifo, (tbg_msg_t *)buf);
}

static void filter_set(uint8_t reg, uint8_t *buf, int size)
{
    uint8_t filtnum = reg - TBGRPI_REG_ADDR_FILT1;
    uint32_t *x = (void *)buf; // Point to an array of 2 TBG CAN IDs

    CAN1->FMR |= CAN_FMR_FINIT;         // Enter Filter Init mode.
    CAN1->sFilterRegister[filtnum].FR1 = TBG_STM32_TBGID2STM(x[0]); // Id
    CAN1->sFilterRegister[filtnum].FR2 = TBG_STM32_TBGID2STM(x[1]); // Mask
    CAN1->FM1R &= ~(1L << filtnum);     // Id/Mask mode
    CAN1->FS1R |= 1L << filtnum;        // 32-bit Id/mask
    CAN1->FFA1R &= ~(1L << filtnum);    // Filter assigned to FIFO 0
    CAN1->FA1R |= 1L << filtnum;        // Activate this filter.

    CAN1->FMR &= ~CAN_FMR_FINIT;        // Leave Filter Init mode.
}

#define CAN_INIT_TIMEOUT (0x10000)

static void cfg_reg_write(uint8_t reg, uint8_t *buf, int size)
{
    uint32_t *x = (void *)buf;
    uint32_t data = x[0], mask = x[1];

    tbg_rpi_cfg1 &= mask;
    tbg_rpi_cfg1 |= data;
    if (mask & TBGRPI_CFG1_LOOPBACK) {
        // To access BTR we need to enter Initialisation mode.
        CAN1->MCR |= CAN_MCR_INRQ;
        for (uint32_t i = 0; (!(CAN1->MSR & CAN_MSR_INAK)) && (i < CAN_INIT_TIMEOUT); i++);
        if (data && TBGRPI_CFG1_LOOPBACK)
            CAN1->BTR |= CAN_BTR_LBKM;
        else
            CAN1->BTR &= ~CAN_BTR_LBKM;
        CAN1->MCR &= ~CAN_MCR_INRQ;
        for (uint32_t i = 0; (CAN1->MSR & CAN_MSR_INAK) && (i < CAN_INIT_TIMEOUT); i++);
    }
    if (mask & TBGRPI_CFG1_SILENT) {
        // To access BTR we need to enter Initialisation mode.
        CAN1->MCR |= CAN_MCR_INRQ;
        for (uint32_t i = 0; (!(CAN1->MSR & CAN_MSR_INAK)) && (i < CAN_INIT_TIMEOUT); i++);
        if (data && TBGRPI_CFG1_SILENT)
            CAN1->BTR |= CAN_BTR_SILM;
        else
            CAN1->BTR &= ~CAN_BTR_SILM;
        CAN1->MCR &= ~CAN_MCR_INRQ;
        for (uint32_t i = 0; (CAN1->MSR & CAN_MSR_INAK) && (i < CAN_INIT_TIMEOUT); i++);
    }
}

static void cfg_reg_read(uint8_t reg, uint8_t *buf, int size)
{
    memcpy(buf, &tbg_rpi_cfg1, sizeof(tbg_rpi_cfg1));
}

void tbg_rpi_wr_data(uint8_t data)
{
    tbg_rpi_t *tp = &tbgrpi;
    // Bounds-check the register address.
    if (tp->addr > TBGRPI_REG_NUMOF) {
        return;
    }
    const tbg_rpi_reg_t *reg = tbg_rpi_regs + tp->addr;
    tp->wr_buf[tp->wr_ptr++] = data;
    if (tp->wr_ptr == reg->size) {
        tp->wr_ptr = 0;
        if (reg->wr_fn) {
            reg->wr_fn(tp->addr, tp->wr_buf, reg->size);
        }
    }
}

uint8_t tbg_rpi_rd_data(void)
{
    tbg_rpi_t *tp = &tbgrpi;
    // Bounds-check the register address.
    if (tp->addr > TBGRPI_REG_NUMOF) {
        return 0x55;
    }
    const tbg_rpi_reg_t *reg = tbg_rpi_regs + tp->addr;
    if (tp->rd_ptr == 0) {
        // Fetch some new data
        if (reg->rd_fn) {
            reg->rd_fn(tp->addr, tp->rd_buf, reg->size);
        }
    }
    uint8_t data = tp->rd_buf[tp->rd_ptr++];
    if (tp->rd_ptr == reg->size) {
        // If we read too much, just wrap back to beginning.
        tp->rd_ptr = 0;
    }
    return data;
}

void tbg_rpi_wr_addr_config(uint8_t data)
{
    tbg_rpi_t *tp = &tbgrpi;
    tp->rd_ptr = 0;
    tp->wr_ptr = 0;
    tp->addr = (data & TBGRPI_ADDR_BIT_MASK) >> TBGRPI_ADDR_BIT_SHIFT;
    tp->conf = (data & TBGRPI_CONF_BIT_MASK);
    if (data & TBGRPI_CONF_RX_OVERFLOW_RESET) {
        tp->stat &= ~TBGRPI_STAT_RX_OVERFLOW;
    }
}

uint8_t tbg_rpi_rd_addr_status(void)
{
    tbg_rpi_t *tp = &tbgrpi;
    tp->rd_ptr = 0;
    tp->wr_ptr = 0;
    __disable_irq();
    // Capture status value.
    uint8_t stat = tp->stat;
    // De-assert RPi's INT pin.
    SET(RPI_PIN_INT);
    // If IE set, reset the int flag.
    // Return the current state of the system.
    stat = (TBG_MSG_FIFO_EMPTY(&tbg_rx_msg_fifo)) ?
        stat & ~TBGRPI_STAT_RX_DATA_AVAIL :
        stat | TBGRPI_STAT_RX_DATA_AVAIL;
    stat = ((CAN1->TSR & CAN_TSR_TME0) || (CAN1->TSR & CAN_TSR_TME1) || (CAN1->TSR & CAN_TSR_TME2)) ?
        stat | TBGRPI_STAT_TX_BUF_EMPTY :
        stat & ~TBGRPI_STAT_TX_BUF_EMPTY;
    __enable_irq();
    return (tp->addr << TBGRPI_ADDR_BIT_SHIFT) | (stat & TBGRPI_STAT_BIT_MASK);
}

void tbg_rpi_txe_int(void)
{
    tbg_rpi_t *tp = &tbgrpi;
    tp->stat |= TBGRPI_STAT_TX_BUF_EMPTY;
    if (tp->conf & TBGRPI_CONF_TX_BUF_EMPTY_IE) {
        // Assert RPi's INT pin
        CLR(RPI_PIN_INT);
    }
}

void tbg_rpi_rxda_int(tbg_msg_t *msg)
{
    tbg_rpi_t *tp = &tbgrpi;

    if (!tbg_msg_fifo_in(&tbg_rx_msg_fifo, msg)) {
        tp->stat |= TBGRPI_STAT_RX_OVERFLOW;
    }

    tp->stat |= TBGRPI_STAT_RX_DATA_AVAIL;

    if (tp->conf & TBGRPI_CONF_RX_DATA_AVAIL_IE) {
        // Assert RPi's INT pin
        CLR(RPI_PIN_INT);
    }
}
