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

void usage(char* name)
{
    fprintf(stderr, "USAGE: %s port\n", name);
    exit(EXIT_FAILURE);
}

int main(int argc, char** argv){
    
    if(argc != 2){
        usage(argv[0]);
    }

    int socket;
    socket = bind_tcp_socket(atoi(argv[1]), 10);
    if(socket < 0){
        ERR("bind_tcp_socket");
    }
    int client_fd = add_new_client(socket);
    if(client_fd >= 0){
        char buf[32];
        if(bulk_read(client_fd, buf, sizeof(buf)) > 0){
            printf("Otrzymano pid od klienta: %s\n", buf);
        }
        close(client_fd);
    }
    close(socket);
    return EXIT_SUCCESS;
}