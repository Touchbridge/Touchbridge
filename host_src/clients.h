/* clients.{h,c} defines a list of client sockets that we might be waiting on, and
   a means to handle a read on one of those sockets (well, not yet, but it will).  When
   we've received a full frame (terminated by a \n), we'll pass this through to a magical
   frame handling function for further noodling. */

#ifndef CLIENTS_H
#define CLIENTS_H

#include <poll.h>

typedef int client_rd_callback_t(unsigned char *data, int length, void *cb_data);
typedef int client_fd_callback_t(int fd, void *cb_data);

void add_client_by_descriptor(int fd, client_rd_callback_t *callback, void *cb_data);
void remove_client_by_descriptor(int fd);
void handle_client_read_by_descriptor(int fd);
void handle_client_reads_by_pollfds(struct pollfd *fds, int constantFDcount);
void for_all_clients(client_fd_callback_t *callback, void *cb_data);
int count_clients();

struct pollfd *create_client_pollfds_array(struct pollfd constantfds[], int num_const_fds, int *numfds);
void free_client_pollfds_array(struct pollfd *pollfds);

#endif
