#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "cchat_utils.h"

int main()
{
	struct addrinfo hints, *res;
	int sockfd;
	size_t bytes_sent;
	long status;

	// getaddrinfo
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	status = getaddrinfo(NULL, "33666", &hints, &res);
	check_status(status, 0, "addr info");

	// socket
	sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

	// connect
	status = connect(sockfd, res->ai_addr, res->ai_addrlen);
	check_status(status, 0, "connect");

	// send
	bytes_sent = send(sockfd, "HELLO FUCKO", 12, 0);
	printf("sent [%zu] bytes\n", bytes_sent);

	return 0;
}
