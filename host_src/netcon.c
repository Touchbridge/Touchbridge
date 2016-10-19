
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

/*
 * This is a tx_enable callback for netcon_t clients.
 */
void netcon_tx_enable_cb(netbuf_t *nb, void *data)
{
    struct pollfd *pfd = data;
    pfd->events |= POLLOUT;
}

/*
 * This is a tx_disable callback for netcon_t clients.
 */
void netcon_tx_disable_cb(netbuf_t *nb, void *data)
{
    struct pollfd *pfd = data;
    pfd->events &= ~POLLOUT;
}

#define NETBUF_SRV_RX_SIZE      (4096)

/*
 * This is the callback fn for poll events on netbuf clients.
 * Internal use only.
 */
static int netbuf_srv_poll_cb(netcon_t *nc, void *cb_data, int fd, short revents)
{
    netbuf_cli_t *cli = cb_data;
    netbuf_srv_t *srv = cli->srv;

    if (revents & POLLIN) {
        unsigned char recv_buf[NETBUF_SRV_RX_SIZE];
        buf_t buf;

        buf_init_from_static(&buf, recv_buf, NETBUF_SRV_RX_SIZE);

        int ret = buf_recv(fd, &buf, 0);
        
        if (ret < 0) {
            fprintf(stderr, "%s: read(fd=%d): %s\n", __FUNCTION__, fd, strerror(errno));
            return -1;
        }

        if (ret == 0) {
            netcon_remove_fd(nc, fd);
            close(fd);
            netbuf_free(cli->nb);
            // Call user's close callback
            if (srv->close_cb != NULL) {
                srv->close_cb(cli);
            }
            // Remove client from server's list. We have to do some pointer
            // arithmetic to get index.
            g_array_remove_index(srv->clients, cli - (netbuf_cli_t *)srv->clients->data);
            
        } else {
            while (netbuf_decode(cli->nb, &buf) > 0) {
                if (NETBUF_GET_TYPE(cli->nb) > 0 && srv->read_cb != NULL) {
                    srv->read_cb(cli, NETBUF_GET_TYPE(cli->nb), NETBUF_GET_LENGTH(cli->nb), NETBUF_GET_DATA(cli->nb));
                }
            }
        }
    }
    if (revents & POLLOUT) {
        // Send any pending data
        netbuf_send(cli->nb);
    }
    
    return 0;
}
/*
 * This is the callback fn for accepting netbuf clients.
 * Internal use only.
 */
static int netbuf_srv_accept_cb(netcon_t *nc, void *cb_data, int fd, short revents)
{
    netbuf_srv_t *srv = cb_data;

    // Add an empty netbuf_cli_t to end of client array and get pointer to it
    netbuf_cli_t tmp;
    g_array_append_val(srv->clients, tmp);
    netbuf_cli_t *cli =  &g_array_index(srv->clients, netbuf_cli_t, srv->clients->len-1);

    // Get a new netbuf and give it a fake socket number as we
    // don't know it until after accept.
    cli->nb = netbuf_new(-1, &tlv1_codec);
    cli->data = NULL; // This can be set by user-provided accept callback.
    cli->srv = srv; // So our poll event cb can access srv

    // Accept the fd and give it our poll event callback and client data.
    int ret = netcon_accept_fd(nc, fd, POLLIN, netbuf_srv_poll_cb, cli, NULL);
    if (ret < 0) {
        exit(-1);
    }
    // Set the socket in the netbuf
    cli->nb->socket = ret;

    // Call user's accept callback
    if (srv->accept_cb) {
        srv->accept_cb(cli);
    }

    // Get pointer to our new pollfd in nc. This is simple as netcon_accept_fd
    // has just appended it, so it'll be the last element in the array.
    struct pollfd *pfd = &g_array_index(nc->pollfds, struct pollfd, nc->pollfds->len-1);

    // Set tx en/dis-able callbacks for netbuf
    netbuf_set_tx_callbacks(cli->nb, netcon_tx_enable_cb, netcon_tx_disable_cb, pfd);
    

    return 0;
}

netbuf_srv_t *netbuf_srv_new(netbuf_cli_fn_t *accept_cb, netbuf_cli_rd_fn_t *read_cb, netbuf_cli_fn_t *close_cb, void *cb_data)
{
    netbuf_srv_t *srv = g_new(netbuf_srv_t, 1);
    srv->accept_cb = accept_cb;
    srv->read_cb = read_cb;
    srv->close_cb = close_cb;
    srv->cb_data = cb_data;
    srv->clients = g_array_new(FALSE, FALSE, sizeof(netbuf_cli_t));
    return srv;
}

void netcon_add_netbuf_srv_fd(netcon_t *nc, int fd, netbuf_srv_t *srv)
{
    netcon_add_fd(nc, fd, POLLIN | POLLOUT, netbuf_srv_accept_cb, srv);
}

void netbuf_srv_free(netbuf_srv_t *srv)
{
    g_array_free(srv->clients, TRUE);
    g_free(srv);
}

