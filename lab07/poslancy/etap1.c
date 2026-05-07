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

    uint16_t port = atoi(argv[1]);
    int sfd = bind_tcp_socket(port, 10);
    if(sfd < 0){
        ERR("bind_tcp_socket");
    }    

    int client_fd = add_new_client(sfd);
    if(client_fd >= 0){
        printf("Klient polaczony\n");
        char buf[5];
        memset(buf, 0, sizeof(buf));
        ssize_t size = bulk_read(client_fd, buf, 4);
        if(size == 0){
            close(client_fd);
        }
        if(size < 0){
            close(client_fd);
            ERR("bulk_read");
        }
        if(size == 4){
            printf("Dostalem 4 bajty: %s\n", buf);
            close(client_fd);
        }
    }

    close(sfd);

    return EXIT_SUCCESS;
}