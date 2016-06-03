/*
 *
 * tbg_msg.h
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
#ifndef TBG_MSG_H
#define TBG_MSG_H

#include <stdint.h>

#include "tbg_protocol.h"

typedef struct tbg_msg_fifo_s {
    uint8_t in;
    uint8_t out;
    uint8_t used;
    uint8_t size;
    tbg_msg_t *bufs;
} tbg_msg_fifo_t;

#define TBG_MSG_FIFO_EMPTY(f)    ((f)->used == 0)

int8_t tbg_msg_fifo_in(tbg_msg_fifo_t *f, tbg_msg_t *msg);
int8_t tbg_msg_fifo_out(tbg_msg_fifo_t *f, tbg_msg_t *msg);

int tbg_msg_is_valid(tbg_msg_t *msg);
int tbg_msg_check_not_resp(tbg_msg_t *req);
void tbg_msg_init(tbg_msg_t *msg);
int tbg_err_resp(tbg_msg_t *req, tbg_msg_t *resp, uint8_t err_code);
void tbg_resp(tbg_msg_t *req, tbg_msg_t *resp, uint8_t state, uint8_t cont, uint8_t len);

#endif // TBG_MSG_H
