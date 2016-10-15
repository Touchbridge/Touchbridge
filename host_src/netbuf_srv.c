
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <poll.h>
#include <sys/socket.h>

#include <glib.h>

#include "debug.h"
#include "netbuf.h"
#include "netcon.h"

int read_callback(void *cb_data, int fd, short revents)
{
    netcon_t *nc = cb_data;
    unsigned char buf[1024];
    int ret = read(fd, buf, 1024);
    
    if (ret < 0) {
    	SYSERROR("%s: read from %d", __FUNCTION__, fd);
    }
    if (ret == 0) {
        printf("Client on fd %d disconnected\n", fd);
        netcon_remove_fd(nc, fd);
    } else {
        printf("Got %d bytes of data from client on fd %d\n", ret, fd);
    }
    return 0;
}

int accept_callback(void *cb_data, int fd, short revents)
{
    netcon_t *nc = cb_data;

    int ret = netcon_accept_fd(nc, fd, POLLIN, read_callback, nc);
    if (ret < 0) {
        exit(-1);
    }
    printf("Got new client on fd %d\n", ret);
    return 0;
}

int main(int argc, char **argv)
{
	int sock = netcon_sock_listen("127.0.0.1", 5555);

    netcon_t *nc = netcon_new();

    netcon_add_fd(nc, sock, POLLIN, accept_callback, nc);

    netcon_main_loop(nc);

    netcon_free(nc);
    
    return 0;
}
