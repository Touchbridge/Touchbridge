/*
 *
 * tbg_server.c
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
 * Example for using zmq router.
 *
 * J.Macfarlane 2015-08-15
 */

#include <glib.h>
#include <zmq.h>
#include <czmq.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <poll.h>
#include "debug.h"

#include "rpi_io.h"
#include "tbg_rpi.h"
#include "tbg_util.h"

int debug_level = 0;

int src_addr = 62;

tbgrpi_t *tpi;

GHashTable *clients;
int peer_count;

typedef struct ztbg_client_s {
    unsigned char *zmq_id;
    int zmq_id_len;
    uint16_t tbg_addr;
} ztbg_client_t;

void dumpbuf(unsigned char *buf, int size)
{
    for (int i = 0; i < size; i++) {
        printf(" 0x%02X", (unsigned char)buf[i]);
    }
}

int get_zmq_events(void *zsocket)
{
    int ret;
    int events = 0;
    size_t events_size = sizeof(events);

    ret = zmq_getsockopt(zsocket, ZMQ_EVENTS, &events, &events_size);
    SYSERROR_IF (ret != 0, "zmq_setsockopt");
    return events;
}

void ztbg_send_all(void *zsocket, GHashTable *clients, char *payload)
{
    GHashTableIter iter;
    gpointer key, value;
    g_hash_table_iter_init (&iter, clients);
    int i;
    for (i = 0; g_hash_table_iter_next(&iter, &key, &value); i++) {
        //zmsg_t *resp = zmsg_new();
        ztbg_client_t *cli = value;
        //zframe_t *id_frame = zframe_new(cli->zmq_id, cli->zmq_id_len);
        //zmsg_append(resp, &id_frame);
        //zframe_t *payload_frame = zframe_dup(payload);
        //zmsg_append(resp, &payload_frame);
        //zmsg_send (&resp, zsocket);

        //dumpbuf(cli->zmq_id, cli->zmq_id_len);
        //printf("\n");
        // Multi-part message, ad peer address ID.
        int ret;
        ret = zmq_send (zsocket, cli->zmq_id, cli->zmq_id_len, ZMQ_SNDMORE);
        if (ret < 0) {
            if (errno == EHOSTUNREACH) {
                if (debug_level >= 1) {
                    printf("Client %s left.\n", (char *)key);
                }
                g_hash_table_iter_remove(&iter);
            } else {
                printf("zmq_send: %s\n", zmq_strerror(errno));
            }
        } else {
            //ret = zmq_send (zsocket, "", 0, ZMQ_SNDMORE);
            //SYSERROR_IF (ret < 0, "zmq_send(delimiter)");
            int ret2 = zmq_send (zsocket, payload, strlen(payload), 0);
            if (debug_level >= 3) {
                printf("    Sent %d:%d bytes to %s\n", ret, ret2, (char *)key);
            }
            SYSERROR_IF (ret2 < 0, "zmq_send(payload)");
        }
    }
}

int do_tbg_msg_recv(void *zsocket)
{
    tbg_msg_t resp;
    char resp_str[TBG_MSG_SIZE*2+1];

    int stat = tbgrpi_read_status(tpi);
    while (stat & TBGRPI_STAT_RX_DATA_AVAIL) {
        tbgrpi_recv_msg(tpi, &resp);
        if (debug_level >= 2) {
            printf("TBG rx: ");
            tbg_msg_dump(&resp);
        }
        tbg_msg_to_hex(&resp, resp_str);
        ztbg_send_all(zsocket, clients, resp_str);
        stat = tbgrpi_read_status(tpi);
    }
    return 0;
}

int src_port = 0;

int do_zmq_msg_recv(void *zsocket)
{
    zmsg_t *msg  = zmsg_recv (zsocket);
    if (!msg) return -1;
    zframe_t *peer = zmsg_pop (msg);
    zframe_t *payload = zmsg_pop (msg);
    tbg_msg_t req;

    // Convert peer ID into a hexadecimal string. ZMQ docs are
    // very coy about how peer IDs are represented internally
    // so we seem to need to use this technique instead of
    // using binary keys. Furthermore, a binary key might make
    // it very difficult to use the GHashtable API.
    char *client_str = zframe_strhex (peer);

    // Grab a copy of the payload string
    char *payload_str = zframe_strdup (payload);

    if (debug_level >= 3) {
        printf ("From %s: '%s' \n", client_str, payload_str);
    }

    // Check if client is already in our list.
    // If not, create a new entry.
    // TODO: use this to generate sender's port address.
    if (!g_hash_table_contains(clients, client_str)) {
        ztbg_client_t *client = g_new(ztbg_client_t, 1);
        client->zmq_id_len = zframe_size(peer);
        client->zmq_id = g_memdup(zframe_data(peer), client->zmq_id_len);
        g_hash_table_insert(clients, client_str, client);

        if (debug_level >= 1) {
            printf("Client %s joined.\n", client_str);
        }
    } else {
        free(client_str);
    }

    // Convert ASCII hex payload into a Touchbridge message.
    tbg_msg_from_hex(&req, payload_str);
    free(payload_str);

    TBG_MSG_SET_SRC_PORT(&req, src_port++);
    TBG_MSG_SET_SRC_ADDR(&req, src_addr);

    if (src_port > 63) {
        src_port = 0;
    }

    // Send it.
    int stat = tbgrpi_read_status(tpi);
    while (!(stat & TBGRPI_STAT_TX_BUF_EMPTY)) {
        stat = tbgrpi_read_status(tpi);
    }
    if (debug_level >= 2) {
        printf("TBG tx: ");
        tbg_msg_dump(&req);
    }
    tbgrpi_send_msg(tpi, &req);

/*
    zmsg_t *resp = zmsg_new();
    zmsg_append(resp, &peer);
    zframe_t *words = zframe_from("World 1");
    zmsg_append(resp, &words);
    zmsg_send (&resp, zsocket);
*/

    return 0;
}

/*
 * We must check for any ZMQ_POLLIN events still remaining
 * after we've serviced first one or else we only ever receive
 * one message as ZMQ's fd doesn't get "reset" and we never
 * get another select/GIOC receive event.
 */
unsigned int do_zmq_recv_events(void *zsocket)
{
    int events;

    events = get_zmq_events(zsocket);
    while (events & ZMQ_POLLIN) {
        do_zmq_msg_recv(zsocket);
        events = get_zmq_events(zsocket);
    }
    return events;
}

char *progname;

char *server_addr = "tcp://*:5555";

static GOptionEntry cmd_line_options[] = {
    { "server",      's', 0, G_OPTION_ARG_STRING, &server_addr, "Set server address to S (e.g. tcp://*:5555)", "S" },
    { "tbg-address", 'a', 0, G_OPTION_ARG_INT,    &src_addr, "Set server's Touchbridge address to A, (range 0-63)", "A" },
    { "debug-level", 'd', 0, G_OPTION_ARG_INT,    &debug_level, "Set debug level to d", "d" },
    { NULL }
};

int main(int argc, char **argv)
{
    int ret;
    progname = argv[0];
    int optdata;

    GError *error = NULL;
    GOptionContext *opt_context = g_option_context_new("- Touchbridge Server");
    g_option_context_add_main_entries (opt_context, cmd_line_options, NULL);
    if (!g_option_context_parse (opt_context, &argc, &argv, &error)) {
        ERROR("option parsing failed: %s\n", error->message);
    }

    if (src_addr < 0) {
        src_addr = 0;
    }
    if (src_addr > 63) {
        src_addr = 63;
    }

    tpi = tbgrpi_open();

    tbgrpi_init_io(tpi);

    // Read status to reset the /INT line if we start with it asserted
    // (which seems to be fairly often)
    int initial_status =  tbgrpi_read_status(tpi);

    if (debug_level >= 2) {
        printf("TBG_HAT initial status: 0x%02X\n", initial_status);
    }

    // Enable ints
    tbgrpi_write_config(tpi, TBGRPI_CONF_RX_DATA_AVAIL_IE | TBGRPI_CONF_RX_OVERFLOW_RESET );

    clients = g_hash_table_new(g_str_hash, g_str_equal);

    int intfd = rpi_io_interrupt_open(TBGRPI_PIN_INT, RPI_IO_EDGE_FALLING);
    rpi_io_interrupt_flush(intfd);

    //  Socket to talk to clients
    void *context = zmq_ctx_new ();
    void *zsocket = zmq_socket (context, ZMQ_ROUTER);
    SYSERROR_IF (zsocket == NULL, "zmq_socket");
    optdata= 1;
    ret = zmq_setsockopt (zsocket, ZMQ_ROUTER_MANDATORY, &optdata, sizeof(optdata));
    SYSERROR_IF (ret != 0, "zmq_setsockopt");
    ret = zmq_bind (zsocket, server_addr);
    SYSERROR_IF (ret != 0, "zmq_bind");

    // Get ZMQ fd.
    int zmqfd;
    size_t zmqfd_size = sizeof(zmqfd);
    ret = zmq_getsockopt(zsocket, ZMQ_FD, &zmqfd, &zmqfd_size);
    SYSERROR_IF(ret < 0, "zmq_getsockopt");

    while (1) {
        // Can't use zmq_poll because it doesn't support POLLPRI
        struct pollfd items[] = {
            { .fd = zmqfd, .events = POLLIN  },
            { .fd = intfd, .events = POLLPRI },
         };
        // We need to clear these each time around the loop.
        items[0].revents = 0;
        items[1].revents = 0;
        ret = poll (items, 2, 2000);
        SYSERROR_IF(ret < 0, "poll");
        if (items[0].revents & POLLIN) {
            do_zmq_recv_events(zsocket);
        } else if (items[1].revents & POLLPRI) {
            rpi_io_interrupt_clear(intfd);
            do_tbg_msg_recv(zsocket);
            /* We need to check for events here because:
             * "...after calling 'zmq_send' socket may become readable (and
             * vice versa) without triggering read event on file descriptor."
             * -https://github.com/zeromq/libzmq/pull/328/files 
             * This is fucked-up. But the benefits of zmq outweighs such
             * inconvenience.
             */
            do_zmq_recv_events(zsocket);
        } else {
            if (debug_level >= 3) {
                int events = get_zmq_events(zsocket);
                printf("Waiting. ZMQ Events: %s %s\n", (events & ZMQ_POLLIN) ? "IN" : "", (events & ZMQ_POLLOUT) ? "OUT" : "");
            }
        }
    }
    return 0;
}

