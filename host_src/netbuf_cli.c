
#include <stdio.h>
#include <stdint.h>
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

int stdin_callback(void *cb_data, int fd, short revents)
{
    char buf[1024];
    int ret = read(fd, buf, 1024);
    if (ret < 0) SYSERROR("read");
    write(sock, buf, ret);
    printf("stdin: %s\n", buf);
    return 0;
}

int main(int argc, char **argv)
{
	sock = netcon_sock_connect("127.0.0.1", 5555);

    if (sock < 0) {
        return -1;
    }

    netcon_t *nc = netcon_new();

    netcon_add_fd(nc, STDIN_FILENO, POLLIN, stdin_callback, NULL);

    netcon_main_loop(nc);

    netcon_free(nc);
    
    return 0;
}

