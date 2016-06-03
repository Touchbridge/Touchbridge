/*
 *
 * tbg_util.c
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
#include <ctype.h>

#include "tbg_util.h"

char *msg_type_names[] = { "RES", "REQ", "ERR", "IND" };

void tbg_msg_dump(tbg_msg_t *msg)
{
    uint8_t type = TBG_MSG_GET_TYPE(msg);

    printf("%s ", msg_type_names[type]);
    printf("%02d:%02d => ", TBG_MSG_GET_SRC_ADDR(msg), TBG_MSG_GET_SRC_PORT(msg));
    printf("%02d:%02d ", TBG_MSG_GET_DST_ADDR(msg), TBG_MSG_GET_DST_PORT(msg));
    for (int i = 0; i < msg->len; i++) {
        printf(" 0x%02X", msg->data[i]);
    }
    printf("           ");
    for (int i = 0; i < msg->len; i++) {
        if (isprint(msg->data[i])) {
            putchar(msg->data[i]);
        } else {
            putchar(' ');
        }
    }
    printf("\n");
    fflush(stdout);
}

static int dehex(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    } else if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    } else if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    } else {
        return -1;
    }
}

int tbg_msg_from_hex(tbg_msg_t *msg, char *hex)
{
    uint8_t *rawmsg = (void *)msg;
    int i;
    for (i = 0; i < TBG_MSG_SIZE; i++) {
        int upper = dehex(hex[i*2]);
        if (upper < 0) break;
        int lower = dehex(hex[i*2+1]);
        if (lower < 0) break;
        rawmsg[i] = (upper << 4) | lower;
    }
    return i;
}

static const char *hexdigits = "0123456789ABCDEF";

// String must be at least TBG_MSG_SIZE x2 + 1 in size
int tbg_msg_to_hex(tbg_msg_t *msg, char *hex)
{
    uint8_t *rawmsg = (void *)msg;
    int j = 0;
    for (int i = 0; i < TBG_MSG_SIZE; i++) {
        hex[j++]   = hexdigits[rawmsg[i] >> 4];
        hex[j++] = hexdigits[rawmsg[i] & 0xf];
    }
    hex[j++] = 0; // Null-terminate the string.
    return j;
}


