#include <asm-generic/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <argp.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>

#define ADDR_LEN 64
#define PORT_LEN 6
#define NAME_LEN 16

#define SEND_BUF_SIZE 256
#define RECV_BUF_SIZE 256

struct config {
	char name[NAME_LEN];
	char l_port[PORT_LEN];
	char c_port[PORT_LEN];
	char addr[ADDR_LEN];
	int fl_l;
	int fl_c;
};

static int parse_opt (int key, char* arg, struct argp_state* state);
void print_usage();
void print_connection_msg(int sockfd, struct sockaddr_storage* sas);
void resolve_addr_port(char* input, char* addr, size_t addr_len, char* port, size_t port_len);
void init_config(struct config* config);
void check_status(long status, long ok, const char* msg);

struct config conf;
int peer_sockfds[16];
int n_peers = 0;

void *input_thread(void *vargp)
{
	size_t bytes_sent;
	char buffer[SEND_BUF_SIZE];
	sprintf(buffer, "[%s] ", conf.name);
	size_t offset = strlen(buffer);
	while(1) {
		fgets(buffer+offset, SEND_BUF_SIZE, stdin);
		for(int i = 0; i < n_peers; i++) {
			bytes_sent = send(peer_sockfds[i], buffer, strlen(buffer), 0);
			#ifndef NDEBUG
			printf("Sent [%zu] bytes to sockfd %d\n",bytes_sent, peer_sockfds[i]);
			#endif
		}
		printf("\n");
	}
}

int main(int argc, char* argv[])
{
	struct addrinfo hints, *res;
	int sockfd_listen;
	long status;
	char recv_buffer[RECV_BUF_SIZE];

	// ========================================================================
	// argp ===================================================================

	struct argp_option options[] = {
		{ 0, 'l', "listen port",   0, "Port to listen on." },
		{ 0, 'c', "address:port",  0, "Connect to port @ address." },
		{ 0, 'n', "name",          0, "Name to use." },
		{ 0 }
	};
	struct argp argp = {options, parse_opt, 0, 0};

	init_config(&conf);
	argp_parse(&argp, argc, argv, 0, 0, &conf);

	if(!conf.fl_l) {
		print_usage();
		exit(1);
	}

	// ========================================================================
	// net ====================================================================

	// Set up listening -------------------------------------------------------

	// getaddrinfo
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	status = getaddrinfo(NULL, conf.l_port, &hints, &res);
	check_status(status, 0, "addr info");

	// socket (make non-blocking)
	sockfd_listen = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
	fcntl(sockfd_listen, F_SETFL, O_NONBLOCK);

	// bind
	int yes=1;
	if(setsockopt(sockfd_listen, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) == -1) {
		perror("setsockopt");
		exit(1);
	}
	status = bind(sockfd_listen, res->ai_addr, res->ai_addrlen);
	check_status(status, 0, "bind");

	// listen
	status = listen(sockfd_listen, 20);
	check_status(status, 0, "listen");

	// Set up sending ---------------------------------------------------------

	// Eventually, this will handle connecting to the specified peer, then
	// getting all the other peers and adding those connections.
	if(conf.fl_c) {
		int sockfd;
		struct sockaddr_storage* sas;

		memset(&hints, 0, sizeof hints);
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_PASSIVE;

		status = getaddrinfo(conf.addr, conf.c_port, &hints, &res);
		check_status(status, 0, "addr info");

		// socket
		sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

		// connect
		status = connect(sockfd, res->ai_addr, res->ai_addrlen);
		check_status(status, 0, "connect");

		peer_sockfds[0] = sockfd;
		n_peers++;

		sas = (struct sockaddr_storage*)res->ai_addr;
		print_connection_msg(sockfd, sas);
	}
	pthread_t thread;
	pthread_create(&thread, NULL, input_thread, NULL);

	// Set up polling ---------------------------------------------------------

	struct pollfd pfds_new_peer[1];  // Monitors new peer requests
	struct pollfd pfds_peers[16];    // Monitors existing peer connections

	pfds_new_peer[0].fd = sockfd_listen;
	pfds_new_peer[0].events = POLLIN;

	for(int i = 0; i < n_peers; i++) {
		pfds_peers[i].fd = peer_sockfds[i];
		pfds_peers[i].events = POLLIN;
	}

	struct sockaddr_storage new_addr;
	socklen_t new_addr_size = sizeof new_addr;

	int num_events;
	int has_pollin;
	int sockfd;
	size_t bytes_recvd;
	while(1) {
		num_events = poll(pfds_new_peer, 1, 50);
		if(num_events != 0) {
			has_pollin = pfds_new_peer[0].revents & POLLIN;
			if(has_pollin) {
				sockfd = accept(sockfd_listen, (struct sockaddr*)&new_addr, &new_addr_size);
				peer_sockfds[n_peers] = sockfd;
				fcntl(sockfd, F_SETFL, O_NONBLOCK);
				pfds_peers[n_peers].fd = sockfd;
				pfds_peers[n_peers].events = POLLIN;
				n_peers++;

				print_connection_msg(sockfd, &new_addr);
			}
		}

		num_events = poll(pfds_peers, n_peers, 50);
		if(num_events != 0) {
			for(int i = 0; i < n_peers; i++) {
				has_pollin = pfds_peers[i].revents & POLLIN;
				if(has_pollin) {
					sockfd = pfds_peers[i].fd;
					bytes_recvd = recv(sockfd, recv_buffer, RECV_BUF_SIZE - 1, 0);
					recv_buffer[bytes_recvd - 1] = '\0';
					#ifndef NDEBUG
					printf("Received [%zu] bytes from sockfd %d\n", bytes_recvd, sockfd);
					#endif
					printf("%s\n\n", recv_buffer);
				}
			}
		}

	}

	return 0;

}

static int parse_opt (int key, char* arg, struct argp_state* state) {
	struct config* config = state->input;
	switch (key){
		case 'c':
		{
			resolve_addr_port(arg, config->addr, ADDR_LEN, config->c_port, PORT_LEN);
			config->fl_c = 1;
			break;
		}
		case 'l':
		{
			strncpy(config->l_port, arg, PORT_LEN);
			config->fl_l = 1;
			break;
		}
		case 'n':
		{
			strncpy(config->name, arg, NAME_LEN);
			break;
		}
		default :
			break;
	}
	return 0;
}

void init_config(struct config* config)
{
	memset(config->addr, 0, ADDR_LEN);
	memset(config->c_port, 0, PORT_LEN);
	memset(config->l_port, 0, PORT_LEN);
	strncpy(config->name, "anon", 5);
	config->fl_c = 0;
	config->fl_l = 0;
}

void resolve_addr_port(char* input, char* addr, size_t addr_len, char* port, size_t port_len)
{
	int i = 0;
	int delim = -1;
	while(input[i] != '\0') {
		if(input[i] == ':')
			delim = i;
		i++;
	}
	strncpy(addr, input, addr_len);
	addr[delim] = '\0';

	strncpy(port, input+delim+1, port_len);
}

void print_usage()
{
	printf("USAGE: cdemon -l port [-c addr:port, -n name]\n");
}

void print_connection_msg(int sockfd, struct sockaddr_storage* sas)
{
	char buf[32];
	if(sas->ss_family == AF_INET) {
		struct sockaddr_in* sa_in = (struct sockaddr_in*)sas;
		inet_ntop(sa_in->sin_family, &sa_in->sin_addr, buf, 32);
		printf("New connection - %s:%d - sockfd [%d]\n\n", buf, htons(sa_in->sin_port), sockfd);
	}
}

void check_status(long status, long ok, const char* msg)
{
	if(status != ok) {
		printf("ofuck: [%s]\n", msg);
		exit(-1);
	}
}