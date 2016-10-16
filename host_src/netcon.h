
#ifndef NETSOCK_H
#define NETSOCK_H

#include <glib.h>
#include <poll.h>

#include "netbuf.h"

struct netcon_fd_s;
struct netcon_s;

typedef int (netcon_fn_t)(struct netcon_s *nc, void *cb_data, int fd, short revents);

#define NETCON_USER_DATA(nd)    ((nd)->data)
#define NETCON_FD (nd)          ((nd)->fd)

typedef struct netcon_fd_t {
    netcon_fn_t *callback;
    void *data;
} netcon_fd_t;

typedef struct netcon_addr_s {
    struct sockaddr addr;   // Address data
    socklen_t addr_len;     // Length of address data
} netcon_addr_t;

typedef struct netcon_s {
    GArray *pollfds;        // GArray of struct pollfd
    GArray *netcon_fds;     // GArray of netcon_fd_t which maps 1:1 to pollfds
} netcon_t;

int netcon_sock_listen(char *net_addr, unsigned short port);
int netcon_sock_connect(char *net_addr, unsigned short port);

netcon_t *netcon_new(void);
void netcon_add_fd(netcon_t *nc, int fd, short events, netcon_fn_t *cb, void *cb_data);
int netcon_accept_fd(netcon_t *nc, int fd, short events, netcon_fn_t *cb, void *cb_data, netcon_addr_t *addr);
void netcon_remove_fd(netcon_t *nc, int fd);
int netcon_poll(netcon_t *nc, int timeout);
int netcon_main_loop(netcon_t *nc);
void netcon_free(netcon_t *nc);

char *netcon_addr_to_str(netcon_addr_t *a);
unsigned short netcon_addr_port(netcon_addr_t *a);

#endif //NETSOCK_H
