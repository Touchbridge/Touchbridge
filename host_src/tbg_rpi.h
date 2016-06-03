/*
 *
 * tbg_rpi.h
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
#ifndef TBG_RPI_H
#define TBG_RPI_H

#include <stdint.h>

#include "tbg_protocol.h"

// Pi GPIO numbers for TBGRPI interface.
// NOte: these are the Broadcom GPIO numbers, not the
// connector pin numbers.
// Data bus is pins [23:16]
#define TBGRPI_PIN_D0           (16)
#define TBGRPI_PIN_ADSEL        (24)
#define TBGRPI_PIN_WRSEL        (25)
#define TBGRPI_PIN_EN           (26)
#define TBGRPI_PIN_ACK          (27)
#define TBGRPI_PIN_INT          (12)
#define TBGRPI_PIN_SPARE        (13)

#define TBGRPI_PINS_DBUS        ((uint32_t)0xff << TBGRPI_PIN_D0)

// Data read/write select line
#define TBGRPI_BUS_WRSEL_RD     (0)
#define TBGRPI_BUS_WRSEL_WR     (1)

// Address / Data select line
#define TBGRPI_BUS_ADSEL_DATA   (0)
#define TBGRPI_BUS_ADSEL_ADDR   (1)

// When reading, if ADSEL bit is high, addr/stat reg is:
// Bits [7:4] Address
// Bit 3 Reserved
// Bit 2 RX Buffer Overflow
// Bit 1 RX Data Available
// Bit 0 TX Buffer Empty
//
// When writing, if ADSEL bit is high,  addr/conf reg is:
// Bits [7:4] Address
// Bit 3 Reserved
// Bit 2 RX Buffer Overflow Reset
// Bit 1 RX Data Available Interrupt Enable
// Bit 0 TX Buffer Empty Interrupt Enable

#define TBGRPI_STAT_TX_BUF_EMPTY        (0x01)
#define TBGRPI_STAT_RX_DATA_AVAIL       (0x02)
#define TBGRPI_STAT_RX_OVERFLOW         (0x04)

#define TBGRPI_CONF_TX_BUF_EMPTY_IE     (0x01)
#define TBGRPI_CONF_RX_DATA_AVAIL_IE    (0x02)
#define TBGRPI_CONF_RX_OVERFLOW_RESET   (0x04)

#define TBGRPI_ADDR_BIT_MASK            (0xf0)
#define TBGRPI_ADDR_BIT_SHIFT           (4)

// Address values
#define TBGRPI_ADDR_CAN         (0)
#define TBGRPI_ADDR_FILT1       (1)
#define TBGRPI_ADDR_FILT2       (2)
#define TBGRPI_ADDR_CONFIG_REG  (3)
#define TBGRPI_REG_NUMOF                (4) 

typedef struct tbgrpi_s {
    struct rpi_io_s *gpio;
} tbgrpi_t;

tbgrpi_t *tbgrpi_open(void);
void tbgrpi_close(tbgrpi_t *tpi);
void tbgrpi_init_io(tbgrpi_t *tpi);
void tbgrpi_write_config(tbgrpi_t *tpi, uint8_t data);
void tbgrpi_write_data(tbgrpi_t *tpi, uint8_t *data, int size);
uint8_t tbgrpi_read_status(tbgrpi_t *tpi);
void tbgrpi_read_data(tbgrpi_t *tpi, uint8_t *data, int size);
void tbgrpi_send_msg(tbgrpi_t *tpi, tbg_msg_t *msg);
void tbgrpi_recv_msg(tbgrpi_t *tpi, tbg_msg_t *msg);

#endif // TBG_RPI_H
