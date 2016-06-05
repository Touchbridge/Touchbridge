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

#include "clients.h"
#include "server_socket.h"

int debug_level = 0;

int src_addr = 62;

tbgrpi_t *tpi;

int accept_socket_on_listener(int sock);

void dumpbuf(unsigned char *buf, int size)
{
    for (int i = 0; i < size; i++) {
        printf(" 0x%02X", (unsigned char)buf[i]);
    }
}

int send_to_client(int fd, void *cb_data)
{
    tbg_msg_t *msg = cb_data;

    // Make a TLV message
    uint8_t buf[sizeof(tbg_msg_t) + 2]; // FIXME: make a #define for header len
    buf[0] = 1; // FIXME: make a #define for packet type
    buf[1] = sizeof(tbg_msg_t);
    memcpy(buf + 2, msg, sizeof(tbg_msg_t));

    // send it
    send(fd, buf, sizeof(tbg_msg_t) + 2, 0);
    // FIXME: check exit status of send.

    return 0;
}

int do_tbg_msg_recv(void)
{
    tbg_msg_t resp;

    int stat = tbgrpi_read_status(tpi);
    while (stat & TBGRPI_STAT_RX_DATA_AVAIL) {
        tbgrpi_recv_msg(tpi, &resp);
        if (debug_level >= 2) {
            printf("TBG rx: ");
            tbg_msg_dump(&resp);
        }
        for_all_clients(send_to_client, &resp);
        stat = tbgrpi_read_status(tpi);
    }

    return 0;
}

int src_port = 0;

int do_client_receive(unsigned char *data, int length, void *cb_data)
{
    tbg_msg_t *req = (void *)data;
    // FIXME: check length is valid

    TBG_MSG_SET_SRC_PORT(req, src_port++);
    TBG_MSG_SET_SRC_ADDR(req, src_addr);

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
        tbg_msg_dump(req);
    }
    tbgrpi_send_msg(tpi, req);

    return 0;
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
    progname = argv[0];

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

    int intfd = rpi_io_interrupt_open(TBGRPI_PIN_INT, RPI_IO_EDGE_FALLING);
    rpi_io_interrupt_flush(intfd);

	int pollResult;
	int sock = make_listening_socket("127.0.0.1", 5555);
	struct pollfd constant_fds[] = {
		{ .fd = sock,  .events = POLLIN,  .revents = 0 },
        { .fd = intfd, .events = POLLPRI, .revents = 0 },
	};
    int num_const_fds = sizeof(constant_fds)/sizeof(struct pollfd);

	struct pollfd *items;

	while(1) {
        int num_items;
		items = create_client_pollfds_array(constant_fds, num_const_fds, &num_items);
		items[0].revents = 0; // FIXME: do we need this?
		
		pollResult = poll(items, num_items, 2000);
		
		if (pollResult < 0) {
			// something's gone a bit awry.
			SYSERROR("poll");
		} else if (pollResult > 0) {

            // New client connected
			if (items[0].revents != 0) {
				accept_socket_on_listener(sock);
			}
			
            // Touchbridge HAT Interrupt
            if (items[1].revents & POLLPRI) {
                rpi_io_interrupt_clear(intfd);
                do_tbg_msg_recv();
            }

            // Handle any data from clients
			handle_client_reads_by_pollfds(items, 1);
		}
		
		free_client_pollfds_array(items);
    }
    return 0;
}

int accept_socket_on_listener(int sock)
{
	struct sockaddr addr;
	socklen_t len;
	int client_sock;
	client_sock = accept(sock, &addr, &len);
	
	add_client_by_descriptor(client_sock, do_client_receive, NULL);
	return client_sock;
}
