CC=gcc

send: cchat_send.c
	gcc -o bin/cchat-send cchat_send.c

recv: cchat_recv.c
	gcc -o bin/cchat-recv cchat_recv.c

all: send recv
