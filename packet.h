#ifndef CRAVEN_PACKET_H
#define CRAVEN_PACKET_H

#include "utils.h"

#define CR_MSG 1
#define CR_META 2
#define CR_CONN 3
#define CR_KILL 4
#define CR_NAME 5

#define CR_PACKET_SIZE 256
#define CR_MSG_LEN 64

#define CR_TIME_HMS_LEN 9

// Use this for recv() calls, then use type field to deduce type and cast
// to appropriate sub-struct
struct cr_packet {
	char type;
	char data[CR_PACKET_SIZE - (sizeof (char))];
};

// For sending text message
struct cr_msg {
	char type;
	char time[CR_TIME_HMS_LEN];
	char message[CR_MSG_LEN];
};

// Should be sent after a successfully connecting to a new peer.
// Set forward to 1 if the meta receiver should forward this information
// onto their other peers, 0 if not.
struct cr_meta {
	char type;
	unsigned short l_port;
	char name[NAME_LEN];
	char forward;
};

// Forward a connection onto other peers
struct cr_conn {
	char type;
	char addr[ADDR_LEN];
	unsigned short port;
};

// Signals that the peer is disconnecting
struct cr_kill {
	char type;
};

// Signals name change
struct cr_name {
	char type;
	char name[NAME_LEN];
};

size_t cr_msg_size(struct cr_msg* crm)
{
	size_t msg_size = strlen(crm->message) + 1; // + '\0'
	return (sizeof (struct cr_msg)) - (CR_MSG_LEN - msg_size);
}

#endif //CRAVEN_PACKET_H
