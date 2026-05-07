CC = gcc
CFLAGS = -Wall -Wextra -std=gnu11 -O2

TARGETS = server client_local client_tcp

all: $(TARGETS)

server: l7-1_server.c	
	$(CC) $(CFLAGS) l7-1_server.c -o server	

client_local: l7-1_client_local.c	
	$(CC) $(CFLAGS) l7-1_client_local.c -o client_local	

client_tcp: l7-1_client_tcp.c	
	$(CC) $(CFLAGS) l7-1_client_tcp.c -o client_tcp	

clean:	
	rm -f $(TARGETS)	

.PHONY: all clean	
