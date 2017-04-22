/*
 *
 * tbg_api.h
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

#ifndef TBG_API_H
#define TBG_API_H

#include "tbg_protocol.h"

#define ADISC_MODE_REFRESH  (0)
#define ADISC_MODE_RESET    (1)

#define TBG_PORT_ANY        (-1)   // For tbg_wait_response2() - match any port
#define TBG_MSG_TYPE_ANY    (-1)   // For tbg_port_wait_msg() - match any message type
#define TBG_TIMEOUT_FOREVER (-1)   // For tbg_port_wait_msg() - wait forever

typedef struct {
    void *zsocket; // 0MQ socket
    int timeout;
} tbg_socket_t;

typedef struct {
    tbg_socket_t *tsock;
    uint8_t addr;
    uint8_t port;
    int timeout;
} tbg_port_t;

typedef struct tbg_node_info_s {
    uint64_t id_msw;
    uint64_t id_lsw;
    uint8_t addr;
    char *product_id;
} tbg_node_info_t;

void tbg_init(void);
tbg_socket_t *tbg_open(char *server_uri);
void tbg_close(tbg_socket_t *tsock);
tbg_port_t *tbg_port_open(tbg_socket_t *tsock, uint8_t addr, uint8_t portnum);
void tbg_port_close(tbg_port_t *port);
int tbg_request(tbg_socket_t *tsock, int node, int port, uint8_t *data, int len, tbg_msg_t *resp);
int tbg_wait_response(tbg_socket_t *tsock, int timeout, tbg_msg_t *resp);
int tbg_wait_response2(tbg_socket_t *tsock, int timeout, tbg_msg_t *resp, int addr, int port);
void tbg_flush_responses(tbg_socket_t *tsock, int timeout);
GArray *tbg_adisc(tbg_socket_t *tsock, int mode, uint8_t addr);
GArray *tbg_adisc_and_assign(tbg_socket_t *tsock);
int tbg_get_conf_string(tbg_socket_t *tsock, uint8_t addr, uint8_t conf_cmd, uint8_t conf_port, char *s);
int tbg_get_conf_descr(tbg_socket_t *tsock, uint8_t addr, uint8_t conf_num, uint8_t conf_port, char *s);
void tbg_print_nodes_json(GArray *nodes);
tbg_node_info_t *tbg_node_info(tbg_socket_t *tsock, uint8_t addr);
void tbg_node_info_free(tbg_node_info_t *node);

int tbg_dout(tbg_port_t *port, uint32_t value, uint32_t mask);
int tbg_aout(tbg_port_t *port, int pin, int value);
int tbg_ain(tbg_port_t *port, int pin, int16_t *result);

int tbg_port_conf_write(tbg_port_t *port, uint8_t cmd, uint8_t *conf_data, int len);
int tbg_port_wait_msg(tbg_port_t *port, int msg_type, int timeout, tbg_msg_t *msg);

#endif // TBG_API_H
