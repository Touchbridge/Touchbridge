
#ifndef NETSOCK_H
#define NETSOCK_H

#include <glib.h>
#include <stdint.h>
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

struct netbuf_srv_s;

typedef struct {
    struct netbuf_srv_s *srv;
    netbuf_t *nb;
    void *data;
} netbuf_cli_t;

typedef int (netbuf_cli_fn_t)(netbuf_cli_t *cli);
typedef void (netbuf_cli_rd_fn_t)(netbuf_cli_t *cli, int type, int length, uint8_t *data);

typedef struct netbuf_srv_s {
    netbuf_cli_fn_t *accept_cb;
    netbuf_cli_fn_t *close_cb;
    netbuf_cli_rd_fn_t *read_cb;
    void *cb_data;
    GArray *clients;    // Array of netbuf_cli_t
} netbuf_srv_t;

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

netbuf_srv_t *netbuf_srv_new(netbuf_cli_fn_t *accept_cb, netbuf_cli_rd_fn_t *read_cd, netbuf_cli_fn_t *close_cb, void *cb_data);
void netcon_add_netbuf_srv_fd(netcon_t *nc, int fd, netbuf_srv_t *srv);
void netbuf_srv_free(netbuf_srv_t *srv);

#endif //NETSOCK_H
