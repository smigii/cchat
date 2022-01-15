#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <argp.h>

#include "cchat_utils.h"

/* ---------------------------------------------------------
 * USAGE: cchat-send -[pa] -[m|f path/to/file]
 * -p port
 * -a addr
 * -m write message
 * -f file
 * ------------------------------------------------------ */

#define ADDR_LEN 64
#define PORT_LEN 6
#define PATH_LEN 128
struct config {
	char addr[ADDR_LEN];
	char port[PORT_LEN];
	char path[PATH_LEN];
	int fl_msg;
	int fl_morf;  // Has -m or -f been specified
};

struct message {
	char* msg;
	unsigned long len;
};

void init_config(struct config* config)
{
	memset(config->addr, 0, ADDR_LEN);
	memset(config->port, 0, PORT_LEN);
	memset(config->path, 0, PATH_LEN);
	config->fl_msg = 0;
	config->fl_morf = 0;
}

static int parse_opt (int key, char* arg, struct argp_state* state) {
	struct config* config = state->input;
	switch (key){
		case 'a':
		{
			strncpy(config->addr, arg, ADDR_LEN);
			break;
		}
		case 'p':
		{
			strncpy(config->port, arg, PORT_LEN);
			break;
		}
		case 'm':
		{
			config->fl_msg = 1;
			config->fl_morf = 1;
			break;
		}
		case 'f':
		{
			strncpy(config->path, arg, PATH_LEN);
			config->fl_morf = 1;
			break;
		}
		default :
			break;
	}
	return 0;
}

void print_usage()
{
	printf("USAGE: cchat-send -a addr -p port -m|-f path/to/file\n");
}

struct message create_message()
{
	#define BUF_SIZE 1024

	int block = 256;
	int n = 1;
	struct message msg;
	msg.msg = (char*)malloc(block);
	msg.len = 0;
	char buffer[BUF_SIZE];
	unsigned long buf_len;

	//printf("\nEnter --SEND-- to send message.\n");
	printf("\n--BEGIN MESSAGE--\n");

	fgets(buffer, BUF_SIZE, stdin);
	while(strncmp("--SEND--\n", buffer, BUF_SIZE) != 0) {
		buf_len = strlen(buffer);
		while(buf_len > (n * block) - msg.len - 1) {
			n++;
			msg.msg = (char*)realloc(msg.msg, n * block);
		}
		strncat(msg.msg, buffer, buf_len);
		msg.len += buf_len;
		fgets(buffer, BUF_SIZE, stdin);
	}

	printf("\n");

	return msg;
}

void shred_message(struct message* message)
{
	free(message->msg);
}

int main(int argc, char* argv[])
{
	struct addrinfo hints, *res;
	int sockfd;
	size_t bytes_sent;
	long status;

	// argp ------------------------------------------------

	struct config config;
	init_config(&config);
	struct argp_option options[] = {
		{ 0, 'a', "Address",    0, "Destination address." },
		{ 0, 'p', "Port",       0, "Destination port." },
		{ 0, 'f', "File",       0, "File to send." },
		{ 0, 'm', 0,			0, "Write message." },
		{ 0 }
	};
	struct argp argp = {options, parse_opt, 0, 0};
	argp_parse(&argp, argc, argv, 0, 0, &config);
	if(config.fl_morf == 0) {
		print_usage();
		exit(-1);
	}


	// fuckinsenditboys ------------------------------------

	// getaddrinfo
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;

	status = getaddrinfo(config.addr, config.port, &hints, &res);
	check_status(status, 0, "addr info");

	// socket
	sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

	// connect
	status = connect(sockfd, res->ai_addr, res->ai_addrlen);
	check_status(status, 0, "connect");

	struct message msg;

	while(1) {
		msg = create_message();

		// send
		bytes_sent = send(sockfd, msg.msg, msg.len, 0);
		printf("sent [%zu] bytes\n", bytes_sent);
		shred_message(&msg);
	}

	// Clean up

	freeaddrinfo(res);

	return 0;
}
