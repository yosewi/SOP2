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

typedef struct {
    char owner;
    int num;
} city;

void usage(char* name)
{
    fprintf(stderr, "USAGE: %s port\n", name);
    exit(EXIT_FAILURE);
}

int main(int argc, char** argv){

    if(argc != 2){
        usage(argv[0]);
    }

    city cities[20];
    for(int i = 0;i<20;i++){
        cities[i].owner = 'g';
        cities[i].num = i+1;
    }

    int clients[4];
    for(int i = 0; i < 4; i++) {
        clients[i] = -1; 
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
                    int client_fd = add_new_client(sfd);
                    for(int j = 0;j<4;j++){
                        if(clients[j] == -1){
                            clients[j] = client_fd;
                            break;
                        }
                    }
                    current_clients++;
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
                if(size <= 0){
                    if (size == 0) printf("Klient sie rozlaczyl\n");
                    for(int j = 0; j < 4; j++){
                        if(clients[j] == fd){
                            clients[j] = -1;
                            break;
                       }
                    }
                    current_clients--;
                    close(fd);
                }
                if(size == 4){
                    printf("Dostalem 4 bajty: %s\n", buf);
                    if(buf[1] >= '0' && buf[1] <= '2' && buf[2] <= '9' && buf[2] >= '0'){
                        int city_index = (buf[1] - '0') * 10 + (buf[2]- '0');
                        char new_owner = buf[0];

                        if(city_index >= 1 && city_index <= 20){
                            if(cities[city_index-1].owner != new_owner){
                                cities[city_index-1].owner = new_owner;
                                for(int k =0;k<4;k++){
                                    if(clients[k] != -1 && clients[k] != fd){
                                        write(clients[k], buf, 4);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    close(epoll_fd);
    close(sfd);

    return EXIT_SUCCESS;
}