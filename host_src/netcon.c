
#define _BSD_SOURCE // for inet_aton(3)

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <poll.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "netcon.h"

#define CONNECTION_BACKLOG (8)

int netcon_sock_connect(char *net_addr, unsigned short port)
{
    // Create the socket
    int sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
    	fprintf(stderr, "%s: socket: %s\n", __FUNCTION__, strerror(errno));
    	return -1;
    }
    
    // Parse the address
    struct in_addr addr;
    if (inet_aton(net_addr, &addr) == 0) {
    	fprintf(stderr, "%s: %s: could not parse IP address\n", __FUNCTION__, net_addr);
        close(sock);
    	return -1;
    }
    
    // set up the sockaddr struct
    struct sockaddr_in addrAndPort;
    addrAndPort.sin_family = AF_INET;
    addrAndPort.sin_port = htons(port);
    addrAndPort.sin_addr = addr;
    
    if (connect(sock, (struct sockaddr *)&addrAndPort, sizeof(addrAndPort)) < 0) {
    	fprintf(stderr, "%s: %s: connect: %s\n", __FUNCTION__, net_addr, strerror(errno));
        close(sock);
    	return -1;
    }
    
    return sock;
}


int netcon_sock_listen(char *net_addr, unsigned short port)
{
    // Create the socket
    int sock = socket(PF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
    	fprintf(stderr, "%s: socket: %s\n", __FUNCTION__, strerror(errno));
    	return -1;
    }
    
    // Set SO_REUSEADDR
    int optval = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, 
    	(const void *)&optval , sizeof(int));

    // parse the IP
    struct in_addr addr;
    if (inet_aton(net_addr, &addr) == 0) {
    	fprintf(stderr, "%s: %s: could not parse IP address\n", __FUNCTION__, net_addr);
        close(sock);
    	return -1;
    }

    // set up the sockaddr struct
    struct sockaddr_in addrAndPort;
    addrAndPort.sin_family = AF_INET;
    addrAndPort.sin_port = htons(port);
    addrAndPort.sin_addr = addr;
    
    if (bind(sock, (struct sockaddr *)&addrAndPort, sizeof(addrAndPort)) < 0) {
    	fprintf(stderr, "%s: %s: bind: %s\n", __FUNCTION__, net_addr, strerror(errno));
        close(sock);
    	return -1;
    }
    
    if (listen(sock, CONNECTION_BACKLOG) < 0) {
    	fprintf(stderr, "%s: %s: listen: %s\n", __FUNCTION__, net_addr, strerror(errno));
        close(sock);
    	return -1;
    }
    
    return sock;
}

netcon_t *netcon_new(void)
{
    netcon_t *nc = g_new(netcon_t, 1);
    nc->pollfds = g_array_new(FALSE, FALSE, sizeof(struct pollfd));
    nc->netcon_fds = g_array_new(FALSE, FALSE, sizeof(netcon_fd_t));
    return nc;
}

void netcon_add_fd(netcon_t *nc, int fd, short events, netcon_fn_t *cb, void *cb_data)
{
    struct pollfd pfd = { .fd = fd, .events = events, .revents = 0 };
    netcon_fd_t nfd = { .callback = cb, .data = cb_data };
    g_array_append_val(nc->pollfds, pfd);
    g_array_append_val(nc->netcon_fds, nfd);
}

int netcon_accept_fd(netcon_t *nc, int fd, short events, netcon_fn_t *cb, void *cb_data, netcon_addr_t *addr)
{
    netcon_fd_t nfd = { .callback = cb, .data = cb_data };
    struct sockaddr sock_addr;
    socklen_t addrlen = sizeof(sock_addr);
    int ret = accept(fd, &sock_addr, &addrlen);
    if (ret < 0) {
        fprintf(stderr, "%s: accept: %s\n", __FUNCTION__, strerror(errno));
        return -1;
    }
    struct pollfd pfd = { .fd = ret, .events = events, .revents = 0 };
    g_array_append_val(nc->pollfds, pfd);
    g_array_append_val(nc->netcon_fds, nfd);
    if (addr) {
        addr->addr = sock_addr;
        addr->addr_len = addrlen;
    }
    return ret;
}


void netcon_remove_fd(netcon_t *nc, int fd)
{
    for (int i = 0; i < nc->pollfds->len; i++) {
        struct pollfd *pfd = &g_array_index(nc->pollfds, struct pollfd, i);
        if (pfd->fd == fd) {
            g_array_remove_index(nc->pollfds, i);
            g_array_remove_index(nc->netcon_fds, i);
            break;
        }
    }
}

int netcon_poll(netcon_t *nc, int timeout)
{
    int ret = poll((struct pollfd *)nc->pollfds->data, nc->pollfds->len, timeout);
    return ret;
}

int netcon_main_loop(netcon_t *nc)
{
    while (1) {
        int ret = netcon_poll(nc, -1); // TODO: add periodic callback capability
        if (ret < 0) {
            fprintf(stderr, "%s: poll: %s\n", __FUNCTION__, strerror(errno));
            return -1;
        }

        if (ret > 0) {
            // We need to copy current number of fds as if we accept and
            // add a new connection, we'll end up iterating over it even
            // though it hasn't been affected by the call to poll().
            int num_fds = nc->pollfds->len;
            for (int i = 0; i < num_fds; i++) {
                struct pollfd *pfd = &g_array_index(nc->pollfds, struct pollfd, i);
                netcon_fd_t *nfd = &g_array_index(nc->netcon_fds, netcon_fd_t, i);
                if (pfd->revents > 0) {
                    if (nfd->callback) {
                        nfd->callback(nc, nfd->data, pfd->fd, pfd->revents);
                    }
                    pfd->revents = 0;
                }
            }
        }
    }
}

void netcon_free(netcon_t *nc)
{
    g_array_free(nc->pollfds, TRUE);
    g_array_free(nc->netcon_fds, TRUE);
    free(nc);
}

char *netcon_addr_to_str(netcon_addr_t *a)
{
    int len = INET_ADDRSTRLEN;
    char *addrstr = g_new(char, len);
    struct sockaddr_in *in_addr = (struct sockaddr_in *)&a->addr;
    const char *ret = inet_ntop(AF_INET, &in_addr->sin_addr, addrstr, len);
    if (ret == NULL) {
    	fprintf(stderr, "%s: inet_ntop: %s\n", __FUNCTION__, strerror(errno));
        return NULL;
    }
    return  addrstr;
}

unsigned short netcon_addr_port(netcon_addr_t *a)
{
    struct sockaddr_in *in_addr = (struct sockaddr_in *)&a->addr;
    return ntohs(in_addr->sin_port);
}
