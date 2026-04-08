CC=gcc
C_FLAGS=-Wall -g -Wno-unused
L_FLAGS=-fsanitize=address,undefined

TARGET=calka
FILES=${TARGET}.o

.PHONY: clean all

${TARGET} : ${FILES}
	${CC} ${L_FLAGS} -o ${TARGET}.exe ${FILES} -lm -pthread -lrt

${TARGET}.o: ${TARGET}.c
	${CC} ${C_FLAGS} -o ${TARGET}.o -c ${TARGET}.c

all: ${TARGET}

clean:
	rm -f ${FILES} ${TARGET}.exe