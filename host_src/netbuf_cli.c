
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
    fwrite(buf, ret, 1, stdout);
    return 0;
}

int main(int argc, char **argv)
{
	sock = netcon_sock_connect("127.0.0.1", 5555);
    netbuf_t *nb = netbuf_new(sock, &tlv1_codec);

    if (sock < 0) {
        return -1;
    }

    netcon_t *nc = netcon_new();

    netcon_add_fd(nc, STDIN_FILENO, POLLIN, stdin_callback, nb);

    netcon_main_loop(nc);

    netcon_free(nc);
    netbuf_free(nb);
    
    return 0;
}

