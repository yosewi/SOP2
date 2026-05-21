CC = gcc
CFLAGS = -Wall -Wextra -std=gnu11 -O2

TARGETS = napoleon

all: $(TARGETS)

napoleon: napoleon.c	
	$(CC) $(CFLAGS) napoleon.c -o napoleon	

clean:	
	rm -f $(TARGETS)	

.PHONY: all clean	
