/*
 *
 * tbg_api.c
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

#define _BSD_SOURCE
#define _GNU_SOURCE

#include <zmq.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <inttypes.h>
#include <sys/time.h>

#include <glib.h>

#include "debug.h"

#include "tbg_api.h"
#include "tbg_protocol.h"
#include "tbg_util.h"

#define ADISC_TIMEOUT       (20) // ms

#define TBG_DEFAULT_TIMEOUT (20) // ms

char *tbg_error_strings[] = TBG_ERROR_STRINGS;

static void *zcontext; // zmq context

void tbg_init(void)
{
    zcontext = zmq_ctx_new ();
}

tbg_socket_t *tbg_open(char *server_uri)
{
    int ret;
    tbg_socket_t *tsock = g_new(tbg_socket_t, 1);

    tsock->timeout = TBG_DEFAULT_TIMEOUT;

    tsock->zsocket = zmq_socket(zcontext, ZMQ_DEALER);
    if (tsock->zsocket == NULL) {
        g_free(tsock);
        return NULL;
    }

    ret = zmq_connect (tsock->zsocket, server_uri);
    if (ret != 0) {
        zmq_close(tsock->zsocket);
        g_free(tsock);
        return NULL;
    }

    return tsock;
}

void tbg_close(tbg_socket_t *tsock)
{
    zmq_close(tsock->zsocket);
    g_free(tsock);
}

int send_msg(tbg_socket_t *tsock, tbg_msg_t *msg)
{
    int ret;

    // Convert to hex
    char hex[TBG_HEX_MSG_SIZE];
    tbg_msg_to_hex(msg, hex);
    if (debug_level >= 2) {
        fprintf(stderr, "  sending: ");
        tbg_msg_dump(msg);
    }

    // Send zmq message
    ret = zmq_send(tsock->zsocket, hex, TBG_HEX_MSG_SIZE, 0);
    if (ret < 0) {
        WARNING("zmq_send: %s\n", zmq_strerror(errno));
    }
    return ret;
}


/*
 * Send a Touchbridge request. Optionally wait for a response if
 * resp is not NULL.
 */
int tbg_request(tbg_socket_t *tsock, int node, int port, uint8_t *data, int len, tbg_msg_t *resp)
{
    int ret;

    tbg_msg_t *req = g_new0(tbg_msg_t, 1);

    // Set-up ID
    TBG_MSG_SET_RTR(req, 0);
    TBG_MSG_SET_EID(req, 1);
    TBG_MSG_SET_DST_PORT(req, port);
    TBG_MSG_SET_DST_ADDR(req, node);
    // Source address gets filled-in by server.
    TBG_MSG_SET_TYPE(req, TBG_MSG_TYPE_REQ);

    // Length
    req->len = len;
    // Data
    memcpy(req->data, data, len);

    ret = send_msg(tsock, req);

    if (ret < 0) {
        ERROR("send_msg");
    }

    g_free(req);

    if (resp) {
        return tbg_wait_response(tsock, tsock->timeout, resp);
    } else {
        return 0;
    }
}

/*
 * Send a Touchbridge request. Waits for a response if resp is not NULL.
 */
int tbg_port_req_resp(tbg_port_t *port, uint8_t *data, int len, tbg_msg_t *resp)
{
    int ret;

    tbg_msg_t req;

    // Set-up ID
    req.id = 0;
    TBG_MSG_SET_RTR(&req, 0);
    TBG_MSG_SET_EID(&req, 1);
    TBG_MSG_SET_DST_PORT(&req, port->port);
    TBG_MSG_SET_DST_ADDR(&req, port->addr);
    TBG_MSG_SET_TYPE(&req, TBG_MSG_TYPE_REQ);

    // Length
    req.len = len;
    // Data
    memcpy(req.data, data, len);

    // Source address gets filled-in by server.

    ret = send_msg(port->tsock, &req);

    if (ret < 0) {
        ERROR("send_msg");
    }

    return tbg_wait_response2(port->tsock, port->timeout, resp, port->addr, port->port);
}

/*
 * Send a Touchbridge request. Waits for a response and discards it.
 */
int tbg_port_request(tbg_port_t *port, uint8_t *data, int len)
{
    tbg_msg_t resp;

    return tbg_port_req_resp(port, data, len, &resp);
}


#define RESP_BUF_SIZE   (256)

/*
 * Wait for a message to arrive with timeout.
 * Returns zero on timeout, non-zero on success.
 */
int tbg_wait_response(tbg_socket_t *tsock, int timeout, tbg_msg_t *resp)
{
    char buf[RESP_BUF_SIZE+1];
    int ret = 0;

    PRINTD(2, "    %s: called with timeout %d\n", __FUNCTION__, timeout);
    zmq_pollitem_t items [] = { { .socket = tsock->zsocket,  .fd = 0, .events = ZMQ_POLLIN, .revents = 0 } };
    int zret = zmq_poll (items, 1, timeout);
    SYSERROR_IF(zret < 0, "zmq_poll");
    if (items [0].revents & ZMQ_POLLIN) {
        int len = zmq_recv (tsock->zsocket, buf, RESP_BUF_SIZE, 0);
        if (len >= RESP_BUF_SIZE) ERROR("response buffer overflow");
        buf[len] = '\0';
        PRINTD(4, "response: len=%d, buf=\"%s\"\n", len, buf);
        if (resp != NULL) {
            tbg_msg_from_hex(resp, buf);
            if (TBG_MSG_IS_ERR_RESP(resp)) {
                uint8_t err_code = resp->data[0];
                if (err_code >= sizeof(tbg_error_strings)/sizeof(char*)) {
                    WARNING("Unknown touchbridge error code: %d", err_code);
                } else {
                    WARNING("Touchbridge error: %s", tbg_error_strings[err_code]);
                }
                ret = -1;
            } else {
                ret = 1;
            }
            if (debug_level >= 2) {
                fprintf(stderr, "    response: ");
                tbg_msg_dump(resp);
            }
        }
    }
    return ret;
}

/*
 * Wait for a response from a specific source address / port.
 * If port or address are negative, then match any port / address.
 * Because multiple poll calls (via wait_response) are made, we can't simply
 * use poll(2)'s timeout facility since non-matched addresses / ports could
 * reset the timeout and make use wait forever. Because of this, we use
 * gettimeofday and some timeval wrangling to see how much time is left after
 * each return from wait_response. Linux has a really nice alternative, in the
 * form of timerfd_create(2) but sadly this completely linux-specific and
 * is not available on OS X (BSD) and therefore not portable. We could
 * alternatively have used setittimer(2) to generate a signal but this is
 * arguably more pfaff than checking the current time.
 *
 * Returns zero on timeout, non-zero on successful match.
 */ 
int tbg_wait_response2(tbg_socket_t *tsock, int timeout, tbg_msg_t *resp, int addr, int port)
{
    int ret;

    struct timeval t_timeout, t_now, dt;

    gettimeofday(&t_now, NULL);

    // Convert from ms
    dt.tv_sec = timeout/1000;
    dt.tv_usec = (timeout % 1000) * 1000;

    // Calculate timeout time
    timeradd(&t_now, &dt, &t_timeout);

    PRINTD(2, "  %s: waiting for addr %d, port %d, timeout %d\n", __FUNCTION__, addr, port, timeout);
    ret = tbg_wait_response(tsock, timeout, resp);
    while (ret && timercmp(&t_now, &t_timeout, < ) ) {
        if ( ((addr >= 0) ? TBG_MSG_GET_SRC_ADDR(resp) == addr : 1)
          && ((port >= 0) ? TBG_MSG_GET_SRC_PORT(resp) == port : 1) ) {
            PRINTD(2, "  %s: matched addr %d, port %d\n", __FUNCTION__, TBG_MSG_GET_SRC_ADDR(resp), TBG_MSG_GET_SRC_PORT(resp) );
            return ret;
        }
        gettimeofday(&t_now, NULL);
        timersub(&t_timeout, &t_now, &dt);
        int timeleft = dt.tv_sec * 1000 + dt.tv_usec / 1000; // Convert to ms
        ret = tbg_wait_response(tsock, timeleft, resp);
    }
    PRINTD(2, "  %s: timeout\n", __FUNCTION__);
    return 0;
}

int tbg_port_wait_msg(tbg_port_t *port, int msg_type, int timeout, tbg_msg_t *msg)
{
    int ret;

    struct timeval t_timeout, t_now, dt;

    gettimeofday(&t_now, NULL);

    // Convert from ms
    dt.tv_sec = timeout/1000;
    dt.tv_usec = (timeout % 1000) * 1000;

    // Calculate timeout time
    timeradd(&t_now, &dt, &t_timeout);

    PRINTD(2, "  %s: waiting for addr %d, port %d, timeout %d\n", __FUNCTION__, port->addr, port->port, timeout);
    ret = tbg_wait_response(port->tsock, timeout, msg);
    while (ret && (timeout < 0 || timercmp(&t_now, &t_timeout, < )) ) {
        if ( (TBG_MSG_GET_SRC_ADDR(msg) == port->addr)
          && ((msg_type >= 0) ? TBG_MSG_GET_TYPE(msg) == msg_type : 1)
          && (TBG_MSG_GET_SRC_PORT(msg) == port->port) ) {
            PRINTD(2, "  %s: matched type %d, addr %d, port %d\n", __FUNCTION__, TBG_MSG_GET_TYPE(msg), TBG_MSG_GET_SRC_ADDR(msg), TBG_MSG_GET_SRC_PORT(msg) );
            return 1;
        }
        gettimeofday(&t_now, NULL);
        timersub(&t_timeout, &t_now, &dt);
        int timeleft = dt.tv_sec * 1000 + dt.tv_usec / 1000; // Convert to ms
        ret = tbg_wait_response(port->tsock, (timeout < 0) ? -1 : timeleft, msg);
    }
    PRINTD(2, "  %s: timeout\n", __FUNCTION__);
    return 0;
}

void adis_data_set(uint8_t *data, uint8_t cmd, uint8_t addr, uint64_t id)
{
    data[0] = cmd;
    data[1] = addr;
    data[2] = (id >>  0) & 0xff;
    data[3] = (id >>  8) & 0xff;
    data[4] = (id >> 16) & 0xff;
    data[5] = (id >> 24) & 0xff;
    data[6] = (id >> 32) & 0xff;
    data[7] = (id >> 40) & 0xff;
}

uint64_t adis_get_id(tbg_msg_t *msg)
{
    uint64_t ret = 
        (uint64_t)msg->data[0] <<  0 |
        (uint64_t)msg->data[1] <<  8 |
        (uint64_t)msg->data[2] << 16 |
        (uint64_t)msg->data[3] << 24 |
        (uint64_t)msg->data[4] << 32 |
        (uint64_t)msg->data[5] << 40;
    return ret;
}

void tbg_flush_responses(tbg_socket_t *tsock, int timeout)
{
    // (hopefully) flush all pending messages by grabing them with zero timeout.
    while (tbg_wait_response(tsock, timeout, NULL));
}

int tbg_get_conf_string(tbg_socket_t *tsock, uint8_t addr, uint8_t conf_cmd, uint8_t conf_port, char *s)
{
    PRINTD(2, "%s: called with addr %d, conf_port %d, cmd %d\n", __FUNCTION__, addr, conf_port, conf_cmd);
    tbg_msg_t resp;
    int ret;
    uint8_t msgdata[8];
    uint8_t len;

    msgdata[0] = conf_cmd;
    uint8_t offs = 0;
    do {
        if (conf_cmd & TBG_CONF_BIT_PORT) {
            len = 3;
            msgdata[1] = conf_port;
            msgdata[2] = offs;
        } else {
            len = 2;
            msgdata[1] = offs;
        }
        tbg_request(tsock, addr, TBG_PORT_CONFIG, msgdata, len, NULL);
        ret = tbg_wait_response2(tsock, ADISC_TIMEOUT, &resp, addr, TBG_PORT_CONFIG);
        if (ret && (resp.len > 1)) {
            strncpy(s+offs, (char *)resp.data, resp.len);
        }
        offs += 8;
    } while (ret && (resp.len > 1));
    return ret;
}

int tbg_get_conf_descr(tbg_socket_t *tsock, uint8_t addr, uint8_t conf_num, uint8_t conf_port, char *s)
{
    tbg_msg_t resp;
    int ret;
    uint8_t msgdata[8];

    msgdata[0] = TBG_PORTCONF_CMD_COM_GET_CONF_DESCR | TBG_CONF_BIT_PORT;
    uint8_t offs = 0;
    do {
        msgdata[1] = conf_port;
        msgdata[2] = conf_num;
        msgdata[3] = offs;
        tbg_request(tsock, addr, TBG_PORT_CONFIG, msgdata, 4, NULL);
        ret = tbg_wait_response2(tsock, ADISC_TIMEOUT, &resp, addr, TBG_PORT_CONFIG);
        if (ret && (resp.len > 1)) {
            strncpy(s+offs, (char *)resp.data, resp.len);
        }
        offs += 8;
    } while (ret && (resp.len > 1));
    return ret;
}


/*
 * Compare function for sorting on ID half-words.
 */
gint idhw_compare_fn(gconstpointer a, gconstpointer b)
{
    uint64_t A = *(uint64_t *)a;
    uint64_t B = *(uint64_t *)b;
    return (B < A) - (A < B);
}

/*
 * Compare function for sorting on full node IDs
 */
gint node_compare_fn(gconstpointer a, gconstpointer b)
{
    tbg_node_info_t *A = (void *)a;
    tbg_node_info_t *B = (void *)b;

    if (A->id_msw == B->id_msw) {
        return (B->id_lsw < A->id_lsw) - (A->id_lsw < B->id_lsw);
    } else {
        return (B->id_msw < A->id_msw) - (A->id_msw < B->id_msw);
    }
}

/*
 * Remove duplicate entries in an array (like unix uniq(1) command.)
 */
void unique_idhw(GArray *a)
{
    if (a->len  < 2) {
        return;
    }
    g_array_sort(a, idhw_compare_fn);
    uint64_t lastval = g_array_index(a, uint64_t, 0);
    for (int i = 1; i < a->len; i++) {
        uint64_t val = g_array_index(a, uint64_t, i);
        if (val == lastval) {
            g_array_remove_index(a, i);
            i--; // We've shortened the array, so need to undo next increment.
        }
        lastval = val;
    }
}

void adisc_unassign(tbg_socket_t *tsock, uint8_t addr)
{
    uint8_t msgdata[2];

    msgdata[0] = TBG_ADISC_BIT_ASSIG_ADDR | TBG_ADISC_BIT_CLR_SHORTLIST;
    msgdata[1] = TBG_ADDR_UNASSIGNED; // Set all addresses to a temporary value & clear shortlist
    tbg_request(tsock, addr, TBG_PORT_ADISC, msgdata, 2, NULL);
    // No response is expected for this request, so we
    // need to wait for the devices to receive it. 1ms is plenty.
    usleep(1000);
}

GArray *adisc_stage1(tbg_socket_t *tsock, uint8_t addr, int timeout)
{
    PRINTD(2, "%s: called with addr %d\n", __FUNCTION__, addr);
    uint8_t msgdata[8];
    tbg_msg_t resp;

    // Create an array to put responses in.
    GArray *msw_ids = g_array_new(FALSE, FALSE, sizeof(uint64_t));

    // Request MSW of ID.
    adis_data_set(msgdata, TBG_ADISC_BIT_RETURN_ID | TBG_ADISC_BIT_RETURN_ID_MSW, 0, 0);
    tbg_request(tsock, addr, TBG_PORT_ADISC, msgdata, 2, NULL);

    // Get responses and put them in the array.
    int ret = tbg_wait_response2(tsock, timeout, &resp, -1, TBG_PORT_ADISC);
    while (ret) {
        uint64_t id_msw = adis_get_id(&resp);
        g_array_append_val(msw_ids, id_msw);
        ret = tbg_wait_response2(tsock, timeout, &resp, -1, TBG_PORT_ADISC);
    }
    // Remove duplicates so we get a set of unique ID MSW's.
    unique_idhw(msw_ids);

    PRINTD(2, "%s: got %d MSW ID responses\n", __FUNCTION__, msw_ids->len);
    return msw_ids;

}

// TODO: use shortlist to enable thorough exploration of address
// space 
GArray *adisc_stage2(tbg_socket_t *tsock, uint8_t addr, GArray *msw_ids, int timeout)
{
    PRINTD(2, "%s: called with addr %d\n", __FUNCTION__, addr);
    uint8_t msgdata[8];
    tbg_msg_t resp;

    // Create an array to put ID and address info in.
    GArray *node_info = g_array_new(FALSE, FALSE, sizeof(tbg_node_info_t));

    // For each MSW ID returned, get LSW's of ID and set their
    // shortlist.
    for (int i = 0; i < msw_ids->len; i++) {
        uint64_t id_msw = g_array_index(msw_ids, uint64_t, i);

        // Send request.
        adis_data_set(msgdata, TBG_ADISC_BIT_MATCH_ID | TBG_ADISC_BIT_MATCH_ID_MSW | TBG_ADISC_BIT_RETURN_ID, 0, id_msw);
        tbg_request(tsock, addr, TBG_PORT_ADISC, msgdata, 8, NULL);

        // Get responses and put them in the array.
        int ret = tbg_wait_response2(tsock, timeout, &resp, -1, TBG_PORT_ADISC);
        while (ret) {
            tbg_node_info_t info;
            info.id_msw = id_msw;
            info.id_lsw = adis_get_id(&resp);
            info.addr = resp.data[6];
            info.product_id = NULL;
            g_array_append_val(node_info, info);
            ret = tbg_wait_response2(tsock, timeout, &resp, -1, TBG_PORT_ADISC);
        }
    }
    PRINTD(2, "%s: got %d nodes\n", __FUNCTION__, node_info->len);
    return node_info;
}

GArray *tbg_adisc(tbg_socket_t *tsock, int mode, uint8_t addr)
{
    PRINTD(2, "%s: called with addr %d\n", __FUNCTION__, addr);

    // Send a command to reset all devices to address 63 (unassigned)
    if (mode == ADISC_MODE_RESET) {
        // The number (3) of times to re-send the command to is somewhat
        // arbitrary but experience shows that it often needs ti be sent
        // more than once.
        for (int i = 0; i < 3; i++) {
            adisc_unassign(tsock, TBG_ADDR_BROADCAST);
        }
    }

    GArray *msw_ids = adisc_stage1(tsock, addr, ADISC_TIMEOUT*4);

    // Didn't find any devices, try again once.
    if (msw_ids->len == 0) {
        WARNING("%s: didn't get any responses at stage1, trying again...\n", __FUNCTION__);
        g_array_free(msw_ids, TRUE);
        msw_ids = adisc_stage1(tsock, addr, ADISC_TIMEOUT*4);
    }

    // Still didn't find any devices.
    if (msw_ids->len == 0) {
        g_array_free(msw_ids, TRUE);
        return NULL;
    }

    GArray *node_info = adisc_stage2(tsock, addr, msw_ids, ADISC_TIMEOUT*2);

    g_array_sort(node_info, node_compare_fn);

    return node_info;
}

void clear_shortlist(tbg_socket_t *tsock, uint8_t addr)
{
    uint8_t msgdata[2];

    PRINTD(2, "%s: called with addr %d\n", __FUNCTION__, addr);
    // Clear shortlist flag for all devices
    msgdata[0] = TBG_ADISC_BIT_CLR_SHORTLIST;
    msgdata[1] = addr;
    tbg_request(tsock, TBG_ADDR_BROADCAST, TBG_PORT_ADISC, msgdata, 2, NULL);
    // No response is expected for this request, so we
    // need to wait for the devices to receive it. 1ms is plenty.
    usleep(1000);
}

int assign(tbg_socket_t *tsock, tbg_node_info_t *node_info, uint8_t addr)
{
    tbg_msg_t resp;
    uint8_t msgdata[8];
    int ret;

    PRINTD(2, "%s: called with addr %d\n", __FUNCTION__, addr);

    // Clear all shortlist flags
    clear_shortlist(tsock, TBG_ADDR_BROADCAST);
    clear_shortlist(tsock, TBG_ADDR_BROADCAST);

    // Match MSW, set shortlist flag, no response expeceted
    adis_data_set(msgdata, TBG_ADISC_BIT_MATCH_ID | TBG_ADISC_BIT_MATCH_ID_MSW | TBG_ADISC_BIT_SET_SHORTLIST, 0, node_info->id_msw);
    tbg_request(tsock, addr, TBG_PORT_ADISC, msgdata, 8, NULL);
    usleep(1000);
    tbg_request(tsock, addr, TBG_PORT_ADISC, msgdata, 8, NULL);
    usleep(1000);

    // Match LSW, match shortlist flag, set address. We also request the LSW ID again in order to generate a response.
    adis_data_set(msgdata,
            TBG_ADISC_BIT_MATCH_ID | TBG_ADISC_BIT_MATCH_SHORTLIST | TBG_ADISC_BIT_ASSIG_ADDR | TBG_ADISC_BIT_RETURN_ID,
            node_info->addr, node_info->id_lsw);

    // Try a few times
    for (int i = 0; i < 3; i++) {
        tbg_request(tsock, addr, TBG_PORT_ADISC, msgdata, 8, NULL);
        ret = tbg_wait_response2(tsock, ADISC_TIMEOUT, &resp, addr, TBG_PORT_ADISC);
        uint64_t id_lsw = adis_get_id(&resp);
        uint8_t assigned_addr = resp.data[6];
        if ( ret && (id_lsw == node_info->id_lsw) && (assigned_addr == node_info->addr) ) {
            clear_shortlist(tsock, node_info->addr);
            return 1;
        }
    }
    PRINTD(2, "%s: assignment failed for addr %d\n", __FUNCTION__, addr);
    return 0;
}

int get_lowest_free_addr(GArray *nodes)
{
    int *addresses = g_new0(int, 64);
    int ret = -1;

    for (int i = 0; i < nodes->len; i++) {
        tbg_node_info_t *n = &g_array_index(nodes, tbg_node_info_t, i);
            if (n->addr == TBG_ADDR_UNASSIGNED) {
                continue;
            }
            // TODO: detect duplicate addresses and de-assign one of them.
            addresses[n->addr] = 1;
    }
    for (int i = 1; i < 62; i++) {
        if (addresses[i] == 0) {
            ret = i;
            break;
        }
    }
    g_free(addresses);
    return ret;
    
}

void get_product_ids(tbg_socket_t *tsock, GArray *nodes)
{
    char str[256];

    for (int i = 0; i < nodes->len; i++) {
        tbg_node_info_t *n = &g_array_index(nodes, tbg_node_info_t, i);
        int ret;
        ret = tbg_get_conf_string(tsock, n->addr, TBG_CONF_GLOBAL_PRODUCT_ID_STR, 0, str);
        if (!ret) {
            // Try once more
            ret = tbg_get_conf_string(tsock, n->addr, TBG_CONF_GLOBAL_PRODUCT_ID_STR, 0, str);
            if (!ret) {
                WARNING("couldn't get product id for 0x%012" PRIX64 "%012" PRIX64, n->id_msw, n->id_lsw);
            }
        }
        n->product_id = g_strdup(str);
    }
}

GArray *tbg_adisc_and_assign(tbg_socket_t *tsock)
{
    PRINTD(2, "%s:\n", __FUNCTION__);
    GArray *nodes = tbg_adisc(tsock, ADISC_MODE_REFRESH, TBG_ADDR_BROADCAST); 
    if (nodes == NULL) {
        return NULL;
    }
    for (int i = 0; i < nodes->len; i++) {
        tbg_node_info_t *n = &g_array_index(nodes, tbg_node_info_t, i);
        if (n->addr == TBG_ADDR_UNASSIGNED) {
            n->addr = get_lowest_free_addr(nodes);
            if (n->addr < 0) {
                n->addr = TBG_ADDR_UNASSIGNED;
                WARNING("ran out of addresses");
                return nodes;
            }
            assign(tsock, n, TBG_ADDR_UNASSIGNED);
        }
    }
    get_product_ids(tsock, nodes);
    return nodes;
}

void tbg_print_nodes_json(GArray *nodes)
{
    int nodes_found = 0;
    printf("[\n");
    for (int i = 0; i < nodes->len; i++) {
        tbg_node_info_t *n = &g_array_index(nodes, tbg_node_info_t, i);

        if (nodes_found) {
            printf(",\n");
        }
        printf("    { \"addr\":%d, \"id\":\"0x%012" PRIX64 "%012" PRIX64 "\", \"product\":%s }", n->addr, n->id_msw, n->id_lsw, n->product_id);
        nodes_found++;
    }
    printf("\n]\n");
}


tbg_node_info_t *tbg_node_info(tbg_socket_t *tsock, uint8_t addr)
{
    uint8_t msgdata[8];
    tbg_msg_t resp;
    char s[256];
    s[0] = '\0';
    int ret;
    tbg_node_info_t *node = g_new(tbg_node_info_t, 1);

    node->addr = addr;

    adis_data_set(msgdata, TBG_ADISC_BIT_RETURN_ID | TBG_ADISC_BIT_RETURN_ID_MSW, 0, 0);
    ret = tbg_request(tsock, addr, TBG_PORT_ADISC, msgdata, 2, &resp);
    if (!ret) { printf("Timeout\n"); return NULL; }
    node->id_msw = adis_get_id(&resp);

    adis_data_set(msgdata, TBG_ADISC_BIT_RETURN_ID, 0, 0);
    ret = tbg_request(tsock, addr, TBG_PORT_ADISC, msgdata, 2, &resp);
    if (!ret) { printf("Timeout\n"); return NULL; }
    node->id_lsw = adis_get_id(&resp);

    ret = tbg_get_conf_string(tsock, addr, TBG_CONF_GLOBAL_PRODUCT_ID_STR, 0, s);
    if (!ret) { printf("Timeout\n"); return NULL; }

    node->product_id = g_strdup(s);

    return node;
}

void tbg_node_info_free(tbg_node_info_t *node)
{
    g_free(node->product_id);
    g_free(node);
}

tbg_port_t *tbg_port_open(tbg_socket_t *tsock, uint8_t addr, uint8_t portnum)
{
    tbg_port_t *port = g_new(tbg_port_t, 1);
    port->tsock = tsock;
    port->addr = addr;
    port->port = portnum;
    port->timeout = tsock->timeout;
    return port;
}

void tbg_port_close(tbg_port_t *port)
{
    g_free(port);
}

int tbg_dout(tbg_port_t *port, uint32_t value, uint32_t mask)
{
    uint32_t req_data[2] = { value, mask };
    PRINTD(2, "dout: addr %d, port %d, value 0x%08X, mask 0x%08X\n", port->addr, port->port, value, mask);
    return tbg_port_request(port, (uint8_t *)req_data, sizeof(req_data));
}

int tbg_aout(tbg_port_t *port, int pin, int value)
{
    uint8_t req_data[3];
    value = (value < 0) ? 0 : value;
    value = (value > 0xffff) ? 0xffff : value;
    req_data[0] = pin;
    req_data[1] = (value >> 0) & 0xff;
    req_data[2] = (value >> 8) & 0xff;
    PRINTD(2, "aout: addr %d, port %d, pin %d, value %d\n", port->addr, port->port, pin, value);
    return tbg_port_request(port, (uint8_t *)req_data, sizeof(req_data));
}

int tbg_ain(tbg_port_t *port, int pin, int16_t *result)
{
    uint8_t req_data[2];
    req_data[0] = pin;
    req_data[1] = 1; // Request just one channel

    PRINTD(2, "ain: addr %d, port %d, pin %d\n", port->addr, port->port, pin);

    tbg_msg_t resp;

    int ret = tbg_port_req_resp(port, (uint8_t *)req_data, sizeof(req_data), &resp);

    if (ret)
        *result = resp.data16[0];

    return ret;
}


int tbg_port_conf_write(tbg_port_t *port, uint8_t cmd, uint8_t *conf_data, int len)
{
    uint8_t req_data[8];
    tbg_port_t *conf_port = g_memdup(port, sizeof(tbg_port_t));
    conf_port->port = TBG_PORT_CONFIG;

    req_data[TBG_CONF_REQ_DATA_CMD] = TBG_CONF_BIT_PORT | TBG_CONF_BIT_WRITE | (TBG_CONF_BITS_CMD & cmd);
    req_data[TBG_CONF_REQ_DATA_PORT] = port->port;
    assert(len <= 6);
    memcpy(req_data+2, conf_data, len);
    int ret = tbg_port_request(conf_port, req_data, len+TBG_PORTCONF_REQ_LEN_MIN);
    g_free(conf_port);
    return ret;
}
