#include <stddef.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>

#include "clients.h"

/* Framing for client connections is sent thus:
   :<length><data>\n
   
   The : is there to sanity check the beginning of a packet, the \n at the end likewise,
   (and also so that people who prefer to read packets linewise have it easy).  Lengths
   are 1 byte unsigned.  A length of 0 is invalid. */
   
#define PACKET_START (':')
#define PACKET_END ('\n')
typedef unsigned char packetlen_t;

/*
 * The payload can be up to 255 bytes long.  The buffer also needs to keep
 * track of what state the parsing state machine is in (see next paragraph),
 * and where to put the next character in the buffer.
*/
typedef struct packetbuffer_t { 
	packetlen_t len;
	unsigned char buf[255];
	unsigned char state;
	unsigned int write_position;
	char* error_message;
} packetbuffer_t;

/*
 * A client needs to store the descriptor associated with its socket and a
 * buffer to put its data in.
 */

typedef struct client_t {
	int sd;
	packetbuffer_t buf;
    client_rd_callback_t *callback;
    void *cb_data;
} client_t;


client_t *find_client_by_descriptor(int fd);

void clear_packetbuffer(packetbuffer_t *buf) {
	buf->len = 0;
	buf->state = 0;
}

void print_packetbuffer(packetbuffer_t *buf) {
	char biggerBuffer[256];
	memcpy(biggerBuffer, buf->buf, 255);
	biggerBuffer[buf->write_position] = '\0';
	printf("length: %d, ascii: %s\n", buf->len, biggerBuffer);
}

/* There is a state machine for parsing messages.

           +----------+         +----------+
           |delimiter?+-------->|readLength|
           +----------+         +----+-----+
                                     |
                                     |
                                     |
                                     v
           +-----------+        +--------+
           |terminator?|<-------+readByte+--------+
           +-----------+        +--------+        |
                                     ^            |
                                     |            | len times
                                     |            |
                                     +------------+
                                     
*/

#define CLI_STATE_READ_DELIMITER (0)
#define CLI_STATE_READ_LENGTH (1)
#define CLI_STATE_READ_BYTE (2)
#define CLI_STATE_READ_TERMINATOR (3)

/* We also need an error state. */
#define CLI_STATE_ERROR (255)

/* And a complete packet state. */
#define CLI_STATE_COMPLETE (254)


/* The state machine has a transition function.  This takes a byte, stores it if
   appropriate, and manages the transition between states. */

void cli_transition_function(packetbuffer_t *buf, unsigned char byte) {
	switch (buf->state) {
		case CLI_STATE_READ_DELIMITER:
			/* The read delimiter state moves to the readLength state if the first character
			   is the start character.  If not, it errors. */
			if (byte == PACKET_START) {
				buf->state = CLI_STATE_READ_LENGTH;
			} else {
				buf->error_message = "Expected a delimiter, got something else.";
				buf->state = CLI_STATE_ERROR;
			}
			break;
		
		case CLI_STATE_READ_LENGTH:
			/* In the read length state, the byte we get is the length of the buffer.
			   Thus, we need to store that byte and zero the write position for the buffer
			   so that the next byte goes in at the start of the buffer. */
			buf->len = byte;
			buf->write_position = 0;
			buf->state = CLI_STATE_READ_BYTE;
			break;
			
		case CLI_STATE_READ_BYTE:
			/* In the read byte state, we should read a single byte into the buffer.  If
			   the byte would take the packet length above the declared length, we
			   move to the next state. */
			if (buf->write_position == buf->len -1) {
				// read one more byte
				buf->buf[buf->write_position] = byte;
				buf->write_position++;
				// then move on to next state
				buf->state = CLI_STATE_READ_TERMINATOR;
			} else {
				buf->buf[buf->write_position] = byte;
				buf->write_position++;
			}
			break;
			
		case CLI_STATE_READ_TERMINATOR:
			/* In the read terminator state, we should check whether the single byte is
			   a terminator.  If it is, we are complete.  If not, we error. */
			if (byte == PACKET_END) {
				buf->state = CLI_STATE_COMPLETE;
			} else {
				buf->error_message = "Expected a terminator, got something else.";
				buf->state = CLI_STATE_ERROR;
			}
			break;
	}
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
		/* We have data.  Let's do something sensible with it. */
		int i;
		for (i = 0; i < recv_count; i++) {
			cli_transition_function(&cli->buf, buf[i]);
			if (cli->buf.state == CLI_STATE_COMPLETE) {
				/* Do something sensible with the message */
				//print_packetbuffer(&cli->buf);
				//clear_packetbuffer(&cli->buf);
                if (cli->callback) {
                    cli->callback(cli->buf.buf, cli->buf.len, cli->cb_data);
                }
			} else if (cli->buf.state == CLI_STATE_ERROR) {
				/* An error, bail out. */
				printf("protocol error: %s\n", cli->buf.error_message);
				clear_packetbuffer(&cli->buf);
			}
		}
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
	clear_packetbuffer(&new_node->client.buf);
	
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
