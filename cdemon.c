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

#define ADDR_LEN 64
#define PORT_LEN 6
#define NAME_LEN 16

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
void resolve_addr_port(char* input, char* addr, size_t addr_len, char* port, size_t port_len);
void init_config(struct config* config);
void check_status(long status, long ok, const char* msg);

void handle_new_peer();

int main(int argc, char* argv[])
{
	struct addrinfo hints, *res;
	int sockfd_listen, sockfd_conn;
	size_t bytes_sent;
	long status;

	// argp ===================================================================

	struct config config;
	init_config(&config);
	struct argp_option options[] = {
		{ 0, 'l', "listen port",   0, "Port to listen on." },
		{ 0, 'c', "address:port",  0, "Connect to port @ address." },
		{ 0, 'n', "name",          0, "Name to use." },
//		{ 0, 'm', 0,			   0, "Write message." },
		{ 0 }
	};
	struct argp argp = {options, parse_opt, 0, 0};
	argp_parse(&argp, argc, argv, 0, 0, &config);
	if( (!config.fl_c && !config.fl_l) || (config.fl_c && config.fl_l)) {
		print_usage();
		exit(-1);
	}

	if(config.fl_c)
		printf("%s %s\n", config.addr, config.c_port);

	// net ====================================================================

	// Handle listener to get peers
	// getaddrinfo
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	status = getaddrinfo(NULL, config.l_port, &hints, &res);
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

	// Handle message sending

	// getaddrinfo
	if(config.fl_c) {
		memset(&hints, 0, sizeof hints);
		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_PASSIVE;

		status = getaddrinfo(config.addr, config.c_port, &hints, &res);
		check_status(status, 0, "addr info");

		// socket
		sockfd_conn = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
//		fcntl(sockfd_conn, F_SETFL, O_NONBLOCK);

		// connect
		status = connect(sockfd_conn, res->ai_addr, res->ai_addrlen);
		check_status(status, 0, "connect");

		char buffer[256];
		sprintf(buffer, "[%s] ", config.name);
		size_t offset = strlen(buffer);
		while(1) {
			fgets(buffer+offset, 256, stdin);
			bytes_sent = send(sockfd_conn, buffer, strlen(buffer), 0);
			printf("%zu bytes sent\n", bytes_sent);
		}
		return 0;
	}

	struct pollfd pfds_new_peer[1];
	struct pollfd pfds_peers[5];
	struct pollfd pfds_stdin[1];
	int n_peers = 0;

	pfds_new_peer[0].fd = sockfd_listen;
	pfds_new_peer[0].events = POLLIN;

	pfds_stdin[0].fd = 0;
	pfds_stdin[0].events = POLLIN;

	struct sockaddr_storage new_addr;
	socklen_t new_addr_size;

	int num_events;
	while(1) {
		num_events = poll(pfds_new_peer, 1, 50);
		if(num_events != 0) {
			int pollin_happened = pfds_new_peer[0].revents & POLLIN;
			if(pollin_happened) {
				int new_sockfd = accept(sockfd_listen, (struct sockaddr*)&new_addr, &new_addr_size);
				fcntl(new_sockfd, F_SETFL, O_NONBLOCK);
				pfds_peers[n_peers].fd = new_sockfd;
				pfds_peers[n_peers].events = POLLIN;
				n_peers++;

				printf("New connection? %d\n", new_sockfd);
			}
			else
				printf("WHAT THE FUCK?\n");
		}

		num_events = poll(pfds_peers, n_peers, 50);
		if(num_events != 0) {
			for(int i = 0; i < n_peers; i++) {
				int pollin_happened = pfds_peers[i].revents & POLLIN;
				if(pollin_happened) {
					int new_sockfd = pfds_peers[i].fd;
					size_t total_bytes_recvd = 0;
					size_t bytes_recvd;
					#define MSG_SIZE 128
					char message[MSG_SIZE];
					bytes_recvd = recv(new_sockfd, message, MSG_SIZE - 1, 0);
					printf("\nReceived [%zu] bytes from sockfd %d\n%s\n", bytes_recvd, new_sockfd, message);
				}
			}
		}

		num_events = poll(pfds_stdin, 1, 50);
		if(num_events != 0) {
			int pollin_happened = pfds_stdin[0].revents & POLLIN;
			if(pollin_happened) {
				printf("???????\n");
				pfds_stdin[0].revents = -1;
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

void check_status(long status, long ok, const char* msg)
{
	if(status != ok) {
		printf("ofuck: [%s]\n", msg);
		exit(-1);
	}
}