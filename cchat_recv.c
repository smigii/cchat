#include <asm-generic/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "cchat_utils.h"

#define MSG_SIZE 64

int main()
{
	struct sockaddr_storage new_addr;
	socklen_t new_addr_size;
	struct addrinfo hints, *res;
	int sockfd, new_sockfd;
	long status;
	char message[MSG_SIZE];
	size_t bytes_recvd, total_bytes_recvd;

	// getaddrinfo
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	status = getaddrinfo(NULL, "33667", &hints, &res);
	check_status(status, 0, "addr info");

	// socket
	sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

	// bind
	int yes=1;
	if(setsockopt(sockfd, SOL_SOCKET,SO_REUSEADDR, &yes, sizeof yes) == -1) {
		perror("setsockopt");
		exit(1);
	}
	status = bind(sockfd, res->ai_addr, res->ai_addrlen);
	check_status(status, 0, "bind");

	// listen
	status = listen(sockfd, 20);
	check_status(status, 0, "listen");

	// accept
	new_addr_size = sizeof new_addr;
	new_sockfd = accept(sockfd, (struct sockaddr*)&new_addr, &new_addr_size);
	while(1) {
		total_bytes_recvd = 0;
		// receive
		while( (bytes_recvd = recv(new_sockfd, message, MSG_SIZE - 1, 0)) != 0) {
			if(message[bytes_recvd-1] != '\0')
				message[bytes_recvd] = '\0';
			printf("%s\n", message);
			total_bytes_recvd += bytes_recvd;
		}
		printf("\nReceived [%zu] bytes from sockfd %d\n\n", total_bytes_recvd, new_sockfd);
	}

	return 0;
}
