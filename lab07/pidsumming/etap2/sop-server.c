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
#define PID_LENGTH 11

void usage(char* name)
{
    fprintf(stderr, "USAGE: %s port\n", name);
    exit(EXIT_FAILURE);
}

int main(int argc, char** argv){
    
    if(argc != 2){
        usage(argv[0]);
    }

    int sfd = bind_tcp_socket(atoi(argv[1]), 10);
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

        for(int i = 0; i < nfds; i++){
            int fd = events[i].data.fd;

            if (fd == sfd) {
                int client_socket = add_new_client(sfd);
                if (client_socket >= 0) {
                    ev.events = EPOLLIN;
                    ev.data.fd = client_socket;
                    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_socket, &ev)){
                        ERR("epoll_ctl");
                    }
                }
            } 
            else {
                char buf[PID_LENGTH];
                memset(buf, 0, sizeof(buf));
                
                ssize_t size = bulk_read(fd, buf, PID_LENGTH);
                
                if (size < 0) {
                    ERR("read:");
                } 
                else if (size == 0) {
                    close(fd);
                } 
                else if (size == PID_LENGTH) {
                    printf("Otrzymano pid od klienta: %s\n", buf);
                    int sum = 0;
                    
                    for(int j = 0; j < PID_LENGTH && buf[j] != '\0'; j++){
                        if (buf[j] >= '0' && buf[j] <= '9') {
                            sum += buf[j] - '0';
                        }
                    }
                    
                    int16_t result = htons((int16_t)sum);
                    
                    if (bulk_write(fd, (char*)&result, sizeof(int16_t)) < 0 && errno != EPIPE) {
                        ERR("write:");
                    }
                    
                    if (TEMP_FAILURE_RETRY(close(fd)) < 0)
                        ERR("close");
                }
            }
        }
    }
    close(epoll_fd);
    close(sfd);
    return EXIT_SUCCESS;
}