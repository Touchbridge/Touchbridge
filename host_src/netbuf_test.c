
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <poll.h>
#include <sys/socket.h>

#include <glib.h>

#include "debug.h"
#include "server_socket.h"
#include "netbuf.h"


typedef int client_rd_callback_t(unsigned char *data, int length, void *cb_data);
typedef int client_fd_callback_t(int fd, void *cb_data);

typedef struct client_t {
	int sd;
    client_rd_callback_t *callback;
    void *cb_data;
} client_t;

void add_client_by_descriptor(int fd, client_rd_callback_t *callback, void *cb_data);
void remove_client_by_descriptor(int fd);
void handle_client_read_by_descriptor(int fd);
void handle_client_reads_by_pollfds(struct pollfd *fds, int constantFDcount);
void for_all_clients(client_fd_callback_t *callback, void *cb_data);
int count_clients();

struct pollfd *create_client_pollfds_array(struct pollfd constantfds[], int num_const_fds, int *numfds);
void free_client_pollfds_array(struct pollfd *pollfds);

int do_client_receive(unsigned char *data, int length, void *cb_data)
{
    return 0;
}

int accept_socket_on_listener(int sock)
{
	struct sockaddr addr;
	socklen_t len;
	int client_sock;
	client_sock = accept(sock, &addr, &len);
    printf("Got new client\n");
	
	add_client_by_descriptor(client_sock, do_client_receive, NULL);
	return client_sock;
}

int main(int argc, char **argv)
{
	int sock = make_listening_socket("127.0.0.1", 5555);
	struct pollfd constant_fds[] = {
		{ .fd = sock,  .events = POLLIN,  .revents = 0 },
	};
    int num_const_fds = sizeof(constant_fds)/sizeof(struct pollfd);

	struct pollfd *items;

	while(1) {
        int num_items;
		items = create_client_pollfds_array(constant_fds, num_const_fds, &num_items);
		items[0].revents = 0; // FIXME: do we need this?
		
		int pollResult = poll(items, num_items, 2000);
		
		if (pollResult < 0) {

			// something's gone a bit awry.
			SYSERROR("poll");

		} else if (pollResult > 0) {

            // New client connected
			if (items[0].revents != 0) {
				accept_socket_on_listener(sock);
			}
			
            // Handle any data from clients
			handle_client_reads_by_pollfds(items, 1);
		}
		
		free_client_pollfds_array(items);
    }
    return 0;
}

void handle_client_read(client_t *cli) {
	unsigned char buf[256];
	ssize_t recv_count = recv(cli->sd, buf, 256, 0);
	
	if (recv_count < 0) {
		// An error has occurred: TODO: do something sensible here.
		printf("there was a connection error.\n");
		shutdown(cli->sd, SHUT_RDWR);
		close(cli->sd);
		remove_client_by_descriptor(cli->sd);
	} else if (recv_count == 0) {
		// The client has torn down their side of the connection, close ours and remove
		// the client from the list.
		printf("lost a connection.\n");
		shutdown(cli->sd, SHUT_RDWR);
		close(cli->sd);
		remove_client_by_descriptor(cli->sd);
	} else {
		/* We have data. */
		printf("Got %d bytes of data.\n", recv_count);
	}
}

/* We also need a list of clients.  This is a linked list. */

typedef struct client_node_t {
	client_t client;
	struct client_node_t *next;
} client_node_t;

client_node_t *client_list_head = NULL;

int count_clients(void)
{
	int count = 0;
	client_node_t *node = client_list_head;
	
	while (node != NULL) {
		count++;
		node = node->next;
	}
	
	return count;
}

void for_all_clients(client_fd_callback_t *callback, void *cb_data)
{
	client_node_t *node = client_list_head;
	
	while (node != NULL) {
        if (callback) {
            callback(node->client.sd, cb_data);
        }
		node = node->next;
	}
}

void add_client_by_descriptor(int fd, client_rd_callback_t *callback, void *cb_data)
{
	/* First create a new client node */
	client_node_t *new_node;
	new_node = malloc(sizeof(client_node_t));
	new_node->next = NULL;
	new_node->client.sd = fd;
    new_node->client.callback = callback;
    new_node->client.cb_data = cb_data;
	
	/* And add it to the list */
	if (client_list_head == NULL) {
		client_list_head = new_node;	
	} else {
		client_node_t *previous_node;
		previous_node = client_list_head;
		
		while (previous_node->next != NULL) {
			previous_node = previous_node->next;
		}		
		
		previous_node->next = new_node;
	}
}

void remove_client_by_descriptor(int fd)
{
	client_node_t *node = client_list_head;
	client_node_t *tempnode = NULL;
	
	if (client_list_head == NULL) {
		return;
	}
	
	if (client_list_head->client.sd == fd) {
		client_list_head = node->next;
		free(node);
		return;
	}
	
	while (node != NULL) {
		if (node->next == NULL) {
			return;
		}
		
		if (node->next->client.sd == fd) {
			tempnode = node->next;
			node->next = node->next->next;
			free(tempnode);
			return;
		}
	}
}

/*
 * Given a file descriptor 'fd', find a client in the linked-list of
 * clients and return a pointer to it. Returns null if fd not found.
 */
client_t *find_client_by_descriptor(int fd)
{
	client_node_t *node, *found_node;
	node = client_list_head;
	found_node = NULL;
	
	while (node != NULL) {
		if (node->client.sd == fd) {
			found_node = node;
			break;
		}
		node = node->next;
	}
	
    if (found_node) {
        return &(found_node->client);
    } else {
        return NULL;
    }
}

void handle_client_read_by_descriptor(int fd)
{
	client_t *client = find_client_by_descriptor(fd);
	
	if (client) {
		handle_client_read(client);
	}
}


/* We need to construct a number of pollfds.  This is messy and inefficient, but it'll
   work. */ 
struct pollfd *create_client_pollfds_array(struct pollfd constantfds[], int num_const_fds, int *numfds)
{
	int count = count_clients();
	count += num_const_fds;
	
	struct pollfd *fds = malloc(count * sizeof(struct pollfd));
	
	// copy the constant pollfds in
	memcpy(fds, constantfds, num_const_fds * sizeof(struct pollfd));
	
	// Then copy in the rest
	struct pollfd *nextfd = fds + num_const_fds;
	
	client_node_t *node = client_list_head;
	while (node != NULL) {
		nextfd->fd = node->client.sd;
		nextfd->events = POLLIN;
		nextfd->revents = 0;
	
		nextfd++;
		node = node->next;
	}
    if (numfds) {
        *numfds = count;
    }	
	return fds;
}

void free_client_pollfds_array(struct pollfd *fds)
{
	free(fds);
}

void handle_client_reads_by_pollfds(struct pollfd *pollfds, int constantFDcount)
{
	struct pollfd *pfd = pollfds + constantFDcount;
	for (int i = 0; i < count_clients(); i++) {
		if (pfd->revents != 0) {
			handle_client_read_by_descriptor(pfd->fd);
		}
		pfd++;
	}
}
