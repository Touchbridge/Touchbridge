
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <poll.h>
#include <sys/socket.h>

#include <glib.h>

#include "debug.h"
#include "netbuf.h"
#include "netcon.h"

int sock;

int stdin_callback(netcon_t *nc, void *cb_data, int fd, short revents)
{
    netbuf_t *nb = cb_data;

    char buf[1025];
    int ret = read(fd, buf, 1024);
    if (ret < 0) SYSERROR("read");
    if (ret == 0) {
        printf("End of File, disconnecting\n");
        close(sock);
        exit(0);
    }
    netbuf_add_msg(nb, 1, (uint8_t *)buf, ret);
    netbuf_add_msg(nb, 2, (uint8_t *)"Foo\n", 4);
    netbuf_send(nb);
    return 0;
}

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
    while (netbuf_decode(nb, &buf) > 0) {
        printf("Got %d byte TVL message of type %d from client on fd %d\n", NETBUF_GET_LENGTH(nb), NETBUF_GET_TYPE(nb), fd);
        if (NETBUF_GET_TYPE(nb) == 1) {
            fwrite(NETBUF_GET_DATA(nb), NETBUF_GET_LENGTH(nb), 1, stdout);
            fflush(stdout);
        }
    }
    return 0;
}

int main(int argc, char **argv)
{
	sock = netcon_sock_connect("127.0.0.1", 5555);

    if (sock < 0) {
        return -1;
    }

    netbuf_t *nb = netbuf_new(sock, &tlv1_codec);

    netcon_t *nc = netcon_new();

    netcon_add_fd(nc, STDIN_FILENO, POLLIN, stdin_callback, nb);
    netcon_add_fd(nc, sock, POLLIN, read_callback, nb);

    netcon_main_loop(nc, -1);

    netcon_free(nc);
    netbuf_free(nb);
    netcon_sock_close(sock);
    
    return 0;
}

