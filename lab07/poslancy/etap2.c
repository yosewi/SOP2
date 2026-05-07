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
#define MAX_EVENTS 10

void usage(char* name)
{
    fprintf(stderr, "USAGE: %s port\n", name);
    exit(EXIT_FAILURE);
}

int main(int argc, char** argv){

    if(argc != 2){
        usage(argv[0]);
    }

    int maxclients = 4;
    int current_clients = 0;
    uint16_t port = atoi(argv[1]);
    int sfd = bind_tcp_socket(port, 10);
    if(sfd < 0){
        ERR("bind_tcp_socket");
    }    

    int epoll_fd = epoll_create1(0);
    if(epoll_fd < 0){
        ERR("epoll_create1");
    }

    struct epoll_event ev, events[MAX_EVENTS];
    ev.events = EPOLLIN;
    ev.data.fd = sfd;
    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sfd, &ev)){
        ERR("epoll_ctl");
    }

    while(1){
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if(nfds < 0){
            ERR("epoll_wait");
        }

        for(int i = 0;i<nfds;i++){
            int fd = events[i].data.fd;

            if(fd==sfd){
                if(current_clients < maxclients){
                    current_clients++;
                    int client_fd = add_new_client(sfd);
                    ev.events = EPOLLIN;
                     ev.data.fd = client_fd;
                    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev)){
                        ERR("epoll_ctl");
                    }

                    int flags = fcntl(client_fd, F_GETFL, 0) | O_NONBLOCK;
                    fcntl(client_fd, F_SETFL, flags);
                }
                else{
                    printf("za duzo klientow\n");
                    int rejected_fd = add_new_client(sfd);
                    if (rejected_fd >= 0) {
                        close(rejected_fd); 
                    }
                }
            }
            else{
                char buf[5];
                memset(buf, 0, sizeof(buf));
                ssize_t size = read(fd, buf, 4);
                if(size == 0){
                    current_clients--;
                    printf("Klient sie rozlaczyl\n");
                    close(fd);
                }
                if(size < 0){
                    current_clients--;
                    close(fd);
                    ERR("bulk_read");
                }
                if(size == 4){
                    printf("Dostalem 4 bajty: %s\n", buf);
                }
            }
        }
    }

    close(sfd);

    return EXIT_SUCCESS;
}