override CFLAGS=-Wall -Wextra -fanalyzer -g -O0 -fsanitize=address,undefined

ifdef CI
override CFLAGS=-Wall -Wextra -Werror
endif

.PHONY: all clean

all: ops-double-processor

channel.o: channel.c channel.h macros.h
	gcc $(CFLAGS) -c -o $@ $<

ops-double-processor: ops-double-processor.c channel.o macros.h
	gcc $(CFLAGS) -o $@ $< channel.o

clean:
	rm -f  *.a *.o ops-double-processor
