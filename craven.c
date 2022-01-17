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

#include "cr_packet.h"
#include "utils.h"

#define MAX_PEERS 16
#define SEND_BUF_SIZE 256
#define RECV_BUF_SIZE 256

struct config {
	char name[NAME_LEN];
	unsigned short l_port;
	unsigned short c_port;
	char addr[ADDR_LEN];
	int fl_l;
	int fl_c;
};

struct peer {
	int sockfd;
	struct cr_meta meta;
};

static int parse_opt (int key, char* arg, struct argp_state* state);
void print_usage();
void print_connection_msg(int sockfd, struct sockaddr_storage* sas);
void init_config(struct config* config);
void check_status(long status, long ok, const char* msg);

void send_meta(int sockfd);

struct config conf;
struct peer peers[MAX_PEERS];
int n_peers = 0;

void *input_thread(void *vargp)
{
	size_t bytes_sent;
	struct cr_msg message;
	message.type = CR_MSG;

	while(1) {
		fgets(message.message, SEND_BUF_SIZE, stdin);
		for(int i = 0; i < n_peers; i++) {
			bytes_sent = send(peers[i].sockfd, &message, cr_msg_size(&message), 0);
			#ifndef NDEBUG
			printf("Sent [%zu] bytes to sockfd %d\n",bytes_sent, peers[i].sockfd);
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
	char port_buf[PORT_LEN]; // For converting port from ushort to char[]

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
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	snprintf(port_buf, PORT_LEN, "%hu", conf.l_port); // %hu = unsigned short
	status = getaddrinfo(NULL, port_buf, &hints, &res);
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
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_PASSIVE;

		snprintf(port_buf, PORT_LEN, "%hu", conf.c_port); // %hu = unsigned short
		status = getaddrinfo(conf.addr, port_buf, &hints, &res);
		check_status(status, 0, "addr info");

		// socket
		sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

		// connect, then send our info
		status = connect(sockfd, res->ai_addr, res->ai_addrlen);
		check_status(status, 0, "connect");
		send_meta(sockfd);

		peers[0].sockfd = sockfd;
		n_peers++;

		sas = (struct sockaddr_storage*)res->ai_addr;
		print_connection_msg(sockfd, sas);
	}
	pthread_t thread;
	pthread_create(&thread, NULL, input_thread, NULL);

	// Polling ----------------------------------------------------------------

	struct pollfd pfds_new_peer[1];  // Monitors new peer requests
	struct pollfd pfds_peers[16];    // Monitors existing peer connections

	pfds_new_peer[0].fd = sockfd_listen;
	pfds_new_peer[0].events = POLLIN;

	for(int i = 0; i < n_peers; i++) {
		pfds_peers[i].fd = peers[i].sockfd;
		pfds_peers[i].events = POLLIN;
	}

	struct sockaddr_storage new_addr;
	socklen_t new_addr_size = sizeof new_addr;
	struct cr_packet crp;
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
				send_meta(sockfd);
				fcntl(sockfd, F_SETFL, O_NONBLOCK);

				// Add peer to list
				struct peer* peer = &(peers[n_peers]);
				peer->sockfd = sockfd;
				memset(&(peer->meta), 0, sizeof (struct cr_meta));

				// Monitor the new socket
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
					struct peer* peer = &(peers[i]);
					bytes_recvd = recv(sockfd, &crp, RECV_BUF_SIZE - 1, 0);

					if(crp.type == CR_MSG) {
						struct cr_msg* crm = (struct cr_msg*)&crp;
						#ifndef NDEBUG
						printf("Received [%zu] bytes from sockfd %d\n", bytes_recvd, sockfd);
						#endif
						printf("[%s] %s\n", peer->meta.name, crm->message);
					}
					else if(crp.type == CR_META) {
						struct cr_meta* cri = (struct cr_meta*)&crp;
						memcpy(&(peer->meta), cri, sizeof (struct cr_meta));
						struct sockaddr_in sa_in;
						socklen_t len = sizeof (struct sockaddr);
						getpeername(peer->sockfd, (struct sockaddr*)&sa_in, &len);
						char buf[32];
						inet_ntop(sa_in.sin_family, &(sa_in.sin_addr), buf, 32);
						printf("--RECEIVED INFO--\n");
						printf("From: %s:%hu [%d]\n", buf, htons(sa_in.sin_port), peer->sockfd);
						printf("Listen Port: %hu\nName: %s\n\n", cri->l_port, cri->name);
					}
					else {
						printf("RECEIVED UNKNOWN PACKET\n\n");
					}

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
			int i = 0;
			int delim = -1;
			while(arg[i] != '\0') {
				if(arg[i] == ':')
					delim = i;
				i++;
			}
			strncpy(config->addr, arg, ADDR_LEN);
			config->addr[delim] = '\0';
			config->c_port = strtol(arg+delim+1, NULL, 10);
			config->fl_c = 1;
			break;
		}
		case 'l':
		{
			config->l_port = strtol(arg, NULL, 10);
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
	config->l_port = 0;
	config->c_port = 0;
	strncpy(config->name, "anon", 5);
	config->fl_c = 0;
	config->fl_l = 0;
}

void print_usage()
{
	printf("USAGE: craven -l port [-c addr:port | -n name]\n");
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

void send_meta(int sockfd)
{
	struct cr_meta cri;
	cri.type = CR_META;
	cri.l_port = conf.l_port;
	strncpy(cri.name, conf.name, NAME_LEN);
	send(sockfd, &cri, sizeof cri, 0);
}