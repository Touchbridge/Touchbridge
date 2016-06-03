/*
 *
 * tbgrpi_protocol.h
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
#ifndef TBGRPI_PROTOCOL_H
#define TBGRPI_PROTOCOL_H

// Data read/write select line
#define TBGRPI_BUS_WRSEL_RD             (0)
#define TBGRPI_BUS_WRSEL_WR             (1)

// Address / Data select line
#define TBGRPI_BUS_ADSEL_DATA           (0)
#define TBGRPI_BUS_ADSEL_ADDR           (1)

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

#define TBGRPI_STAT_BIT_MASK            (0x0f)
#define TBGRPI_STAT_TX_BUF_EMPTY        (0x01)
#define TBGRPI_STAT_RX_DATA_AVAIL       (0x02)
#define TBGRPI_STAT_RX_OVERFLOW         (0x04)

#define TBGRPI_CONF_BIT_MASK            (0x0f)
#define TBGRPI_CONF_TX_BUF_EMPTY_IE     (0x01)
#define TBGRPI_CONF_RX_DATA_AVAIL_IE    (0x02)
#define TBGRPI_CONF_RX_OVERFLOW_RESET   (0x04)

#define TBGRPI_ADDR_BIT_MASK            (0xf0)
#define TBGRPI_ADDR_BIT_SHIFT           (4)

// Address values
#define TBGRPI_REG_ADDR_CAN             (0)
#define TBGRPI_REG_ADDR_FILT1           (1)
#define TBGRPI_REG_ADDR_FILT2           (2)
#define TBGRPI_REG_ADDR_CFG1            (3)
#define TBGRPI_REG_NUMOF                (4) 

#define TBGRPI_CFG1_LOOPBACK            (0x00000001)
#define TBGRPI_CFG1_SILENT              (0x00000002)

#endif // TBGRPI_PROTOCOL_H
