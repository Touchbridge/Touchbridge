
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <poll.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <glib.h>

#include "debug.h"
#include "netbuf.h"
#include "netcon.h"

void echo(netbuf_cli_t *cli, void *data)
{
    netbuf_add_msg(cli->nb, 1, data, strlen(data));
}

void nb_read_cb(netbuf_cli_t *cli, int type, int len, uint8_t *data)
{
    //int fd = (int)cli->data;
    printf("Got %d byte TVL message of type %d\n", len, type);
    if (type == 1) {
        fwrite(data, len, 1, stdout);
        printf("\n");
        fflush(stdout);
        netbuf_srv_forall(cli->srv, echo, data);
    }
}

int main(int argc, char **argv)
{
	int sock = netcon_sock_listen("127.0.0.1", 5555);

    netcon_t *nc = netcon_new();

    // (accept_cb, read_cb, close_cb, cb_data)
    netbuf_srv_t *srv = netbuf_srv_new(NULL, nb_read_cb, NULL, NULL);

    netcon_add_netbuf_srv_fd(nc, sock, srv);

    netcon_main_loop(nc, -1);

    netcon_free(nc);
    netbuf_srv_free(srv);
    netcon_sock_close(sock);
    
    return 0;
}
