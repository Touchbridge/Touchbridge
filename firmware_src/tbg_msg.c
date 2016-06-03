/*
 *
 * tbg_msg.c
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

#include <string.h>

#include "tbg_msg.h"

/*
 * Return 1 if msg is a valid Touchbridge CAN message, else
 * return 0.
 */
int tbg_msg_is_valid(tbg_msg_t *msg)
{
    // Touchbridge only uses extended ID's.
    if (!TBG_MSG_GET_EID(msg)) {
        return 0;
    }

    // RTR messages not used in Touchbridge for now.
    if (TBG_MSG_GET_RTR(msg)) {
        return 0;
    }

    return 1;
}

/*
 * Check that a message is not a response of some kind.
 */
int tbg_msg_check_not_resp(tbg_msg_t *req)
{
    // Get the request type
    uint8_t type = TBG_MSG_GET_TYPE(req);

    if ((type == TBG_MSG_TYPE_RESP) || (type == TBG_MSG_TYPE_ERR_RESP)) {
        return 0;
    } else {
        return 1;
    }
}

/*
 * Initialise a Touchbridge message.
 */
void tbg_msg_init(tbg_msg_t *msg)
{
    msg->id = 0;
    msg->len = 0;
}

/*
 * Create an error response message in resp from
 * supplied request message req. Returns 1 if resp
 * should be transmitted, else zero. Does not respond
 * if request was a broadcast message.
 */
int tbg_err_resp(tbg_msg_t *req, tbg_msg_t *resp, uint8_t err_code)
{
    if (TBG_MSG_IS_BROADCAST(req)) {
        return 0;
    } else {
        tbg_resp(req, resp, 0, 0, 1);
        TBG_MSG_SET_TYPE(resp, TBG_MSG_TYPE_ERR_RESP);
        resp->data[0] = err_code;
        return 1;
    }
}

/*
 * Create a Touchbridge response to req in resp.
 */
void tbg_resp(tbg_msg_t *req, tbg_msg_t *resp, uint8_t state, uint8_t cont, uint8_t len)
{
    uint8_t req_src_port = TBG_MSG_GET_SRC_PORT(req);
    uint8_t req_src_addr = TBG_MSG_GET_SRC_ADDR(req);
    uint8_t req_dst_port = TBG_MSG_GET_DST_PORT(req);
    uint8_t req_dst_addr = TBG_MSG_GET_DST_ADDR(req);

    tbg_msg_init(resp);
    TBG_MSG_SET_TYPE(resp, TBG_MSG_TYPE_RESP);
    TBG_MSG_SET_STATE(resp, state);
    TBG_MSG_SET_CONT(resp, cont);
    TBG_MSG_SET_DST_PORT(resp, req_src_port);
    TBG_MSG_SET_DST_ADDR(resp, req_src_addr);
    TBG_MSG_SET_SRC_PORT(resp, req_dst_port);
    TBG_MSG_SET_SRC_ADDR(resp, req_dst_addr);
    resp->len = len;
}

int8_t tbg_msg_fifo_in(tbg_msg_fifo_t *f, tbg_msg_t *msg)
{
    // Check for overflow
    if (f->used == f->size) {
        return 0;
    }
    // Copy data into fifo buffer.
    memcpy(f->bufs + f->in++, msg, TBG_MSG_SIZE);
    // Wrap input pointer.
    f->in = (f->in >= f->size) ? 0 : f->in;
    f->used++;
    return 1;
}

int8_t tbg_msg_fifo_out(tbg_msg_fifo_t *f, tbg_msg_t *msg)
{
    // Check for underflow
    if (f->used == 0) {
        return 0;
    }
    // Copy data from fifo buffer.
    memcpy(msg, f->bufs + f->out++, TBG_MSG_SIZE);
    // Wrap output pointer.
    f->out = (f->out >= f->size) ? 0 : f->out;
    f->used--;
    return 1;
}

