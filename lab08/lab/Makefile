CC = gcc
CFLAGS = -Wall -Wextra -std=gnu11 -O2

TARGETS = server client_local

all: $(TARGETS)

server: l8-1_server.c	
	$(CC) $(CFLAGS) l8-1_server.c -o server	

client_local: l8-1_client.c	
	$(CC) $(CFLAGS) l8-1_client.c -o client_local	

clean:	
	rm -f $(TARGETS)	

.PHONY: all clean	
