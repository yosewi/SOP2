CC=gcc
CFLAGS= -std=gnu99 -Wall
LDFLAGS=-fsanitize=address,undefined -fanalyzer

# Lista programów do zbudowania
TARGETS=prog21a_s prog21b_s prog21_c

.PHONY: all clean

# Domyślny cel - buduje wszystkie programy z listy
all: $(TARGETS)

# Reguła wzorcowa: tworzenie pliku wykonywalnego z pliku .o
%: %.o
	$(CC) $(LDFLAGS) -o $@ $^

# Reguła wzorcowa: tworzenie pliku obiektu .o z kodu źródłowego .c
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGETS) *.o