#define _BSD_SOURCE // for inet_aton(3)

#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "server_socket.h"
#include "clients.h"

/* make_listening_socket takes a friendly IP address and returns a listening socket
   for TCP connections on that socket.  Returns -1 if the socket could not be created. */
int make_listening_socket(char* friendlyAddress, unsigned short port) {
	// Create the socket
	int sock = socket(PF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		fprintf(stderr, "creating listening socket: error %d %s\n", errno, strerror(errno));
		return -1;
	}
	
	// Set SO_REUSEADDR so that I don't have to muck about while debugging too much
	int optval = 1;
	setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, 
		(const void *)&optval , sizeof(int));

	
	// parse the IP
	struct in_addr addr;
	if (inet_aton(friendlyAddress, &addr) == 0) {
		fprintf(stderr, "creating listening socket: could not parse IP address %s\n", friendlyAddress);
		return -1;
	}
	
	// set up the sockaddr struct
	struct sockaddr_in addrAndPort;
	addrAndPort.sin_family = AF_INET;
	addrAndPort.sin_port = htons(port);
	addrAndPort.sin_addr = addr;
	
	if (bind(sock, (struct sockaddr *)&addrAndPort, sizeof(addrAndPort)) < 0) {
		fprintf(stderr, "binding listening socket: error %d %s\n", errno, strerror(errno));
		return -1;
	}
	
	if (listen(sock, CONNECTION_BACKLOG) < 0) {
		fprintf(stderr, "listening on listening socket: error %d %s\n", errno, strerror(errno));
		return -1;
	}
	
	return sock;
}


