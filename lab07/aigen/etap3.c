#include "common.h"

#define MAX_EVENTS 10
#define MAX_CLIENTS 10

typedef struct {
    int fd;
    char family;
    char name[16];
} client_state;

volatile sig_atomic_t do_work = 1;

void sigint_handler(int sig) { 
    do_work = 0; 
}

void usage(char* name) {
    fprintf(stderr, "USAGE: %s local_socket_name\n", name);
    exit(EXIT_FAILURE);
}

void remove_client(int fd, client_state clients[]) {
    for(int i = 0; i < MAX_CLIENTS; i++) {
        if(clients[i].fd == fd) {
            clients[i].fd = -1;
            clients[i].family = 0;
            memset(clients[i].name, 0, 16);
            break;
        }
    }
    close(fd);
}

int main(int argc, char** argv) {
    if(argc != 2) usage(argv[0]);

    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);
    sethandler(sigint_handler, SIGINT);
    sethandler(SIG_IGN, SIGPIPE);

    client_state clients[MAX_CLIENTS];
    for(int i = 0; i < MAX_CLIENTS; i++) {
        clients[i].fd = -1;
        clients[i].family = 0;
    }

    int sfd = bind_local_socket(argv[1], 10);
    if(sfd < 0) ERR("bind_local_socket");

    int epoll_fd = epoll_create1(0);
    if(epoll_fd < 0) ERR("epoll_create1");

    struct epoll_event ev, events[MAX_EVENTS];
    ev.events = EPOLLIN;
    ev.data.fd = sfd;
    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sfd, &ev)) ERR("epoll_ctl");

    while(do_work) {
        int nfds = epoll_pwait(epoll_fd, events, MAX_EVENTS, -1, &oldmask);
        if(nfds < 0) {
            if(errno != EINTR) ERR("epoll_pwait");
            continue;
        }

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
                    remove_client(fd, clients);
                } 
                else {
                    if(buf[0] != 'M' && buf[0] != 'C') {
                        remove_client(fd, clients);
                        continue;
                    }

                    // --- LOGIKA ŁĄCZENIA (MATCHMAKING) ---
                    int matched = 0;
                    for(int j = 0; j < MAX_CLIENTS; j++) {
                        // Szukamy kogoś z PRZECIWNEGO rodu
                        if(clients[j].fd != -1 && clients[j].family != 0 && clients[j].family != buf[0]) {
                            
                            char msg_to_new[32], msg_to_waiting[32];
                            snprintf(msg_to_new, sizeof(msg_to_new), "MATCH: %s", clients[j].name);
                            snprintf(msg_to_waiting, sizeof(msg_to_waiting), "MATCH: %s", buf);

                            // Piszemy do obu
                            if(write(fd, msg_to_new, strlen(msg_to_new)) < 0 && errno != EPIPE) ERR("write");
                            if(write(clients[j].fd, msg_to_waiting, strlen(msg_to_waiting)) < 0 && errno != EPIPE) ERR("write");

                            printf("Paczka dostarczona. Rozlaczam pare: %s oraz %s", buf, clients[j].name);
                            
                            // Rozłączamy obu (nowy nie trafia do tablicy, starego usuwamy)
                            remove_client(clients[j].fd, clients);
                            close(fd);
                            
                            matched = 1;
                            break;
                        }
                    }

                    if(!matched) {
                        // Nie ma nikogo z przeciwnego rodu -> klient ląduje w poczekalni
                        for(int j = 0; j < MAX_CLIENTS; j++) {
                            if(clients[j].fd == -1) {
                                clients[j].fd = fd;
                                clients[j].family = buf[0];
                                strncpy(clients[j].name, buf, 15);
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    // --- SPRZĄTANIE PO SIGINT ---
    int m_count = 0, c_count = 0;
    for(int i = 0; i < MAX_CLIENTS; i++) {
        if(clients[i].fd != -1) {
            if(clients[i].family == 'M') m_count++;
            if(clients[i].family == 'C') c_count++;
            close(clients[i].fd);
        }
    }
    printf("\nSerwer zakonczyl prace. Czekajacych Montekich: %d, Kapuletow: %d\n", m_count, c_count);

    close(epoll_fd);
    close(sfd);
    unlink(argv[1]);
    return EXIT_SUCCESS;
}