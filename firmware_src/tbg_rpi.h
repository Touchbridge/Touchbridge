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
#ifndef TBGRPI_H
#define TBGRPI_H

/*
 * tbg_rpi.h
 *
 * Touchbridge Raspberry Pi HAT interface (STM32 side)
 */

#include "tbg_protocol.h"
#include "tbgrpi_protocol.h"

#define GNU_SRC

void tbg_rpi_wr_data(uint8_t data);
uint8_t tbg_rpi_rd_data(void);
void tbg_rpi_wr_addr_config(uint8_t data);
uint8_t tbg_rpi_rd_addr_status(void);

void tbg_rpi_txe_int(void);
void tbg_rpi_rxda_int(tbg_msg_t *msg);

#endif // TBGRPI_H
