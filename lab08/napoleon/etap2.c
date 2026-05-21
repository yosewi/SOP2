#include "common.h"
#include <pthread.h>

#define MAX_EVENTS 10
#define STACK_SIZE 16
#define WORKERS_COUNT 4

typedef struct {
    char buf[512];
} message_t;

typedef struct {
    message_t stack[STACK_SIZE];
    int top; // Wskaźnik na wierzch stosu (ilość elementów)
    pthread_mutex_t mutex;
    pthread_cond_t cond;
} app_state;

volatile sig_atomic_t do_work = 1;
void sigint_handler(int sig) { do_work = 0; }

void usage(char *name) {
    fprintf(stderr, "USAGE: %s port\n", name);
    exit(EXIT_FAILURE);
}

int bind_udp_socket(uint16_t port) {
    int socketfd = socket(PF_INET, SOCK_DGRAM, 0);
    if (socketfd < 0) ERR("socket");
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    int t = 1;
    if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &t, sizeof(t))) ERR("setsockopt");
    if (bind(socketfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) ERR("bind");
    return socketfd;
}

void* adjutant_thread(void* arg) {
    app_state* state = (app_state*)arg;

    while (1) {
        pthread_mutex_lock(&state->mutex);
        // Zmienna warunkowa - czekamy aż będzie praca (top > 0) LUB nadejdzie SIGINT (!do_work)
        while (do_work && state->top == 0) {
            pthread_cond_wait(&state->cond, &state->mutex);
        }
        
        // Warunek wyjścia z wątku po naciśnięciu Ctrl+C
        if (!do_work && state->top == 0) {
            pthread_mutex_unlock(&state->mutex);
            break;
        }

        // Zdejmujemy raport ze stosu
        state->top--;
        message_t msg = state->stack[state->top];
        pthread_mutex_unlock(&state->mutex);

        int x, y, p;
        char div_name[129];
        memset(div_name, 0, sizeof(div_name));

        if (sscanf(msg.buf, "%d %d %d %128[^\n]", &x, &y, &p, div_name) == 4) {
            if (x >= 0 && x <= 99 && y >= 0 && y <= 99 && (p == 0 || p == 1)) {
                const char* type = (p == 1) ? "Nasz" : "Wrogi";
                printf("%s oddział %s był widziany na pozycji %d:%d\n", type, div_name, x, y);
            } else {
                fprintf(stderr, "Błąd: Nieprawidłowe wartości w meldunku (X/Y/P).\n");
            }
        } else {
            fprintf(stderr, "Błąd: Źle sformatowana wiadomość.\n");
        }
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc != 2) usage(argv[0]);

    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);
    sethandler(sigint_handler, SIGINT);

    app_state state;
    state.top = 0;
    pthread_mutex_init(&state.mutex, NULL);
    pthread_cond_init(&state.cond, NULL);

    pthread_t workers[WORKERS_COUNT];
    for (int i = 0; i < WORKERS_COUNT; i++) {
        if (pthread_create(&workers[i], NULL, adjutant_thread, &state) != 0) ERR("pthread_create");
    }

    uint16_t port = atoi(argv[1]);
    int udp_sfd = bind_udp_socket(port);
    int flags = fcntl(udp_sfd, F_GETFL, 0) | O_NONBLOCK;
    fcntl(udp_sfd, F_SETFL, flags);

    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) ERR("epoll_create1");

    struct epoll_event ev, events[MAX_EVENTS];
    ev.events = EPOLLIN;
    ev.data.fd = udp_sfd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, udp_sfd, &ev)) ERR("epoll_ctl");

    char buf[512];
    struct sockaddr_in client_addr;
    socklen_t client_len;

    while (do_work) {
        int nfds = epoll_pwait(epoll_fd, events, MAX_EVENTS, -1, &oldmask);
        if (nfds < 0) {
            if (errno != EINTR) ERR("epoll_pwait");
            continue;
        }

        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == udp_sfd) {
                client_len = sizeof(client_addr);
                memset(buf, 0, sizeof(buf));
                ssize_t size = recvfrom(udp_sfd, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&client_addr, &client_len);
                
                if (size > 0) {
                    // Dodajemy meldunek na stos (Krytyczna sekcja)
                    pthread_mutex_lock(&state.mutex);
                    if (state.top < STACK_SIZE) {
                        strncpy(state.stack[state.top].buf, buf, sizeof(state.stack[state.top].buf) - 1);
                        state.top++;
                        pthread_cond_signal(&state.cond); // Wybudzenie jednego adiutanta
                    } else {
                        fprintf(stderr, "Stos pełny! Meldunek posłańca przepadł w chaosie bitwy.\n");
                    }
                    pthread_mutex_unlock(&state.mutex);
                }
            }
        }
    }

    // Bezpieczne kończenie pracy (wybudzenie czekających wątków po zmianie do_work na 0)
    pthread_mutex_lock(&state.mutex);
    pthread_cond_broadcast(&state.cond);
    pthread_mutex_unlock(&state.mutex);

    for (int i = 0; i < WORKERS_COUNT; i++) pthread_join(workers[i], NULL);

    pthread_mutex_destroy(&state.mutex);
    pthread_cond_destroy(&state.cond);
    close(epoll_fd);
    close(udp_sfd);
    return EXIT_SUCCESS;
}