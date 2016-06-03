/*
 *
 * tbg_client.c
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

/*
 * Touchbridge Client
 *
 * J.Macfarlane 2015-10-15
 */

#define _BSD_SOURCE
#define _GNU_SOURCE

#include <zmq.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <libgen.h> // For basename()
#include <string.h>
#include <ctype.h>
#include <stdarg.h>
#include <inttypes.h>
#include <sys/time.h>

#include <glib.h>

#include "debug.h"

#include "tbg_util.h"
#include "tbg_protocol.h"

char *tbg_error_strings[] = TBG_ERROR_STRINGS;

#define SRC_ADDR (60)

#define MAX_LINE_LENGTH (80)

#define ADISC_TIMEOUT       (20) // ms

static int resp_timeout = 10; // Time to wait for response in ms.

// For debug.h
char *progname;
int debug_level = 0;

void flush_responses(void *zsocket, int timeout);
int wait_response(void *zsocket, int timeout, tbg_msg_t *resp);
int wait_response2(void *zsocket, int timeout, tbg_msg_t *resp, int addr, int port);

int get_int(char *s, int *x)
{
    char *end;
    int tmp = strtol(s, &end, 0);
    if (end > s) {
        *x = tmp;
        return 1;
    } else {
        ERROR("Failed to parse \"%s\" as integer.", s);
        return 0;
    }
}

int send_request(void *zsocket, int node, int port, uint8_t *data, int len, tbg_msg_t *resp)
{
    int ret;

    tbg_msg_t *req = g_new0(tbg_msg_t, 1);

    // Set-up ID
    TBG_MSG_SET_RTR(req, 0);
    TBG_MSG_SET_EID(req, 1);
    TBG_MSG_SET_DST_PORT(req, port);
    TBG_MSG_SET_DST_ADDR(req, node);
    TBG_MSG_SET_TYPE(req, TBG_MSG_TYPE_REQ);

    // Length
    req->len = len;
    // Data
    memcpy(req->data, data, len);

    // Source address gets filled-in by server.


    // Convert to hex
    char hex[TBG_HEX_MSG_SIZE];
    tbg_msg_to_hex(req, hex);
    if (debug_level >= 2) {
        fprintf(stderr, "  sending: ");
        tbg_msg_dump(req);
    }
    g_free(req);

    // Send zmq message
    ret = zmq_send(zsocket, hex, TBG_HEX_MSG_SIZE, 0);
    if (ret < 0) {
        ERROR("zmq_send: %s\n", zmq_strerror(errno));
    }

    if (resp) {
        return wait_response(zsocket, resp_timeout, resp);
    } else {
        return 0;
    }
}

#define RESP_BUF_SIZE   (256)

/*
 * Wait for a message to arrive with timeout.
 * Returns zero on timeout, non-zero on success.
 */
int wait_response(void *zsocket, int timeout, tbg_msg_t *resp)
{
    char buf[RESP_BUF_SIZE+1];

    PRINTD(2, "    %s: called with timeout %d\n", __FUNCTION__, timeout);
    zmq_pollitem_t items [] = { { .socket = zsocket,  .fd = 0, .events = ZMQ_POLLIN, .revents = 0 } };
    int ret = zmq_poll (items, 1, timeout);
    SYSERROR_IF(ret < 0, "zmq_poll");
    if (items [0].revents & ZMQ_POLLIN) {
        int len = zmq_recv (zsocket, buf, RESP_BUF_SIZE, 0);
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
            }
            if (debug_level >= 2) {
                fprintf(stderr, "    response: ");
                tbg_msg_dump(resp);
            }
        }
        return 1;
    }
    return 0;
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
int wait_response2(void *zsocket, int timeout, tbg_msg_t *resp, int addr, int port)
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
    ret = wait_response(zsocket, timeout, resp);
    while (ret && timercmp(&t_now, &t_timeout, < ) ) {
        if ( ((addr >= 0) ? TBG_MSG_GET_SRC_ADDR(resp) == addr : 1)
          && ((port >= 0) ? TBG_MSG_GET_SRC_PORT(resp) == port : 1) ) {
            PRINTD(2, "  %s: matched addr %d, port %d\n", __FUNCTION__, TBG_MSG_GET_SRC_ADDR(resp), TBG_MSG_GET_SRC_PORT(resp) );
            return 1;
        }
        gettimeofday(&t_now, NULL);
        timersub(&t_timeout, &t_now, &dt);
        int timeleft = dt.tv_sec * 1000 + dt.tv_usec / 1000; // Convert to ms
        ret = wait_response(zsocket, timeleft, resp);
    }
    PRINTD(2, "  %s: timeout\n", __FUNCTION__);
    return 0;
}

// Get a line from stdin and parse up to nargs values as integers.
int get_input(void *zsocket, int nargs, ...)
{
    char line[MAX_LINE_LENGTH];
    va_list ap;

{
    char buf[RESP_BUF_SIZE+1];

    /*
     * Discard incomming ZMQ messages while waiting for input in stdin.
     */
    while (1) {
        zmq_pollitem_t items [] = {
            { .socket = zsocket,  .fd = 0, .events = ZMQ_POLLIN, .revents = 0 },
            { .socket = NULL,  .fd = STDIN_FILENO, .events = ZMQ_POLLIN, .revents = 0 },
        };
        int ret = zmq_poll (items, 2, -1);
        SYSERROR_IF(ret < 0, "zmq_poll");
        if (items [0].revents & ZMQ_POLLIN) {
            // Got a ZMQ message
            int len = zmq_recv (zsocket, buf, RESP_BUF_SIZE, 0);
            if (len >= RESP_BUF_SIZE) ERROR("response buffer overflow");
            buf[len] = '\0';
            PRINTD(4, "discarded zmq msg: len=%d, buf=\"%s\"\n", len, buf);
        }
        if (items [1].revents & ZMQ_POLLIN) {
            // Got something on stdin.
            PRINTD(3, "got data on stdin\n");
            break;
        }
    }
}

    char * ret = fgets(line, MAX_LINE_LENGTH, stdin);
    if (ret == NULL)
        return 0;

    va_start(ap, nargs);
    char *start = line, *end;
    int  x;
    x = strtol(start, &end, 0);
    int i = 0;
    for (i = 0; (end != start) && i < nargs; i++) {
        *va_arg(ap, int *) = x;
        start = end;
        while (isblank(*start) || *start == ',') start++;
        if (*start == 0) break;
        x = strtol(start, &end, 0);
    }
    va_end(ap);
    return i;
}

void tbgc_req_init(tbg_msg_t *msg, uint8_t addr, uint8_t port, uint8_t len)
{
    msg->id = 0;
    TBG_MSG_SET_DST_PORT(msg, port);
    TBG_MSG_SET_DST_ADDR(msg, addr);
    TBG_MSG_SET_TYPE(msg, TBG_MSG_TYPE_REQ);
    msg->len = len;
}

void tbgc_msg_set32(tbg_msg_t *msg, uint8_t offset, uint32_t data)
{
    uint32_t *dptr = (void *)(msg->data + offset);
    dptr[0] = data;
}

uint32_t tbgc_msg_get32(tbg_msg_t *msg, uint8_t offset)
{
    uint32_t *dptr = (void *)(msg->data + offset);
    return dptr[0];
}

void tbgc_data_set32(uint8_t *data, uint32_t value, uint8_t offset)
{
    data[offset+0] = (value >>  0) & 0xff;
    data[offset+1] = (value >>  8) & 0xff;
    data[offset+2] = (value >> 16) & 0xff;
    data[offset+3] = (value >> 24) & 0xff;
}

void tbgc_portclass_dout(void *zsocket, uint8_t addr, uint32_t value, uint32_t mask)
{
    uint32_t msgdata[2];
    msgdata[0] = value;
    msgdata[1] = mask;
    uint8_t port = 8;
    PRINTD(2, "dout: addr %d, port %d, value 0x%02X, mask 0x%02X\n", addr, port, value, mask);
    send_request(zsocket, addr, port, (void *)msgdata, sizeof(msgdata), NULL);
    flush_responses(zsocket, 0);
}

int dout_cmd(int argc, char **argv, void *zsocket)
{
    int ret;
    uint8_t mask, value;
    int node = atoi(argv[1]);
    int pin = atoi(argv[2]);
    if (pin < 1 || pin > 8) 
        ERROR("pin number \"%s\" out of bounds", argv[2]);
    pin--;
    mask = 1 << pin;
    if (argc > 3) {
        // Get value from cmd line, send it & exit
        value = atoi(argv[3]);
        value = (value) ? 0xff : 0;
        tbgc_portclass_dout(zsocket, node, value, mask);
    } else {
        // Get values from stdin & send them until EOF.
        ret = get_input(zsocket, 1, &value);
        while (!feof(stdin)) {
            if (ret > 0) {
                // Value is boolean.
                value = (value) ? 0xff : 0;
                tbgc_portclass_dout(zsocket, node, value, mask);
            } else {
                fprintf(stderr, "Expected numeric value on stdin (ctrl-d to exit.)\n");
            }
            ret = get_input(zsocket, 1, &value);
        }
    }

    return 0;
}

void dout_usage(char *name)
{
    printf("usage: %s %s node pin_number [value]\n", progname, name);
}

int dout_cmd2(int argc, char **argv, void *zsocket)
{
    int ret;
    int mask = 0;
    int value;
    int node = atoi(argv[1]);
    int port = 8;
    get_int(argv[2], &mask);
    if (argc > 3) {
        // Get value from cmd line, send it & exit
        get_int(argv[3], &value);
        PRINTD(2, "node %d, port %d, value 0x%02X, mask 0x%02X\n", node, port, value, mask);
        uint8_t data[] = { (uint8_t)value, 0, 0, 0, (uint8_t)mask, 0, 0, 0 };
        send_request(zsocket, node, port, data, sizeof(data), NULL);
    } else {
        // Get values from stdin & send them until EOF.
        ret = get_input(zsocket, 1, &value);
        while (!feof(stdin)) {
            if (ret > 0) {
                // Value is boolean.
                PRINTD(2, "node %d, port %d, value 0x%02X, mask 0x%02X\n", node, port, value, mask);
                uint8_t data[] = { (uint8_t)value, 0, 0, 0, (uint8_t)mask, 0, 0, 0 };
                send_request(zsocket, node, port, data, sizeof(data), NULL);
            } else {
                fprintf(stderr, "Expected numeric value on stdin (ctrl-d to exit.)\n");
            }
            ret = get_input(zsocket, 1, &value);
        }
    }

    return 0;
}

void dout_cmd2_usage(char *name)
{
    printf("usage: %s %s node mask [value]\n", progname, name);
}

int din_cmd(int argc, char **argv, void *zsocket)
{
    tbg_msg_t msg;

    int addr = atoi(argv[1]);
    int pin = atoi(argv[2]);
    if (pin < 1 || pin > 8) 
        ERROR("pin number \"%s\" out of bounds", argv[2]);
    pin--;
    uint32_t mask = 1 << pin;
    // Ensure rising & falling event masks are set
    // This also serves a second purpose which is to 
    // tell the server we're here.
    // TODO: think of a better way, so we can just set the
    // mask bits we want but read-modify-write races when dealing
    // with multiple clients.  For now, we set the mask for all
    // bits.
    uint8_t msgdata1[] = { 0xC8, 8, 0xff, 0, 0, 0 };
    uint8_t msgdata2[] = { 0xC9, 8, 0xff, 0, 0, 0 };
    send_request(zsocket, addr, 2, (void *)msgdata1, sizeof(msgdata1), NULL);
    send_request(zsocket, addr, 2, (void *)msgdata2, sizeof(msgdata2), NULL);

    while (1) {
        wait_response(zsocket, -1, &msg);
        if (TBG_MSG_GET_TYPE(&msg) == TBG_MSG_TYPE_IND &&
            TBG_MSG_GET_SRC_PORT(&msg) == 8 &&
            TBG_MSG_GET_SRC_ADDR(&msg) == addr) {
            uint32_t events = tbgc_msg_get32(&msg, 0);
            uint32_t state = tbgc_msg_get32(&msg, 4);
            if (events & mask) {
                //printf("mask = 0x%08X, events = 0x%08X, state = 0x%08X\n", mask, events, state);
                printf("%c\n", (state & mask) ? '1' : '0');
                fflush(stdout);
            }
        }
    }
    return 0;
}

void din_usage(char *name)
{
    printf("usage: %s %s node pin\n", progname, name);
}

void tbgc_portclass_aout(void *zsocket, uint8_t addr, uint8_t pin, int value)
{
    uint8_t msgdata[3];
    value = (value < 0) ? 0 : value;
    msgdata[0] = pin;
    msgdata[1] = (value >> 0) & 0xff;
    msgdata[2] = (value >> 8) & 0xff;
    uint8_t port = 11;
    PRINTD(2, "dout: addr %d, port %d, pin %d, value %d\n", addr, port, pin, value);
    send_request(zsocket, addr, port, (void *)msgdata, sizeof(msgdata), NULL);
    flush_responses(zsocket, 0);
}

int aout_cmd(int argc, char **argv, void *zsocket)
{
    int ret;
    int value;
    int node = atoi(argv[1]);
    int pin = atoi(argv[2]);
    if (pin < 1 || pin > 8) 
        ERROR("pin number \"%s\" out of bounds", argv[2]);
    pin--;
    if (argc > 3) {
        // Get value from cmd line, send it & exit
        value = atoi(argv[3]);
        tbgc_portclass_aout(zsocket, node, pin, value);
    } else {
        // Get values from stdin & send them until EOF.
        ret = get_input(zsocket, 1, &value);
        while (!feof(stdin)) {
            if (ret > 0) {
                tbgc_portclass_aout(zsocket, node, pin, value);
            } else {
                fprintf(stderr, "Expected numeric value on stdin (ctrl-d to exit.)\n");
            }
            ret = get_input(zsocket, 1, &value);
        }
    }
    return 0;
}

void aout_usage(char *name)
{
    printf("usage: %s node pin [value]\n", name);
}

int ain_cmd(int argc, char **argv, void *zsocket)
{
    return 0;
}

void ain_usage(char *name)
{
    printf("Not implemented yet\n");
    printf("usage: %s node pin\n", name);
}

int tbg_cmd(int argc, char **argv, void *zsocket)
{
    tbg_msg_t resp;
    uint8_t msgdata[8];

    int argptr = 1;
    uint8_t dst_addr = strtol(argv[argptr++], NULL, 0);
    uint8_t dst_port = strtol(argv[argptr++], NULL, 0);

    int j =0;
    for (int i = argptr; i < argc; i++) {
        char *s = argv[i];
        int len = strlen(s);
        unsigned long long data = strtoll(s, NULL, 0);
        void *p = &data;
        int width = 1;
        if (s[0] == '0' && s[1] == 'x') {
            width = (len-1)/2;
        }
        if ((j + width) >= 8) {
            width = 8-j;
            if (width <= 0) {
                break;
            }
        }
        memcpy(msgdata+j, p, width);
        j += width;
    }
    int ret = send_request(zsocket, dst_addr, dst_port, msgdata, j, &resp);
    if (!ret) {
        printf("Timeout\n");
    } else {
        tbg_msg_dump(&resp);
    }
    return 0;
}

void tbg_usage(char *name)
{
    printf("usage: %s %s dst_addr dst_port [data]\n", progname, name);
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

void flush_responses(void *zsocket, int timeout)
{
    // (hopefully) flush all pending messages by grabing them with zero timeout.
    while (wait_response(zsocket, timeout, NULL));
}

int get_conf_string(void *zsocket, uint8_t addr, uint8_t conf_cmd, uint8_t conf_port, char *s)
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
        send_request(zsocket, addr, TBG_PORT_CONFIG, msgdata, len, NULL);
        ret = wait_response2(zsocket, ADISC_TIMEOUT, &resp, addr, TBG_PORT_CONFIG);
        if (ret && (resp.len > 1)) {
            strncpy(s+offs, (char *)resp.data, resp.len);
        }
        offs += 8;
    } while (ret && (resp.len > 1));
    return ret;
}

int get_conf_descr(void *zsocket, uint8_t addr, uint8_t conf_num, uint8_t conf_port, char *s)
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
        send_request(zsocket, addr, TBG_PORT_CONFIG, msgdata, 4, NULL);
        ret = wait_response2(zsocket, ADISC_TIMEOUT, &resp, addr, TBG_PORT_CONFIG);
        if (ret && (resp.len > 1)) {
            strncpy(s+offs, (char *)resp.data, resp.len);
        }
        offs += 8;
    } while (ret && (resp.len > 1));
    return ret;
}

#define ADISC_MODE_REFRESH  (0)
#define ADISC_MODE_RESET    (1)


typedef struct tbg_node_info_s {
    uint64_t id_msw;
    uint64_t id_lsw;
    uint8_t addr;
    char *product_id;
} tbg_node_info_t;


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

void adisc_unassign(void *zsocket, uint8_t addr)
{
    uint8_t msgdata[2];

    msgdata[0] = TBG_ADISC_BIT_ASSIG_ADDR | TBG_ADISC_BIT_CLR_SHORTLIST;
    msgdata[1] = TBG_ADDR_UNASSIGNED; // Set all addresses to a temporary value & clear shortlist
    send_request(zsocket, addr, TBG_PORT_ADISC, msgdata, 2, NULL);
    // No response is expected for this request, so we
    // need to wait for the devices to receive it. 1ms is plenty.
    usleep(1000);
}

GArray *adisc_stage1(void *zsocket, uint8_t addr, int timeout)
{
    PRINTD(2, "%s: called with addr %d\n", __FUNCTION__, addr);
    uint8_t msgdata[8];
    tbg_msg_t resp;

    // Create an array to put responses in.
    GArray *msw_ids = g_array_new(FALSE, FALSE, sizeof(uint64_t));

    // Request MSW of ID.
    adis_data_set(msgdata, TBG_ADISC_BIT_RETURN_ID | TBG_ADISC_BIT_RETURN_ID_MSW, 0, 0);
    send_request(zsocket, addr, TBG_PORT_ADISC, msgdata, 2, NULL);

    // Get responses and put them in the array.
    int ret = wait_response2(zsocket, timeout, &resp, -1, TBG_PORT_ADISC);
    while (ret) {
        uint64_t id_msw = adis_get_id(&resp);
        g_array_append_val(msw_ids, id_msw);
        ret = wait_response2(zsocket, timeout, &resp, -1, TBG_PORT_ADISC);
    }
    // Remove duplicates so we get a set of unique ID MSW's.
    unique_idhw(msw_ids);

    PRINTD(2, "%s: got %d MSW ID responses\n", __FUNCTION__, msw_ids->len);
    return msw_ids;

}

// TODO: use shortlist to enable thorough exploration of address
// space 
GArray *adisc_stage2(void *zsocket, uint8_t addr, GArray *msw_ids, int timeout)
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
        send_request(zsocket, addr, TBG_PORT_ADISC, msgdata, 8, NULL);

        // Get responses and put them in the array.
        int ret = wait_response2(zsocket, timeout, &resp, -1, TBG_PORT_ADISC);
        while (ret) {
            tbg_node_info_t info;
            info.id_msw = id_msw;
            info.id_lsw = adis_get_id(&resp);
            info.addr = resp.data[6];
            info.product_id = NULL;
            g_array_append_val(node_info, info);
            ret = wait_response2(zsocket, timeout, &resp, -1, TBG_PORT_ADISC);
        }
    }
    PRINTD(2, "%s: got %d nodes\n", __FUNCTION__, node_info->len);
    return node_info;
}

GArray *adisc(void *zsocket, int mode, uint8_t addr)
{
    PRINTD(2, "%s: called with addr %d\n", __FUNCTION__, addr);

    // Send a command to reset all devices to address 63 (unassigned)
    if (mode == ADISC_MODE_RESET) {
        // The number (3) of times to re-send the command to is somewhat
        // arbitrary but experience shows that it often needs ti be sent
        // more than once.
        for (int i = 0; i < 3; i++) {
            adisc_unassign(zsocket, TBG_ADDR_BROADCAST);
        }
    }

    GArray *msw_ids = adisc_stage1(zsocket, addr, ADISC_TIMEOUT*4);

    // Didn't find any devices, try again once.
    if (msw_ids->len == 0) {
        WARNING("%s: didn't get any responses at stage1, trying again...\n", __FUNCTION__);
        g_array_free(msw_ids, TRUE);
        msw_ids = adisc_stage1(zsocket, addr, ADISC_TIMEOUT*4);
    }

    // Still didn't find any devices.
    if (msw_ids->len == 0) {
        g_array_free(msw_ids, TRUE);
        return NULL;
    }

    GArray *node_info = adisc_stage2(zsocket, addr, msw_ids, ADISC_TIMEOUT*2);

    g_array_sort(node_info, node_compare_fn);

    return node_info;
}

void clear_shortlist(void *zsocket, uint8_t addr)
{
    uint8_t msgdata[2];

    PRINTD(2, "%s: called with addr %d\n", __FUNCTION__, addr);
    // Clear shortlist flag for all devices
    msgdata[0] = TBG_ADISC_BIT_CLR_SHORTLIST;
    msgdata[1] = addr;
    send_request(zsocket, TBG_ADDR_BROADCAST, TBG_PORT_ADISC, msgdata, 2, NULL);
    // No response is expected for this request, so we
    // need to wait for the devices to receive it. 1ms is plenty.
    usleep(1000);
}

int assign(void *zsocket, tbg_node_info_t *node_info, uint8_t addr)
{
    tbg_msg_t resp;
    uint8_t msgdata[8];
    int ret;

    PRINTD(2, "%s: called with addr %d\n", __FUNCTION__, addr);

    // Clear all shortlist flags
    clear_shortlist(zsocket, TBG_ADDR_BROADCAST);
    clear_shortlist(zsocket, TBG_ADDR_BROADCAST);

    // Match MSW, set shortlist flag, no response expeceted
    adis_data_set(msgdata, TBG_ADISC_BIT_MATCH_ID | TBG_ADISC_BIT_MATCH_ID_MSW | TBG_ADISC_BIT_SET_SHORTLIST, 0, node_info->id_msw);
    send_request(zsocket, addr, TBG_PORT_ADISC, msgdata, 8, NULL);
    usleep(1000);
    send_request(zsocket, addr, TBG_PORT_ADISC, msgdata, 8, NULL);
    usleep(1000);

    // Match LSW, match shortlist flag, set address. We also request the LSW ID again in order to generate a response.
    adis_data_set(msgdata,
            TBG_ADISC_BIT_MATCH_ID | TBG_ADISC_BIT_MATCH_SHORTLIST | TBG_ADISC_BIT_ASSIG_ADDR | TBG_ADISC_BIT_RETURN_ID,
            node_info->addr, node_info->id_lsw);

    // Try a few times
    for (int i = 0; i < 3; i++) {
        send_request(zsocket, addr, TBG_PORT_ADISC, msgdata, 8, NULL);
        ret = wait_response2(zsocket, ADISC_TIMEOUT, &resp, addr, TBG_PORT_ADISC);
        uint64_t id_lsw = adis_get_id(&resp);
        uint8_t assigned_addr = resp.data[6];
        if ( ret && (id_lsw == node_info->id_lsw) && (assigned_addr == node_info->addr) ) {
            clear_shortlist(zsocket, node_info->addr);
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

GArray *adisc_and_assign(void *zsocket)
{
    PRINTD(2, "%s:\n", __FUNCTION__);
    GArray *nodes = adisc(zsocket, ADISC_MODE_REFRESH, TBG_ADDR_BROADCAST); 
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
            assign(zsocket, n, TBG_ADDR_UNASSIGNED);
        }
    }
    return nodes;
}

void print_nodes_json(GArray *nodes)
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

void get_product_ids(void *zsocket, GArray *nodes)
{
    char str[256];

    for (int i = 0; i < nodes->len; i++) {
        tbg_node_info_t *n = &g_array_index(nodes, tbg_node_info_t, i);
        int ret;
        ret = get_conf_string(zsocket, n->addr, TBG_CONF_GLOBAL_PRODUCT_ID_STR, 0, str);
        if (!ret) {
            // Try once more
            ret = get_conf_string(zsocket, n->addr, TBG_CONF_GLOBAL_PRODUCT_ID_STR, 0, str);
            if (!ret) {
                WARNING("couldn't get product id for 0x%012" PRIX64 "%012" PRIX64, n->id_msw, n->id_lsw);
            }
        }
        n->product_id = g_strdup(str);
    }
}


int adisc2_cmd(int argc, char **argv, void *zsocket)
{
    GArray *nodes = adisc_and_assign(zsocket);
    if (nodes == NULL || nodes->len == 0) {
        WARNING("no nodes found\n");
        return 1;
    }
    get_product_ids(zsocket, nodes);
    print_nodes_json(nodes);
    return 0;
}

int uniq_test(int argc, char **argv, void *zsocket)
{
    GArray *msw_ids = g_array_new(FALSE, FALSE, sizeof(uint64_t));
    uint64_t val;
    val = 0x1234567B;
    g_array_append_val(msw_ids, val);
    val = 0x12345679;
    g_array_append_val(msw_ids, val);
    val = 0x1234567A;
    g_array_append_val(msw_ids, val);
    val = 0x1234567B;
    g_array_append_val(msw_ids, val);
    val = 0x12345679;
    g_array_append_val(msw_ids, val);
    val = 0x12345679;
    g_array_append_val(msw_ids, val);
    val = 0x12345679;
    g_array_append_val(msw_ids, val);
    val = 0x1234567A;
    g_array_append_val(msw_ids, val);
    val = 0x1234567B;
    g_array_append_val(msw_ids, val);

    g_array_sort(msw_ids, idhw_compare_fn);
    printf("Sorted array:\n");
    for (int i = 0; i < msw_ids->len; i++) {
        printf("%d: 0x%" PRIX64 "\n", i, g_array_index(msw_ids, uint64_t, i));
    }

    unique_idhw(msw_ids);
    printf("Uniq'd array:\n");
    for (int i = 0; i < msw_ids->len; i++) {
        printf("%d: 0x%" PRIX64 "\n", i, g_array_index(msw_ids, uint64_t, i));
    }

    return 0;
}

int adisc_cmd(int argc, char **argv, void *zsocket)
{
    tbg_msg_t resp;
    uint8_t msgdata[8];
    int ret;
    int boards_found = 0;
    int addr = 1;

    msgdata[0] = TBG_ADISC_BIT_ASSIG_ADDR | TBG_ADISC_BIT_CLR_SHORTLIST;
    msgdata[1] = 63; // Set all addresses to a temporary value & clear shortlist
    send_request(zsocket, TBG_ADDR_BROADCAST, TBG_PORT_ADISC, msgdata, 2, NULL);

    while (1) {

        // Get MSW of ID
        adis_data_set(msgdata, TBG_ADISC_BIT_RETURN_ID | TBG_ADISC_BIT_RETURN_ID_MSW, 0, 0);
        ret = send_request(zsocket, 63, TBG_PORT_ADISC, msgdata, 2, &resp);
        if (!ret) {
            // If we get a timeout, assume we've finished assigning addresses.
            // TODO: double-check this
            break;
        }
        // Grab id from first response
        uint64_t id_msw = adis_get_id(&resp);

        // Flush other responses
        flush_responses(zsocket, 20);

        // Match MSW, set shortlist flag, get LSW
        adis_data_set(msgdata, TBG_ADISC_BIT_MATCH_ID | TBG_ADISC_BIT_MATCH_ID_MSW | TBG_ADISC_BIT_RETURN_ID | TBG_ADISC_BIT_SET_SHORTLIST, 0, id_msw);
        ret = send_request(zsocket, 63, TBG_PORT_ADISC, msgdata, 8, &resp);
        if (!ret) {
            WARNING("Timeout at step 2 (get LSW)\n");
            return 1;
        }
        uint64_t id_lsw = adis_get_id(&resp);

        // Flush other responses
        flush_responses(zsocket, 20);

        // Match LSW, match & clear shortlist flag, set address. We also request the ID again in order to generate a response.
        adis_data_set(msgdata, TBG_ADISC_BIT_MATCH_ID | TBG_ADISC_BIT_MATCH_SHORTLIST | TBG_ADISC_BIT_ASSIG_ADDR | TBG_ADISC_BIT_RETURN_ID | TBG_ADISC_BIT_CLR_SHORTLIST, addr++, id_lsw);
        ret = send_request(zsocket, 63, TBG_PORT_ADISC, msgdata, 8, &resp);
        uint8_t assigned_addr = resp.data[6];
        if (!ret) {
            WARNING("Timeout at step 3 (set address)\n");
            return 1;
        } else {
            if (boards_found == 0) {
                printf("[\n");
            }
            char s[256];
            ret = get_conf_string(zsocket, assigned_addr, TBG_CONF_GLOBAL_PRODUCT_ID_STR, 0, s);
            if (!ret) {
                WARNING("Timeout getting board info\n");
                return 1;
            }
            if (boards_found) {
                printf(",\n");
            }
            printf("    { \"addr\":%d, \"id\":\"0x%012" PRIX64 "%012" PRIX64 "\", \"product\":%s }", assigned_addr, id_msw, id_lsw, s);
            boards_found++;
        }
    }
    if (boards_found) {
        printf("\n]\n");
    }

    return (boards_found) ? 0 : 1;
}

void adisc_usage(char *name)
{
    printf("usage: %s %s\n", progname, name);
}

int getstr_cmd(int argc, char **argv, void *zsocket)
{
    char s[256];
    s[0] = '\0';
    uint8_t conf_port = 0;
    int argptr = 1;
    uint8_t addr = strtol(argv[argptr++], NULL, 0);
    uint8_t conf_cmd = strtol(argv[argptr++], NULL, 0) & TBG_CONF_BITS_CMD;
    if (argc > 3) {
        conf_port = strtol(argv[argptr++], NULL, 0);
    }
    if (conf_cmd == TBG_PORTCONF_CMD_COM_GET_CONF_DESCR) {
        if (argc <= 4) {
            printf("usage: %s getstr addr 2 port conf_num\n", progname);
            return 1;
        }
        uint8_t conf_num = strtol(argv[argptr++], NULL, 0);
        get_conf_descr(zsocket, addr, conf_num, conf_port, s);
    } else {
        if (argc > 3) {
            get_conf_string(zsocket, addr, conf_cmd | TBG_CONF_BIT_PORT, conf_port, s);
        } else {
            get_conf_string(zsocket, addr, conf_cmd, 0, s);
        }
    }

    if (s[0]) {
        printf("%s\n", s);
    }
    return 0;
}

void getstr_usage(char *name)
{
    printf("usage: %s %s addr conf [port]\n", progname, name);
    printf("usage: %s %s addr 2 port conf_num\n", progname, name);
}

int info_cmd(int argc, char **argv, void *zsocket)
{
    uint8_t msgdata[8];
    tbg_msg_t resp;
    char s[256];
    s[0] = '\0';
    int ret;

    uint8_t addr = strtol(argv[1], NULL, 0);

    adis_data_set(msgdata, TBG_ADISC_BIT_RETURN_ID | TBG_ADISC_BIT_RETURN_ID_MSW, 0, 0);
    ret = send_request(zsocket, addr, TBG_PORT_ADISC, msgdata, 2, &resp);
    if (!ret) { printf("Timeout\n"); return 1; }
    uint64_t id_msw = adis_get_id(&resp);

    adis_data_set(msgdata, TBG_ADISC_BIT_RETURN_ID, 0, 0);
    ret = send_request(zsocket, addr, TBG_PORT_ADISC, msgdata, 2, &resp);
    if (!ret) { printf("Timeout\n"); return 1; }
    uint64_t id_lsw = adis_get_id(&resp);

    ret = get_conf_string(zsocket, addr, TBG_CONF_GLOBAL_PRODUCT_ID_STR, 0, s);
    if (!ret) { printf("Timeout\n"); return 1; }

    printf("{ \"addr\":%d, \"id\":\"0x%012" PRIX64 "%012" PRIX64 "\", \"product\":%s }\n", addr, id_msw, id_lsw, s);
    return 0;
}

void info_usage(char *name)
{
    printf("usage: %s %s addr\n", progname, name);
}


typedef int (inv_fn_t)(int argc, char **argv, void *zsocket);
typedef void (usage_fn_t)(char *name);

typedef struct command_s {
    const char *name;
    inv_fn_t *fn;
    int min_args;
    usage_fn_t *usage;
} command_t;

const command_t commands[] = {
    {  "dout", dout_cmd, 2, dout_usage },
    {  "dout2", dout_cmd2, 2, dout_cmd2_usage },
    {  "din", din_cmd, 2, din_usage },
    {  "aout", aout_cmd, 2, aout_usage },
    {  "ain", ain_cmd, 2, ain_usage },
    {  "tbg", tbg_cmd, 2, tbg_usage },
    {  "adisc", adisc_cmd, 0, adisc_usage },
    {  "adisc2", adisc2_cmd, 0, adisc_usage },
    {  "getstr", getstr_cmd, 2, getstr_usage },
    {  "info", info_cmd, 1, info_usage },
    {  "uniq_test", uniq_test, 0, NULL },
};

#define N_COMMANDS (sizeof(commands)/sizeof(command_t))

char *server_addr = "tcp://localhost:5555";
int iflag = 0;

static GOptionEntry cmd_line_options[] = {
    { "server",      's', 0, G_OPTION_ARG_STRING, &server_addr, "Set server address to A (e.g. tcp://localhost:5555)", "A" },
    { "debug-level", 'd', 0, G_OPTION_ARG_INT,    &debug_level, "Set debug level to d", "d" },
    { "interactive", 'i', 0, G_OPTION_ARG_NONE,   &iflag, "Interactive mode - reads commands from stdin", NULL },
    { NULL }
};

/*
 * Pull an argv out of argv and shift it down, preserving argv[0].
 */
char *shift_args(int *argc, char **argv)
{
    if (*argc > 1) {
        char *ret = argv[1];
        int i;
        for (i = 1; i < (*argc - 1); i++) {
            argv[i] = argv[i+1];
        }
        *argc = i;
        return ret;
    } else {
        return NULL;
    }
}


int run_command(int argc, char **argv, void *zsocket)
{
    int ret = 1;

    char *cmd = shift_args(&argc, argv);
    if (!cmd) {
        ERROR("usage: %s command [args]\n", argv[0]);
    }

    int i;
    for (i = 0; i < N_COMMANDS; i++) {
        const command_t *inv = commands + i;
        if (strcmp(cmd, inv->name) == 0) {
            if (argc <= inv->min_args) {
                inv->usage(cmd);
            } else {
                ret = inv->fn(argc, argv, zsocket);
            }
            break;
        }
    }
    if (i == N_COMMANDS) {
        ERROR("Incorrect command \"%s\".", cmd);
    }
    return ret;
}

/*
 * Split-out whitespace-separated tokens from a string.
 * Unlike g_strplit, etc, we treat multiple contiguous
 * whitespace characters as a single separator.
 */
GPtrArray *strsplit(char *str)
{
    GPtrArray *args = g_ptr_array_new();
    char *stringp = str;
    char *arg;
    arg = strsep(&stringp, " \t"); 
    while (arg) {
        if (*arg != '\0') {
            g_ptr_array_add(args, arg);
        }
        arg = strsep(&stringp, " \t"); 
    }
    // Add sentinel value
    g_ptr_array_add(args, NULL);
    return args;
}

/*
 * Read lines from stdin and interpret them in the same way as
 * the commnd line parameters for the same function. This
 * allows 'interactive' operation.
 */
int interactive(void *zsocket)
{
    char *linebuf = NULL;
    size_t linebufsize = 0;
    int ret;

    ret = getline(&linebuf, &linebufsize, stdin);
    while (ret >= 0) {
        // Add a substitute for the normal argv[0] program name
        char *cmdline = g_strconcat("<stdin> ", linebuf, NULL);
        //char **line_argv = g_strsplit_set(cmdline, " \t", -1);
        GPtrArray *args = strsplit(cmdline);
        run_command(args->len, (char **)args->pdata, zsocket);
        g_ptr_array_free(args, FALSE);
        g_free(cmdline);
        // Get next line
        ret = getline(&linebuf, &linebufsize, stdin);
    }
    return 0;
}

int main (int argc, char **argv)
{
    int ret;
    progname = argv[0]; // This is used by ael-debug.h


    GError *error = NULL;
    GOptionContext *context = g_option_context_new("- Touchbridge Command Line Client");
    g_option_context_add_main_entries (context, cmd_line_options, NULL);
    if (!g_option_context_parse (context, &argc, &argv, &error)) {
        ERROR("option parsing failed: %s\n", error->message);
    }

    void *zcontext = zmq_ctx_new ();
    void *zsocket = zmq_socket (zcontext, ZMQ_DEALER);
    SYSERROR_IF(zsocket == NULL, "zmq_socket");
    ret = zmq_connect (zsocket, server_addr);
    SYSERROR_IF(ret != 0, "zmq_connect: %s", server_addr);

    if (iflag) {
        ret = interactive(zsocket);
    } else {
        ret = run_command(argc, argv, zsocket);
    }

    usleep(10000);
    zmq_close(zsocket);
    return ret;
}
