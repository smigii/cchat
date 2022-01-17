#include <asm-generic/socket.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
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

#include "packet.h"
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

int make_connection(char* addr, unsigned short port, struct pollfd* pollfds);
void send_meta(int sockfd, char forward);

struct config conf;
struct peer peers[MAX_PEERS];
int n_peers = 0;
int run = 1;

void *input_loop(void *vargp)
{
	size_t bytes_sent;
	struct cr_msg message;
	message.type = CR_MSG;

	while(run) {
		fgets(message.message, SEND_BUF_SIZE, stdin);
		if(strncmp("\n", message.message, 1) == 0) {
			continue;
		}
		else if(strncmp(":ls", message.message, 3) == 0) {
			for(int i = 0; i < n_peers; i++) {
				printf("%s - %hu - %d\n", peers[i].meta.name, peers[i].meta.l_port, peers[i].sockfd);
			}
			printf("\n");
		}
		else if(strncmp(":whoami", message.message, 7) == 0) {
			printf("%s - %hu\n\n", conf.name, conf.l_port);
		}
		else if(strncmp(":q", message.message, 2) == 0) {
			run = 0;
		}
		else {
			for(int i = 0; i < n_peers; i++) {
				bytes_sent = send(peers[i].sockfd, &message, cr_msg_size(&message), 0);
				#ifndef NDEBUG
				printf("Sent [%zu] bytes to sockfd %d\n",bytes_sent, peers[i].sockfd);
				#endif
			}
			printf("\n");
		}
	}
}

int main(int argc, char* argv[])
{
	struct addrinfo hints, *res;
	int sockfd_listen;
	long status;
	char addr_buf[ADDR_LEN]; // For holding addresses in presentation form
	char port_buf[PORT_LEN]; // For converting port from ushort to char[]

	struct pollfd pfds_new_peer[1];  // Monitors new peer requests
	struct pollfd pfds_peers[16];    // Monitors existing peer connections

	// ========================================================================
	// argp ===================================================================

	struct argp_option options[] = {
		{ 0, 'l', "port",          0, "Port to listen on." },
		{ 0, 'c', "address:port",  0, "Connect to port @ address." },
		{ 0, 'n', "name",          0, "Name that other peers will see." },
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

	// CLA Connection ---------------------------------------------------------

	if(conf.fl_c) {
		int sockfd = make_connection(conf.addr, conf.c_port, pfds_peers);
		send_meta(sockfd, 1);
	}

	// Input thread -----------------------------------------------------------

	pthread_t input_thread;
	pthread_create(&input_thread, NULL, input_loop, NULL);

	// Polling ----------------------------------------------------------------

	pfds_new_peer[0].fd = sockfd_listen;
	pfds_new_peer[0].events = POLLIN;

	struct sockaddr_storage new_addr;
	socklen_t new_addr_size = sizeof new_addr;
	struct cr_packet crp;
	int num_events;
	int has_pollin;
	size_t bytes_recvd;

	while(run) {
		num_events = poll(pfds_new_peer, 1, 50);
		if(num_events != 0) {
			has_pollin = pfds_new_peer[0].revents & POLLIN;
			if(has_pollin) {
				int sockfd = accept(sockfd_listen, (struct sockaddr*)&new_addr, &new_addr_size);
				fcntl(sockfd, F_SETFL, O_NONBLOCK);
				send_meta(sockfd, 0);

				// Add peer to list
				struct peer* peer = &(peers[n_peers]);
				peer->sockfd = sockfd;
				memset(&(peer->meta), 0, sizeof (struct cr_meta));

				// Monitor the new socket
				pfds_peers[n_peers].fd = sockfd;
				pfds_peers[n_peers].events = POLLIN;
				n_peers++;

				#ifndef NDEBUG
				print_connection_msg(sockfd, &new_addr);
				#endif
			}
		}

		num_events = poll(pfds_peers, n_peers, 50);
		if(num_events != 0) {
			for(int i = 0; i < n_peers; i++) {
				has_pollin = pfds_peers[i].revents & POLLIN;
				if(has_pollin) {
					int sockfd = pfds_peers[i].fd;
					struct peer* peer = &(peers[i]);
					struct sockaddr_in sa_in;
					socklen_t len = sizeof (struct sockaddr);
					getpeername(peer->sockfd, (struct sockaddr*)&sa_in, &len);
					inet_ntop(sa_in.sin_family, &(sa_in.sin_addr), addr_buf, 32);
					unsigned short port = htons(sa_in.sin_port);
					bytes_recvd = recv(sockfd, &crp, RECV_BUF_SIZE - 1, 0);

					if(crp.type == CR_MSG) {
						struct cr_msg* crm = (struct cr_msg*)&crp;
						#ifndef NDEBUG
						printf("Received [%zu] bytes from sockfd %d\n", bytes_recvd, sockfd);
						#endif
						printf("[%s] %s\n", peer->meta.name, crm->message);
					}
					else if(crp.type == CR_META) {
						struct cr_meta* crm = (struct cr_meta*)&crp;
						memcpy(&(peer->meta), crm, sizeof (struct cr_meta));
						#ifndef NDEBUG
						printf("--RECEIVED INFO--\n");
						printf("From: %s:%hu [%d]\n", addr_buf, htons(sa_in.sin_port), peer->sockfd);
						printf("Listen Port: %hu\nName: %s\n\n", crm->l_port, crm->name);
						#else
						printf("Peer connected: %s@%s:%hu - L:%hu\n\n", crm->name, addr_buf, port, crm->l_port);
						#endif

						// Tell all other peers to connect to new person
						if(crm->forward) {
							struct cr_conn new_conn;
							new_conn.type = CR_CONN;
							strncpy(new_conn.addr, addr_buf, ADDR_LEN);
							new_conn.port = crm->l_port;
							for(int p = 0; p < n_peers; p++) {
								if(peers[p].sockfd != sockfd) {
									send(peers[p].sockfd, &new_conn, sizeof (struct cr_conn), 0);
								}
							}
						}
					}
					else if(crp.type == CR_CONN) {
						struct cr_conn* crc = (struct cr_conn*)&crp;
						sockfd = make_connection(crc->addr, crc->port, pfds_peers);
						send_meta(sockfd, 0);
					}
					else if(crp.type == CR_KILL) {
						printf("Peer disconnected: %s@%s:%hu - L:%hu\n\n", peer->meta.name, addr_buf, port, peer->meta.l_port);
						close(peers[i].sockfd);
						memcpy(&peers[i], &peers[n_peers-1], sizeof (struct peer));
						memcpy(&pfds_peers[i], &pfds_peers[n_peers-1], sizeof (struct pollfd));
						n_peers--;
					}
					else {
						printf("RECEIVED UNKNOWN PACKET\n\n");
					}

				}
			}
		}

	}

	pthread_join(input_thread, NULL);

	struct cr_kill crk;
	crk.type = CR_KILL;
	for(int i = 0; i < n_peers; i++) {
		send(peers[i].sockfd, &crk, sizeof (struct cr_kill), 0);
		close(peers[i].sockfd);
	}

	freeaddrinfo(res);

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
	printf(
		"USAGE: craven -l port [-c addr:port | -n name]\n"
		"Commands:\n"
		"    :ls     -> List current peers\n"
		"    :q      -> Quit\n"
		"    :whoami -> Get name and port information\n"
	);
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

void send_meta(int sockfd, char forward)
{
	struct cr_meta cri;
	cri.type = CR_META;
	cri.l_port = conf.l_port;
	cri.forward = forward;
	strncpy(cri.name, conf.name, NAME_LEN);
	send(sockfd, &cri, sizeof cri, 0);
}

int make_connection(char* addr, unsigned short port, struct pollfd* pollfds)
{
	// Dumb check
	for(int i = 0; i < n_peers; i++) {
		if(peers[i].meta.l_port == port)
			return 0;
	}

	struct addrinfo hints, *res;
	int sockfd;
	int status;
	char port_buf[PORT_LEN];

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	snprintf(port_buf, PORT_LEN, "%hu", port);
	status = getaddrinfo(addr, port_buf, &hints, &res);
	check_status(status, 0, "addr info");

	// socket
	sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

	// connect, then send our info
	status = connect(sockfd, res->ai_addr, res->ai_addrlen);
	check_status(status, 0, "connect");

	peers[n_peers].sockfd = sockfd;
	pollfds[n_peers].fd = peers[n_peers].sockfd;
	pollfds[n_peers].events = POLLIN;

	n_peers++;

	#ifndef NDEBUG
	struct sockaddr_storage* sas;
	sas = (struct sockaddr_storage*)res->ai_addr;
	print_connection_msg(sockfd, sas);
	#endif

	freeaddrinfo(res);

	return sockfd;
}