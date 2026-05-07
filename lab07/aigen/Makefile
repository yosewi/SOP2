override CFLAGS=-Wall -Wextra -Wshadow -g -O0 -fsanitize=address,undefined

ifdef CI
override CFLAGS=-Wall -Wextra -Werror
endif

.PHONY: clean all

all: aigen

aigen: aigen.c
	gcc $(CFLAGS) -o aigen aigen.c

clean:
	rm -f aigen
