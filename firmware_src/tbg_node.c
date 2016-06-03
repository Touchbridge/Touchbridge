/*
 *
 * tbg_node.c
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
#include "tbg_stm32.h"
#include "tbg_node.h"
#include "version.h"

/*
 * Returns non-zero if msg's destination address matches the
 * 'my_addr' field in node.
 */
int tbg_node_check_addr(tbg_node_t *node, tbg_msg_t *msg)
{
    return (TBG_MSG_GET_DST_ADDR(msg) == node->my_addr);
}

/*
 * As above but also returns non-zero for broadcast messages.
 */
int tbg_check_addr_or_broadcast(tbg_node_t *node, tbg_msg_t *msg)
{
    return (TBG_MSG_IS_BROADCAST(msg) || (TBG_MSG_GET_DST_ADDR(msg) == node->my_addr));
}

/*
 * Helper function for tbg_port_mux().
 */
static int call_port_fn(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp, tbg_port_t *port)
{
    if (port->fn == NULL) {
        return tbg_err_resp(req, resp, TBG_ERR_UNIMPLEMENTED);
    }
    return port->fn(node, req, resp, port);
}

/*
 * Helper function for tbg_port_mux() and do_conf().
 */
static tbg_port_t *find_port(tbg_node_t *node, uint8_t port_number)
{
    // Check if it's a common port
    if (port_number < TBG_DEVICE_PORT_BASE) {
        return node->common_ports + port_number;
    } else {
        // Search through device port list
        for (int i = 0; i < node->num_ports; i++) {
            if (node->ports[i].port_number == port_number) {
                return node->ports + i;
            }
        }
        return NULL;
    }
}

/*
 * Scan the ports in the supplied node and call a port function if possible.
 * Fills in response structure 'resp'. If 'resp' contains a valid response
 * (and that includes error responses), the function returns 1. If 'resp'
 * should not be sent, zero is returned.
 *
 * This function assumes that req is a valid Touchbridge message. If in
 * doubt, check it with tbg_msg_is_valid() before calling this fn.
 *
 * This function also assumes the destination address of req has already
 * been checked.
 */
int tbg_port_mux(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp)
{
    // We don't want to respond to RESPonse messages - could
    // cause an endless loop of messages.
    if (!tbg_msg_check_not_resp(req)) {
        return 0;
    }

    // Get the response message ready, default to zero length.
    tbg_resp(req, resp, 0, 0, 0);

    // Explicitly set src addr in case we're responding to a broadcast
    // message.
    TBG_MSG_SET_SRC_ADDR(resp, node->my_addr);

    uint8_t port_number = TBG_MSG_GET_DST_PORT(req);

    tbg_port_t *port = find_port(node, port_number);
    if (port) {
        return call_port_fn(node, req, resp, port);
    } else {
        // Port not found
        return tbg_err_resp(req, resp, TBG_ERR_NO_PORT);
    }
}

/*
 * Transmit a Touchbridge message.
 */
void tbg_msg_tx(tbg_msg_t *msg)
{
    TBG_MSG_SET_RTR(msg, 0);
    TBG_MSG_SET_EID(msg, 1);
    tbg_can_tx(msg);
}

/*****************************************************************************
 * Common port functions
 *****************************************************************************/

static int tbg_port_fn_tstrigger(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp, tbg_port_t *port)
{
    return tbg_err_resp(req, resp, TBG_ERR_UNIMPLEMENTED);
}

static int tbg_port_fn_adisc(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp, tbg_port_t *port)
{
    int notmatch = 0;

    if (req->len < TBG_ADISC_REQ_LEN_MIN) {
        return tbg_err_resp(req, resp, TBG_ERR_LENGTH);
    }

    uint8_t cmd = req->data[TBG_ADISC_REQ_DATA_CMD];

    // Match ID half-word in request
    if (cmd & TBG_ADISC_BIT_MATCH_ID) {
        if (req->len < TBG_ADISC_REQ_LEN_ID) {
            return tbg_err_resp(req, resp, TBG_ERR_LENGTH);
        }
        if (cmd & TBG_ADISC_BIT_MATCH_ID_MSW) {
            notmatch = memcmp(req->data + TBG_ADISC_REQ_DATA_ID0, tbg_stm32_unique_id->bytes + TBG_NODE_ID_HALF_WORD_SIZE, TBG_NODE_ID_HALF_WORD_SIZE);
        } else {
            notmatch = memcmp(req->data + TBG_ADISC_REQ_DATA_ID0, tbg_stm32_unique_id->bytes, TBG_NODE_ID_HALF_WORD_SIZE);
        }
    }

    // Match shortlist flag
    if (cmd & TBG_ADISC_BIT_MATCH_SHORTLIST) {
        notmatch = !( !notmatch && node->shortlist_flag);
    }

    // If a match was requested and we didn't get one,
    // don't perform any further functions.
    if (notmatch) {
        return 0;
    }

    // Set our address from one supplied in request
    if (cmd & TBG_ADISC_BIT_ASSIG_ADDR) {
        node->my_addr = req->data[TBG_ADISC_REQ_DATA_ADDR];
    }

    // Set Shortlist flag
    if (cmd & TBG_ADISC_BIT_SET_SHORTLIST) {
        node->shortlist_flag = 1;
    }

    // Clear Shortlist flag
    if (cmd & TBG_ADISC_BIT_CLR_SHORTLIST) {
        node->shortlist_flag = 0;
    }

    // Return ID half-word
    if (cmd & TBG_ADISC_BIT_RETURN_ID) {
        if (cmd & TBG_ADISC_BIT_RETURN_ID_MSW) {
            memcpy(resp->data + TBG_ADISC_RESP_DATA_ID0, tbg_stm32_unique_id->bytes + TBG_NODE_ID_HALF_WORD_SIZE, TBG_NODE_ID_HALF_WORD_SIZE);
        } else {
            memcpy(resp->data + TBG_ADISC_RESP_DATA_ID0, tbg_stm32_unique_id->bytes, TBG_NODE_ID_HALF_WORD_SIZE);
        }
        resp->data[TBG_ADISC_RESP_DATA_SOFT_ADDR] = node->my_addr;
        resp->data[TBG_ADISC_RESP_DATA_HARD_ADDR] = 0xff; // FIXME: Get hard address
        resp->len = 8;
        return 1; // Send response
    }

    return 0;
}

/*
 * Helper function for tbg_port_fn_config().
 */
static int call_conf_fn(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp, tbg_port_t *port, tbg_conf_t *conf, uint8_t write_flag)
{
    if (!conf->wr_fn && !conf->rd_fn) {
        return tbg_err_resp(req, resp, TBG_ERR_UNIMPLEMENTED);
    }
    if (write_flag) {
        if (conf->wr_fn) {
            return conf->wr_fn(node, req, resp, port, conf);
        } else {
            return tbg_err_resp(req, resp, TBG_ERR_RDONLY);
        }
    } else {
        if (conf->rd_fn) {
            return conf->rd_fn(node, req, resp, port, conf);
        } else {
            return tbg_err_resp(req, resp, TBG_ERR_WRONLY);
        }
    }
}

/*
 * Helper function for finding a conf.
 */
static tbg_conf_t *find_conf(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp, tbg_port_t *port,  uint8_t conf_num)
{
    tbg_conf_t *conf = NULL;

    if (conf_num < TBG_DEVICE_PORTCONF_CMD_BASE) {
        // Common Port Configs
        if (conf_num >= TBG_PORTCONF_CMD_COM_NUMOF) {
            tbg_err_resp(req, resp, TBG_ERR_UNIMPLEMENTED);
            return NULL;
        }
        conf = node->port_common_confs + conf_num;
    } else {
        conf_num -= TBG_DEVICE_PORTCONF_CMD_BASE;
        if (conf_num >= port->num_confs) {
            // Config not found
            tbg_err_resp(req, resp, TBG_ERR_NO_CONF);
            return NULL;
        }
        conf = port->confs + conf_num;
    }
    return conf;
}

/*
 * Read or write configuration.
 */
static int tbg_port_fn_config(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp, tbg_port_t *conf_port)
{
    (void)conf_port; // Not used
    tbg_port_t *port = NULL;
    tbg_conf_t *conf;

    if (req->len < TBG_CONF_REQ_LEN_MIN) {
        return tbg_err_resp(req, resp, TBG_ERR_LENGTH);
    }

    uint8_t conf_bits = req->data[TBG_CONF_REQ_DATA_CMD];
    uint8_t cmd = conf_bits & TBG_CONF_BITS_CMD;
    uint8_t write_flag = conf_bits & TBG_CONF_BIT_WRITE;

    if (conf_bits & TBG_CONF_BIT_PORT) {
        if (req->len < TBG_PORTCONF_REQ_LEN_MIN) {
            return tbg_err_resp(req, resp, TBG_ERR_LENGTH);
        }
        uint8_t port_number = req->data[TBG_CONF_REQ_DATA_PORT];
        port = find_port(node, port_number);
        if (!port) {
            // Port not found
            return tbg_err_resp(req, resp, TBG_ERR_NO_PORT);
        }
        conf = find_conf(node, req, resp, port, cmd);
        if (conf == NULL) {
            // find_conf will have set the appropriate error response
            return 1;
        }
    } else {
        // Global config
        if (cmd >= TBG_CONF_GLOBAL_NUMOF) {
            // Command not found
            return tbg_err_resp(req, resp, TBG_ERR_NO_CONF);
        }
        conf = node->confs + cmd;
    }
    return call_conf_fn(node, req, resp, port, conf, write_flag);
}

/*
 * Get and clear fault flags.
 */
static int tbg_port_fn_faults(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp, tbg_port_t *conf_port)
{
    (void)conf_port; // Not used

    if (req->len != 0 && req->len != 2) {
        return tbg_err_resp(req, resp, TBG_ERR_LENGTH);
    }
    if (req->len == 2) {
        uint16_t fault_resets;
        fault_resets = req->data[0];
        fault_resets |= req->data[1] << 8;
        node->faults &= ~fault_resets;
    }
    resp->data[0] = (node->faults >> 0) & 0xff;
    resp->data[1] = (node->faults >> 8) & 0xff;
    resp->len = 2;
    return 1;
}

tbg_port_t tbg_common_ports[] = {
    {
        .fn = tbg_port_fn_tstrigger,
        .fn_data = NULL,
        .descr = "Timestamp Trigger",
        .confs = NULL,
        .num_confs = 0,
        .port_number = TBG_PORT_TSTRIGGER,
        .port_class = TBG_PORT_CLASS_COMMON,
    },
    {
        .fn = tbg_port_fn_adisc,
        .fn_data = NULL,
        .descr = "Address Discovery",
        .confs = NULL,
        .num_confs = 0,
        .port_number = TBG_PORT_ADISC,
        .port_class = TBG_PORT_CLASS_COMMON,
    },
    {
        .fn = tbg_port_fn_config,
        .fn_data = NULL,
        .descr = "Configuration",
        .confs = NULL,
        .num_confs = 0,
        .port_number = TBG_PORT_CONFIG,
        .port_class = TBG_PORT_CLASS_COMMON,
    },
    {
        .fn = tbg_port_fn_faults,
        .fn_data = NULL,
        .descr = "Faults",
        .confs = NULL,
        .num_confs = 0,
        .port_number = TBG_PORT_FAULTS,
        .port_class = TBG_PORT_CLASS_COMMON,
    },

};

static int tbg_conf_fn_global_noop(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp, tbg_port_t *port, tbg_conf_t *conf)
{
    return 0;
}

static int tbg_conf_fn_global_ping(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp, tbg_port_t *port, tbg_conf_t *conf)
{
    // Send data back unchanged.
    memcpy(resp->data, req->data, req->len);
    resp->len = req->len;
    return 1;
}

static int tbg_conf_fn_global_protocol(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp, tbg_port_t *port, tbg_conf_t *conf)
{
    uint8_t len = 0;

    resp->data[len++] = TBG_PROTOCOL_VER_TYPE;
    resp->data[len++] = TBG_PROTOCOL_VER_MAJOR;
    resp->data[len++] = TBG_PROTOCOL_VER_MINOR;
    resp->len = len;
    return 1;
}

static int tbg_conf_fn_global_reset(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp, tbg_port_t *port, tbg_conf_t *conf)
{
    // TODO
    return tbg_err_resp(req, resp, TBG_ERR_UNIMPLEMENTED);
}

static int tbg_conf_fn_global_id_lsw(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp, tbg_port_t *port, tbg_conf_t *conf)
{
    memcpy(resp->data, tbg_stm32_unique_id->bytes, TBG_NODE_ID_HALF_WORD_SIZE);
    resp->len = TBG_NODE_ID_HALF_WORD_SIZE;
    return 1;
}

static int tbg_conf_fn_global_id_msw(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp, tbg_port_t *port, tbg_conf_t *conf)
{
    memcpy(resp->data, tbg_stm32_unique_id->bytes + TBG_NODE_ID_HALF_WORD_SIZE, TBG_NODE_ID_HALF_WORD_SIZE);
    resp->len = TBG_NODE_ID_HALF_WORD_SIZE;
    return 1;
}

static int tbg_conf_fn_global_blink(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp, tbg_port_t *port, tbg_conf_t *conf)
{
    if (req->len > TBG_CONF_REQ_LEN_MIN && req->data[1]) {
        //tbg_stm32_blink_leds_start();
        return tbg_err_resp(req, resp, TBG_ERR_UNIMPLEMENTED);
    } else {
        //tbg_stm32_blink_leds_stop();
        return tbg_err_resp(req, resp, TBG_ERR_UNIMPLEMENTED);
    }

    return 1;
}

/*
 * Helper function for reading strings.
 */
static int read_str8(tbg_msg_t *req, tbg_msg_t *resp, const char *string, uint8_t offset)
{
    uint8_t slen = strlen(string) + 1; // Include '\0' at end of string
    if (offset >= slen) {
        resp->len = 1;
        resp->data[0] = '\0';
    } else {
        resp->len = slen - offset;
        if (resp->len > 8) {
            resp->len = 8;
        }
        memcpy(resp->data, string + offset, resp->len);
    }
    return 1;
}

static int tbg_conf_fn_global_product_id(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp, tbg_port_t *port, tbg_conf_t *conf)
{
    // TODO: find some nicer way of specifying length requirements of data field for config params
    if (req->len < TBG_CONF_REQ_LEN_MIN + 1) {
        return tbg_err_resp(req, resp, TBG_ERR_LENGTH);
    }
    uint8_t offset = req->data[1];
    return read_str8(req, resp, node->product_id, offset);
}

static int tbg_conf_fn_global_firmware_version(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp, tbg_port_t *port, tbg_conf_t *conf)
{
    // TODO: find some nicer way of specifying length requirements of data field for config params
    if (req->len < TBG_CONF_REQ_LEN_MIN + 1) {
        return tbg_err_resp(req, resp, TBG_ERR_LENGTH);
    }
    uint8_t offset = req->data[1];

    // tbg_version_string is in tbg_version.c, which is generated from git
    // info at build-time. It is declared in tbg_version.h.
    // tbg_version.c should not be tracked by git.
    // FIXME: the aforementioned files don't exist yet.
    return read_str8(req, resp, version, offset);
}


tbg_conf_t tbg_global_confs[] = {
    {
        .wr_fn = tbg_conf_fn_global_noop,
        .rd_fn = tbg_conf_fn_global_noop,
        .descr = "NOP (Send no response)",
    },
    {
        .wr_fn = tbg_conf_fn_global_ping,
        .rd_fn = tbg_conf_fn_global_ping,
        .descr = "Ping (Echo request data back to sender)",
    },
    {
        .wr_fn = NULL,
        .rd_fn = tbg_conf_fn_global_protocol,
        .descr = "Touchbrige Protocol Version",
    },
    {
        .wr_fn = tbg_conf_fn_global_reset,
        .rd_fn = NULL,
        .descr = "Reset Node",
    },
    {
        .wr_fn = NULL,
        .rd_fn = tbg_conf_fn_global_id_lsw,
        .descr = "Node ID (Least Significant Word)",
    },
    {
        .wr_fn = NULL,
        .rd_fn = tbg_conf_fn_global_id_msw,
        .descr = "Node ID (Most Significant Word)",
    },
    {
        .wr_fn = NULL,
        .rd_fn = NULL,
        .descr = "Write Hard Address to non-volatile memory",
    },
    {
        .wr_fn = tbg_conf_fn_global_blink,
        .rd_fn = NULL,
        .descr = "Blink LEDs",
    },
    {
        .wr_fn = NULL,
        .rd_fn = tbg_conf_fn_global_product_id,
        .descr = "Product ID",
    },
    {
        .wr_fn = NULL,
        .rd_fn = tbg_conf_fn_global_firmware_version,
        .descr = "Firmware Version",
    },
    {
        .wr_fn = NULL,
        .rd_fn = NULL,
        .descr = "User ID String",
    },
    {
        .wr_fn = NULL,
        .rd_fn = NULL,
        .descr = "Write User ID String to non-volatile memory",
    },
};

static int tbg_conf_fn_port_common_class(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp, tbg_port_t *port, tbg_conf_t *conf)
{
    resp->data[0] = port->port_class;
    resp->data[1] = port->num_confs;
    resp->len = 2;
    return 1;
}

static int tbg_conf_fn_port_common_descr(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp, tbg_port_t *port, tbg_conf_t *conf)
{
    if (req->len < TBG_PORTCONF_REQ_LEN_MIN + 1) {
        return tbg_err_resp(req, resp, TBG_ERR_LENGTH);
    }
    uint8_t offset = req->data[2];
    return read_str8(req, resp, port->descr, offset);
}

static int tbg_conf_fn_port_common_conf_descr(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp, tbg_port_t *port, tbg_conf_t *conf)
{
    if (req->len < TBG_PORTCONF_REQ_LEN_MIN + 1) {
        return tbg_err_resp(req, resp, TBG_ERR_LENGTH);
    }
    uint8_t conf_num = req->data[2];
    tbg_conf_t *portconf = find_conf(node, req, resp, port, conf_num);
    if (portconf == NULL) {
        // find_conf will have set the appropriate error response
        return 1;
    }
    uint8_t offset = req->data[3];
    if (portconf->descr == NULL) {
        return read_str8(req, resp, "", offset);
    } else {
        return read_str8(req, resp, portconf->descr, offset);
    }
}

static int tbg_conf_fn_port_common_tstrigger(tbg_node_t *node, tbg_msg_t *req, tbg_msg_t *resp, tbg_port_t *port, tbg_conf_t *conf)
{
    return tbg_err_resp(req, resp, TBG_ERR_UNIMPLEMENTED);
}

tbg_conf_t tbg_port_common_confs[TBG_PORTCONF_CMD_COM_NUMOF] = {
    {
        .wr_fn = NULL,
        .rd_fn = tbg_conf_fn_port_common_class,
        .descr = "Port Class",
    },
    {
        .wr_fn = NULL,
        .rd_fn = tbg_conf_fn_port_common_descr,
        .descr = "Port Description",
    },
    {
        .wr_fn = NULL,
        .rd_fn = tbg_conf_fn_port_common_conf_descr,
        .descr = "Port Config Description",
    },
    {
        .wr_fn = tbg_conf_fn_port_common_tstrigger,
        .rd_fn = NULL,
        .descr = "Timestamp Trigger",
    },
};
