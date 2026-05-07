#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "common.h"

#define PID_LENGTH 11

void usage(char* name)
{
    fprintf(stderr, "USAGE: %s port\n", name);
    exit(EXIT_FAILURE);
}

int main(int argc, char** argv){
    
    if(argc != 3){
        usage(argv[0]);
    }

    pid_t pid = getpid();
    int16_t answer;
    char buffer[PID_LENGTH];
    if(snprintf(buffer, PID_LENGTH, "%d", pid) < 0){
        ERR("snprintf");
    }

    int client_socket = connect_tcp_socket(argv[1], argv[2]);

    if (bulk_write(client_socket, buffer, PID_LENGTH) < 0)
        ERR("write:");

    if(bulk_read(client_socket, (char*)&answer,  sizeof(int16_t)) < (int)sizeof(int16_t)){
        ERR("bulk_read");
    }

    printf("[%d]: SUM = %d\n", getpid(), ntohs(answer));

    if (TEMP_FAILURE_RETRY(close(client_socket)) < 0)
        ERR("close");
    return EXIT_SUCCESS;
}