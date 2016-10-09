#ifndef SERVERSOCKET_H
#define SERVERSOCKET_H

#define CONNECTION_BACKLOG 5

int make_listening_socket(char* friendlyAddress, unsigned short port);

#endif
