CC = gcc
CFLAGS = -Wall -Wextra
LDFLAGS = -lrt

TARGETS = l6-2_client l6-2_server

.PHONY: all clean

all: $(TARGETS)

%: %.c
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

clean:
	rm -f $(TARGETS) *.o data.bin