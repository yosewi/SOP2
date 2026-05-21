#include "common.h"
#include <pthread.h>
#include <time.h>

#define MAX_EVENTS 10
#define STACK_SIZE 16
#define WORKERS_COUNT 4
#define DIVISION_NAMES_SIZE 128

typedef struct {
    char buf[512];
    struct sockaddr_in addr; // Oprócz wiadomości zachowujemy adres zwrotny posłańca
} message_t;

typedef struct {
    message_t stack[STACK_SIZE];
    int top;
    pthread_mutex_t stack_mutex;
    pthread_cond_t stack_cond;

    char div_names[DIVISION_NAMES_SIZE][129];
    struct sockaddr_in div_addr[DIVISION_NAMES_SIZE]; // Adres posłańca dla każdego oddziału
    int div_p[DIVISION_NAMES_SIZE]; // 1 = Nasz, 0 = Wrogi
    int num_divs;
    pthread_mutex_t div_mutex;

    int map[100][100];
    pthread_mutex_t row_mutexes[100];
    
    int udp_fd; // Wątek Napoleona musi mieć dostęp do gniazda, żeby odsyłać rozkazy
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
                
                usleep(10000); // Adiutant pracuje (10ms)

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
                // Jeśli oddział ma ID, rejestrujemy dla niego adres powrotny (dla Napoleona)
                if (div_idx != -1) {
                    state->div_addr[div_idx] = msg.addr;
                    state->div_p[div_idx] = p;
                }
                pthread_mutex_unlock(&state->div_mutex);

                // Przenoszenie pionków na planszy (mapie)
                if (div_idx != -1) {
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
                        if (found) break;
                    }

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

// Wątek cesarski Napoleona
void* napoleon_thread(void* arg) {
    app_state* state = (app_state*)arg;
    
    while (do_work) {
        usleep(30000); // Napoleon lustruje mapę co 30ms
        
        printf("\n--- CESARSKA MAPA SZTABOWA ---\n");
        for (int r = 0; r < 100; r++) {
            pthread_mutex_lock(&state->row_mutexes[r]);
            for (int c = 0; c < 100; c++) {
                if (state->map[r][c] != -1) {
                    printf("  Pozycja [%d:%d] zajęta przez jednostkę ID %d\n", r, c, state->map[r][c]);
                }
            }
            pthread_mutex_unlock(&state->row_mutexes[r]);
        }

        // Filtrowanie wyłącznie sojuszniczych oddziałów
        pthread_mutex_lock(&state->div_mutex);
        int allies[DIVISION_NAMES_SIZE];
        int allies_count = 0;
        
        for (int i = 0; i < state->num_divs; i++) {
            if (state->div_p[i] == 1) {
                allies[allies_count++] = i; // Zapisujemy indeksy sojuszników
            }
        }

        if (allies_count > 0) {
            // Cesarz wybiera jeden losowy oddział
            int random_idx = rand() % allies_count;
            int target_div = allies[random_idx];
            
            struct sockaddr_in target_addr = state->div_addr[target_div];
            char target_name[129];
            strncpy(target_name, state->div_names[target_div], 128);
            pthread_mutex_unlock(&state->div_mutex); // Zwalniamy muteks przed wysyłką do sieci

            // Wydawanie rozkazu (Losowa pozycja na mapie X, Y)
            int new_x = rand() % 100;
            int new_y = rand() % 100;
            char order[256];
            snprintf(order, sizeof(order), "%d %d 1 %s\n", new_x, new_y, target_name);
            
            // Wysłanie datagramu zwrotnego do posłańca z rozkazem 
            if (sendto(state->udp_fd, order, strlen(order), 0, (struct sockaddr*)&target_addr, sizeof(target_addr)) < 0) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) ERR("sendto");
            }
            printf("[NAPOLEON] Wydano rozkaz ruchu: '%s'\n", order);
        } else {
            pthread_mutex_unlock(&state->div_mutex);
        }
    }
    return NULL;
}

int main(int argc, char **argv) {
    if (argc != 2) usage(argv[0]);

    srand(time(NULL));
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
        for (int j = 0; j < 100; j++) state.map[i][j] = -1;
    }

    uint16_t port = atoi(argv[1]);
    state.udp_fd = bind_udp_socket(port);
    int flags = fcntl(state.udp_fd, F_GETFL, 0) | O_NONBLOCK;
    fcntl(state.udp_fd, F_SETFL, flags);

    // Odpalenie adiutantów
    pthread_t workers[WORKERS_COUNT];
    for (int i = 0; i < WORKERS_COUNT; i++) {
        if (pthread_create(&workers[i], NULL, adjutant_thread, &state) != 0) ERR("pthread_create");
    }

    // Wkracza Cesarz
    pthread_t napoleon_th;
    if (pthread_create(&napoleon_th, NULL, napoleon_thread, &state) != 0) ERR("pthread_create");

    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) ERR("epoll_create1");

    struct epoll_event ev, events[MAX_EVENTS];
    ev.events = EPOLLIN;
    ev.data.fd = state.udp_fd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, state.udp_fd, &ev)) ERR("epoll_ctl");

    struct sockaddr_in client_addr;
    socklen_t client_len;

    while (do_work) {
        int nfds = epoll_pwait(epoll_fd, events, MAX_EVENTS, -1, &oldmask);
        if (nfds < 0) {
            if (errno != EINTR) ERR("epoll_pwait");
            continue;
        }

        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == state.udp_fd) {
                client_len = sizeof(client_addr);
                message_t msg;
                memset(msg.buf, 0, sizeof(msg.buf));
                
                ssize_t size = recvfrom(state.udp_fd, msg.buf, sizeof(msg.buf) - 1, 0, (struct sockaddr *)&client_addr, &client_len);
                
                if (size > 0) {
                    msg.addr = client_addr; // Odbieramy adres posłańca
                    pthread_mutex_lock(&state.stack_mutex);
                    if (state.top < STACK_SIZE) {
                        state.stack[state.top] = msg;
                        state.top++;
                        pthread_cond_signal(&state.stack_cond);
                    } else {
                        fprintf(stderr, "Stos pełny! Odrzucono meldunek z pola bitwy.\n");
                    }
                    pthread_mutex_unlock(&state.stack_mutex);
                }
            }
        }
    }

    // Bezpieczne zatrzymywanie wszystkich wątków
    pthread_mutex_lock(&state.stack_mutex);
    pthread_cond_broadcast(&state.stack_cond);
    pthread_mutex_unlock(&state.stack_mutex);

    for (int i = 0; i < WORKERS_COUNT; i++) pthread_join(workers[i], NULL);
    pthread_join(napoleon_th, NULL);

    pthread_mutex_destroy(&state.stack_mutex);
    pthread_cond_destroy(&state.stack_cond);
    pthread_mutex_destroy(&state.div_mutex);
    for (int i = 0; i < 100; i++) pthread_mutex_destroy(&state.row_mutexes[i]);
    
    close(epoll_fd);
    close(state.udp_fd);
    return EXIT_SUCCESS;
}