override CFLAGS=-Wall -Wextra -Wshadow -g -O0 -fsanitize=address,undefined

ifdef CI
override CFLAGS=-Wall -Wextra -Werror
endif

.PHONY: clean all

all: sop-hre

sop-hre: sop-hre.c
	gcc $(CFLAGS) -o sop-hre sop-hre.c

clean:
	rm -f sop-hre
