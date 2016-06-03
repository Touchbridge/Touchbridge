/*
 *
 * tbg_node.h
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
#ifndef TBG_NODE_H
#define TBG_NODE_H

/*
 * tbg_node.h
 *
 * Functions for implementing Touchbridge peripheral nodes.
 */

#include "tbg_protocol.h"

struct tbg_node_s;
struct tbg_port_s;
struct tbg_conf_s;

// This should return non-zero if the resp msg is to be sent
typedef int port_fn_t(struct tbg_node_s *node, tbg_msg_t *req, tbg_msg_t *resp, struct tbg_port_s *port);
typedef int config_fn_t(struct tbg_node_s *node, tbg_msg_t *req, tbg_msg_t *resp, struct tbg_port_s *port, struct tbg_conf_s *conf);

typedef struct tbg_conf_s {
    config_fn_t *wr_fn;
    config_fn_t *rd_fn;
    const char *descr;
} tbg_conf_t;

typedef struct tbg_port_s {
    port_fn_t *fn;
    void *fn_data;
    const char *descr;
    void *conf_data;
    tbg_conf_t *confs;
    uint8_t num_confs;
    uint8_t port_number;
    uint8_t port_class;
} tbg_port_t;


typedef struct tbg_node_s {
    tbg_port_t *common_ports;
    tbg_port_t *ports;
    tbg_conf_t *confs; // Global confs
    tbg_conf_t *port_common_confs;
    const char *product_id;
    uint16_t faults;
    uint8_t num_ports;
    uint8_t my_addr;
    uint8_t shortlist_flag;
} tbg_node_t;

extern tbg_port_t tbg_common_ports[];
extern tbg_conf_t tbg_global_confs[];
extern tbg_conf_t tbg_port_common_confs[];

int tbg_node_check_addr(tbg_node_t *node, tbg_msg_t *msg);
int tbg_check_addr_or_broadcast(tbg_node_t *node, tbg_msg_t *msg);
int tbg_port_mux(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp);
void tbg_msg_tx(tbg_msg_t *msg);

#endif // TBG_NODE_H
