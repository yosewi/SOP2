#include "common.h"

#define MAX_EVENTS 10

void usage(char* name) {
    fprintf(stderr, "USAGE: %s local_socket_name\n", name);
    exit(EXIT_FAILURE);
}

int main(int argc, char** argv) {
    if(argc != 2) usage(argv[0]);

    int sfd = bind_local_socket(argv[1], 10);
    if(sfd < 0) ERR("bind_local_socket");

    int epoll_fd = epoll_create1(0);
    if(epoll_fd < 0) ERR("epoll_create1");

    struct epoll_event ev, events[MAX_EVENTS];
    ev.events = EPOLLIN;
    ev.data.fd = sfd;
    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sfd, &ev)) ERR("epoll_ctl");

    while(1) {
        int nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if(nfds < 0) ERR("epoll_wait");

        for(int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;

            if(fd == sfd) {
                int client_fd = add_new_client(sfd);
                if(client_fd >= 0) {
                    ev.events = EPOLLIN;
                    ev.data.fd = client_fd;
                    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev)) ERR("epoll_ctl");
                    
                    int flags = fcntl(client_fd, F_GETFL, 0) | O_NONBLOCK;
                    fcntl(client_fd, F_SETFL, flags);
                }
            } 
            else {
                char buf[16];
                memset(buf, 0, sizeof(buf));
                
                ssize_t size = read(fd, buf, sizeof(buf) - 1);
                if(size <= 0) {
                    close(fd);
                } 
                else {
                    // Walidacja rodów (char comparison)
                    if(buf[0] != 'M' && buf[0] != 'C') {
                        printf("Intruz odrzucony: %s", buf);
                        close(fd); // Natychmiastowe wyrzucenie intruza
                    } else {
                        printf("Czeka na pare: %s", buf);
                    }
                }
            }
        }
    }

    close(sfd);
    unlink(argv[1]);
    return EXIT_SUCCESS;
}