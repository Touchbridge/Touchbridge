
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

int read_callback(netcon_t *nc, void *cb_data, int fd, short revents)
{
    netbuf_t *nb = cb_data;
    unsigned char recv_buf[1024];
    buf_t buf;

    buf_init_from_static(&buf, recv_buf, 1024);

    int ret = buf_recv(fd, &buf, 0);
    
    if (ret < 0) {
    	SYSERROR("%s: read from %d", __FUNCTION__, fd);
    }
    if (ret == 0) {
        printf("Client on fd %d disconnected\n", fd);
        netcon_remove_fd(nc, fd);
        netbuf_free(nb);
    } else {
        printf("Got %d bytes of data from client on fd %d\n", ret, fd);
        while (netbuf_decode(nb, &buf) > 0) {
            printf("Got %d byte TVL message of type %d from client on fd %d\n", NETBUF_GET_LENGTH(nb), NETBUF_GET_TYPE(nb), fd);
            if (NETBUF_GET_TYPE(nb) == 1) {
                fwrite(NETBUF_GET_DATA(nb), NETBUF_GET_LENGTH(nb), 1, stdout);
            }
        }
    }
    return 0;
}

int accept_callback(netcon_t *nc, void *cb_data, int fd, short revents)
{
    // Get a new netbuf and give it a fake socket number as we
    // don't know it until after accept.
    netbuf_t *nb = netbuf_new(-1, &tlv1_codec);
    netcon_addr_t addr;
    int ret = netcon_accept_fd(nc, fd, POLLIN, read_callback, nb, &addr);
    if (ret < 0) {
        exit(-1);
    }
    // Set the socket in the netbuf
    nb->socket = ret;
    char *addrstr = netcon_addr_to_str(&addr);
    printf("Got new client on fd %d from %s:%d\n", ret, addrstr, netcon_addr_port(&addr));
    g_free(addrstr);
    return 0;
}

int main(int argc, char **argv)
{
	int sock = netcon_sock_listen("127.0.0.1", 5555);

    netcon_t *nc = netcon_new();

    netcon_add_fd(nc, sock, POLLIN, accept_callback, NULL);

    netcon_main_loop(nc);

    netcon_free(nc);
    
    return 0;
}
