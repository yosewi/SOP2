override CFLAGS=-Wall -Wextra -Wshadow -g -O0 -fsanitize=address,undefined

ifdef CI
override CFLAGS=-Wall -Wextra -Werror
endif

.PHONY: clean all

all: poslancy

poslancy: poslancy.c
	gcc $(CFLAGS) -o poslancy poslancy.c

clean:
	rm -f poslancy
