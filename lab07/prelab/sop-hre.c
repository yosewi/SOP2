#include "l4-common.h"
#include <pthread.h>

#define MAX_EVENTS 10
#define ELECTORS_NUM 7
#define WELCOME_MSG "Welcome elector!\n"

typedef struct {
    int connected;
    int vote;
    int fd;
} client_state;

typedef struct {
    client_state* electors;
    pthread_mutex_t mutex;
    char* port;
} thread_state;

volatile sig_atomic_t do_work = 1;

void sigint_handler(int sig) {
    do_work = 0;
}

void usage(char* name)
{
    fprintf(stderr, "USAGE: %s port\n", name);
    exit(EXIT_FAILURE);
}

int make_socket(void)
{
    int sock;
    sock = socket(PF_INET, SOCK_DGRAM, 0);
    if (sock < 0)
        ERR("socket");
    return sock;
}

void do_server(int sfd, client_state electors[ELECTORS_NUM], pthread_mutex_t* mutex, sigset_t* oldmask) {
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) ERR("epoll_create1");
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = sfd;
    struct epoll_event events[MAX_EVENTS];
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sfd, &ev))
        ERR("epoll_ctl");

    while (do_work) {
        int nfds = epoll_pwait(epoll_fd, events, MAX_EVENTS, -1, oldmask);
        if (nfds < 0) {
            if (errno != EINTR)
                ERR("epoll_wait");
        }
        for (int i = 0; i < nfds; i++) {
            int fd = events[i].data.fd;
            if (fd == sfd) { // socket fd - new client
                int client_fd = add_new_client(sfd);
                ev.events = EPOLLIN;
                ev.data.fd = client_fd;
                if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev))
                    ERR("epoll_ctl");

                int flags = fcntl(client_fd, F_GETFL, 0) | O_NONBLOCK;
                fcntl(client_fd, F_SETFL, flags);
            }
            else { // client messaged us
                char msg;
                ssize_t n = read(fd, &msg, 1);
                if (n < 0) ERR("read");
                if (n == 0) {
                    printf("client disconnected\n");
                    for (int j = 0; j < ELECTORS_NUM; j++) {
                        if (electors[j].fd == fd) {
                            electors[j].connected = 0;
                            electors[j].fd = -1;
                        }
                    }
                    close(fd);
                }
                int registered_elector = 0;
                for (int j = 0; j < ELECTORS_NUM; j++) {
                    if (electors[j].fd == fd) {
                        if (msg >= '0' && msg <= '2') {
                            pthread_mutex_lock(mutex);
                            electors[j].vote = msg - '0';
                            pthread_mutex_unlock(mutex);
                            printf("Elector %d voted %d\n", j, electors[j].vote);
                        }
                        registered_elector = 1;
                        break;
                    }
                }
                if (!registered_elector) {
                    if (msg >= '1' && msg <= '7') {
                        int index = msg - '1';
                        if (!electors[index].connected) {
                            electors[index].connected = 1;
                            electors[index].fd = fd;
                            ev.events = EPOLLIN;
                            ev.data.fd = fd;
                            if (write(fd, WELCOME_MSG, sizeof(WELCOME_MSG)) < 0)
                                ERR("write");
                        } else {
                            close(fd);
                        }
                    }
                }
            }
        }
    }

    for (int i = 0; i < ELECTORS_NUM; i++) {
        if (electors[i].connected) {
            close(electors[i].fd);
        }
    }
}

void* election_broadcast_thread(void* thread_args) {
    thread_state* state = (thread_state*) thread_args;

    int sfd = make_socket();
    struct sockaddr_in addr = make_address("localhost", state->port);

    while (do_work) {
        sleep(1);
        pthread_mutex_lock(&state->mutex);

        for (int i = 0; i < ELECTORS_NUM; i++) {
            if (state->electors[i].vote >= 0) {
                char message[256];
                snprintf(message, sizeof(message), "Elector %d voted %d\n", i, state->electors[i].vote);
                if (sendto(sfd, message, strlen(message), 0, (struct sockaddr*)&addr, sizeof(addr)) < 0)
                    ERR("sendto");
            }
        }

        pthread_mutex_unlock(&state->mutex);
    }

    return NULL;
}

int main(int argc, char** argv) {
    if (argc != 3)
        usage(argv[0]);

    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);

    sethandler(sigint_handler, SIGINT);

    uint16_t port = atoi(argv[1]);

    int sfd = bind_tcp_socket(port, 10);
    if (sfd < 0) ERR("bind_tcp_socket");

    client_state electors[ELECTORS_NUM];
    for (int i = 0; i < ELECTORS_NUM; i++) {
        electors[i].connected = 0;
        electors[i].vote = -1;
        electors[i].fd = -1;
    }
    thread_state state;
    state.electors = electors;
    pthread_mutex_init(&state.mutex, NULL);
    state.port = argv[2];

    pthread_t thread_id;
    if (pthread_create(&thread_id, NULL, election_broadcast_thread, &state) < 0)
        ERR("pthread_create");

    do_server(sfd, electors, &state.mutex, &oldmask);

    pthread_join(thread_id, NULL);

    pthread_mutex_destroy(&state.mutex);

    for (int i = 0; i < ELECTORS_NUM; i++) {
        if (state.electors[i].vote >= 0) {
            printf("Elector %d voted %d\n", i, state.electors[i].vote);
        }
    }

    close(sfd);
}
