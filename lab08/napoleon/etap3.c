#include "common.h"
#include <pthread.h>

#define MAX_EVENTS 10
#define STACK_SIZE 16
#define WORKERS_COUNT 4
#define DIVISION_NAMES_SIZE 128

typedef struct {
    char buf[512];
} message_t;

typedef struct {
    message_t stack[STACK_SIZE];
    int top;
    pthread_mutex_t stack_mutex;
    pthread_cond_t stack_cond;

    // Tablica jednostek
    char div_names[DIVISION_NAMES_SIZE][129];
    int num_divs;
    pthread_mutex_t div_mutex; // Chroni tablicę jednostek

    // Mapa sztabowa
    int map[100][100];
    pthread_mutex_t row_mutexes[100]; // 100 muteksów chroniących niezależnie każdy wiersz
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
        pthread_mutex_lock(&state->stack_mutex);
        while (do_work && state->top == 0) {
            pthread_cond_wait(&state->stack_cond, &state->stack_mutex);
        }
        if (!do_work && state->top == 0) {
            pthread_mutex_unlock(&state->stack_mutex);
            break;
        }

        state->top--;
        message_t msg = state->stack[state->top];
        pthread_mutex_unlock(&state->stack_mutex);

        int x, y, p;
        char div_name[129];
        memset(div_name, 0, sizeof(div_name));

        if (sscanf(msg.buf, "%d %d %d %128[^\n]", &x, &y, &p, div_name) == 4) {
            if (x >= 0 && x <= 99 && y >= 0 && y <= 99 && (p == 0 || p == 1)) {
                
                usleep(10000); // 10ms pracy z mapami (według wymogów Etapu 3)
                
                const char* type = (p == 1) ? "Nasz" : "Wrogi";
                printf("%s oddział %s był widziany na pozycji %d:%d\n", type, div_name, x, y);

                // Rejestracja lub wyszukanie oddziału
                pthread_mutex_lock(&state->div_mutex);
                int div_idx = -1;
                for (int i = 0; i < state->num_divs; i++) {
                    if (strcmp(state->div_names[i], div_name) == 0) {
                        div_idx = i;
                        break;
                    }
                }
                if (div_idx == -1 && state->num_divs < DIVISION_NAMES_SIZE) {
                    div_idx = state->num_divs;
                    strncpy(state->div_names[div_idx], div_name, 128);
                    state->num_divs++;
                }
                pthread_mutex_unlock(&state->div_mutex);

                if (div_idx != -1) {
                    // Usuwamy starą pozycję (skan całej mapy wiersz po wierszu)
                    int found = 0;
                    for (int r = 0; r < 100; r++) {
                        pthread_mutex_lock(&state->row_mutexes[r]);
                        for (int c = 0; c < 100; c++) {
                            if (state->map[r][c] == div_idx) {
                                state->map[r][c] = -1;
                                found = 1;
                                break;
                            }
                        }
                        pthread_mutex_unlock(&state->row_mutexes[r]);
                        if (found) break; // Optymalizacja, znaleźliśmy go
                    }

                    // Wstawiamy oddział na nową pozycję
                    pthread_mutex_lock(&state->row_mutexes[x]);
                    state->map[x][y] = div_idx;
                    pthread_mutex_unlock(&state->row_mutexes[x]);
                }

            } else {
                fprintf(stderr, "Błąd: Nieprawidłowe wartości (X/Y/P).\n");
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
    state.num_divs = 0;
    pthread_mutex_init(&state.stack_mutex, NULL);
    pthread_cond_init(&state.stack_cond, NULL);
    pthread_mutex_init(&state.div_mutex, NULL);
    
    for (int i = 0; i < 100; i++) {
        pthread_mutex_init(&state.row_mutexes[i], NULL);
        for (int j = 0; j < 100; j++) state.map[i][j] = -1; // Wypełniamy puste miejsca -1
    }

    uint16_t port = atoi(argv[1]);
    int udp_sfd = bind_udp_socket(port);
    int flags = fcntl(udp_sfd, F_GETFL, 0) | O_NONBLOCK;
    fcntl(udp_sfd, F_SETFL, flags);

    pthread_t workers[WORKERS_COUNT];
    for (int i = 0; i < WORKERS_COUNT; i++) {
        if (pthread_create(&workers[i], NULL, adjutant_thread, &state) != 0) ERR("pthread_create");
    }

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
                    pthread_mutex_lock(&state.stack_mutex);
                    if (state.top < STACK_SIZE) {
                        strncpy(state.stack[state.top].buf, buf, sizeof(state.stack[state.top].buf) - 1);
                        state.top++;
                        pthread_cond_signal(&state.stack_cond);
                    }
                    pthread_mutex_unlock(&state.stack_mutex);
                }
            }
        }
    }

    pthread_mutex_lock(&state.stack_mutex);
    pthread_cond_broadcast(&state.stack_cond);
    pthread_mutex_unlock(&state.stack_mutex);

    for (int i = 0; i < WORKERS_COUNT; i++) pthread_join(workers[i], NULL);

    pthread_mutex_destroy(&state.stack_mutex);
    pthread_cond_destroy(&state.stack_cond);
    pthread_mutex_destroy(&state.div_mutex);
    for (int i = 0; i < 100; i++) pthread_mutex_destroy(&state.row_mutexes[i]);
    close(epoll_fd);
    close(udp_sfd);
    return EXIT_SUCCESS;
}