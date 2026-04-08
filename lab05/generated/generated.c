#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>
#include <fcntl.h>
#include <signal.h>

#define MAX_N 10

#define READ 0
#define WRITE 1

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), exit(EXIT_FAILURE))

// --- FLAGI I SYGNAŁY ---
volatile sig_atomic_t keep_running = 1;

void sig_handler(int sig) {
    keep_running = 0;
}

int set_handler(void (*f)(int), int sig) {
    struct sigaction act = {0};
    act.sa_handler = f;
    if (sigaction(sig, &act, NULL) == -1) return -1;
    return 0;
}

// --- FUNKCJE POMOCNICZE ---
void msleep(int millisec) {
    struct timespec tt;
    tt.tv_sec = millisec / 1000;
    tt.tv_nsec = (millisec % 1000) * 1000000;
    while (nanosleep(&tt, &tt) == -1 && errno == EINTR);
}

int count_descriptors() {
    int count = 0;
    DIR* dir;
    struct dirent* entry;
    struct stat stats;
    if ((dir = opendir("/proc/self/fd")) == NULL) ERR("opendir");
    char path[PATH_MAX];
    getcwd(path, PATH_MAX);
    chdir("/proc/self/fd");
    do {
        errno = 0;
        if ((entry = readdir(dir)) != NULL) {
            if (lstat(entry->d_name, &stats)) ERR("lstat");
            if (!S_ISDIR(stats.st_mode)) count++;
        }
    } while (entry != NULL);
    if (chdir(path)) ERR("chdir");
    if (closedir(dir)) ERR("closedir");
    return count - 1; 
}

// Ustawia potok w tryb nieblokujący (niezbędne do czytania z wielu wejść)
void set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) ERR("fcntl get");
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) ERR("fcntl set");
}

// --- STRUKTURA ---
typedef struct {
    int n;
    int matrix[MAX_N][MAX_N]; 
} graph_t;

// --- PARSOWANIE PLIKU ---
void load_graph(const char *filename, graph_t *graph) 
{
    FILE *file = fopen(filename, "r");
    if (file == NULL) ERR("fopen");

    if (fscanf(file, "%d", &graph->n) != 1) {
        fprintf(stderr, "Błąd wczytywania rozmiaru grafu.\n");
        fclose(file); exit(EXIT_FAILURE);
    }
    if (graph->n < 2 || graph->n > MAX_N) {
        fprintf(stderr, "Nieprawidlowy rozmiar grafu (2 <= N <= 10).\n");
        fclose(file); exit(EXIT_FAILURE);
    }
    for (int i = 0; i < graph->n; i++) {
        for (int j = 0; j < graph->n; j++) {
            if (fscanf(file, "%d", &graph->matrix[i][j]) != 1) {
                fprintf(stderr, "Blad formatu macierzy w pliku.\n");
                fclose(file); exit(EXIT_FAILURE);
            }
        }
    }
    fclose(file);
}

// --- MAIN ---
int main(int argc, char *argv[]) 
{
    if (argc != 2) {
        fprintf(stderr, "USAGE: %s <plik_mapy.txt>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    set_handler(SIG_IGN, SIGPIPE);
    set_handler(sig_handler, SIGINT);

    graph_t graph;
    load_graph(argv[1], &graph);

    // --- NOWE (ETAP 3): Tworzenie FIFO CasaRosada ---
    const char *fifo_name = "CasaRosada";
    unlink(fifo_name); 
    if (mkfifo(fifo_name, 0666) == -1) ERR("mkfifo");

    int pipes[MAX_N][MAX_N][2];
    for (int i = 0; i < graph.n; i++) {
        for (int j = 0; j < graph.n; j++) {
            if (graph.matrix[i][j] == 1) {
                if (pipe(pipes[i][j]) == -1) ERR("pipe");
            }
        }
    }

    // --- NOWE (ETAP 3): Specjalny potok Rząd -> Prowincja 0 ---
    int boss_to_p0[2];
    if (pipe(boss_to_p0) == -1) ERR("pipe");

    // --- TWORZENIE PROCESÓW PROWINCJI ---
    for (int k = 0; k < graph.n; k++) 
    {
        pid_t pid = fork();
        
        if (pid == 0) 
        {
            // Przygotowujemy sobie wygodne tablice do przechowywania NASZYCH potoków
            int in_fds[MAX_N + 1];  // Maksymalnie N potoków wejściowych + 1 od Rządu
            int out_fds[MAX_N];     // Maksymalnie N potoków wyjściowych
            int in_count = 0;
            int out_count = 0;

            // Zamykanie potoków w grafie (i zapisywanie tych naszych)
            for (int i = 0; i < graph.n; i++) {
                for (int j = 0; j < graph.n; j++) {
                    if (graph.matrix[i][j] == 1) {
                        if (i == k) {
                            close(pipes[i][j][READ]);
                            out_fds[out_count++] = pipes[i][j][WRITE]; // Zapisujemy potok wyjściowy
                        } 
                        else if (j == k) {
                            close(pipes[i][j][WRITE]);
                            in_fds[in_count++] = pipes[i][j][READ];   // Zapisujemy potok wejściowy
                        } 
                        else {
                            close(pipes[i][j][READ]);
                            close(pipes[i][j][WRITE]);
                        }
                    }
                }
            }

            // --- NOWE (ETAP 3): Obsługa specjalnego potoku od Rządu ---
            if (k == 0) {
                close(boss_to_p0[WRITE]);
                in_fds[in_count++] = boss_to_p0[READ]; // Prowincja 0 odbiera dyrektywy od rządu
            } else {
                close(boss_to_p0[READ]);
                close(boss_to_p0[WRITE]);
            }

            // --- NOWE (ETAP 3): Ustawiamy odczyty jako NIEBLOKUJĄCE ---
            for (int i = 0; i < in_count; i++) {
                set_nonblock(in_fds[i]);
            }

            printf("[%d]: Provincia %d gotowa na rozkazy.\n", getpid(), k);

            // --- NOWE (ETAP 3): Główna pętla propagacji ---
            char last_char = '\0';
            int active_inputs = in_count;

            // Działamy, dopóki mamy otwarte wejścia (bądź wciśnięto Ctrl+C)
            while (active_inputs > 0 && keep_running) 
            {
                for (int i = 0; i < in_count; i++) 
                {
                    if (in_fds[i] == -1) continue; // Ten potok został już zamknięty

                    char c;
                    ssize_t ret = read(in_fds[i], &c, 1);
                    
                    if (ret > 0) 
                    {
                        // Unikanie cykli: przetwarzamy dyrektywę tylko, jeśli jest inna niż ostatnia
                        if (c != last_char) 
                        {
                            printf("[%d]: Provincia %d otrzymala dyrektywe %c\n", getpid(), k, c);
                            last_char = c;

                            // Wysyłamy do wszystkich naszych wyjść
                            for (int out = 0; out < out_count; out++) {
                                if (out_fds[out] != -1) {
                                    if (write(out_fds[out], &c, 1) < 0 && errno == EPIPE) {
                                        close(out_fds[out]);
                                        out_fds[out] = -1; // Sąsiad padł, więcej mu nie wyślemy
                                    }
                                }
                            }
                        }
                    } 
                    else if (ret == 0) 
                    {
                        // Wykryto zamknięcie krawędzi wejściowej (EOF)
                        close(in_fds[i]);
                        in_fds[i] = -1;
                        active_inputs--;
                    }
                }
                msleep(10); // Odciążenie procesora, gdy nie ma wiadomości
            }

            // Sprzątanie po wyjściu z pętli
            for (int i = 0; i < out_count; i++) {
                if (out_fds[i] != -1) close(out_fds[i]);
            }
            
            exit(EXIT_SUCCESS);
        }
        else if (pid < 0) ERR("fork");
    }

    // --- RODZIC (Casa Rosada) ---

    // Zamykamy potoki miedzy-prowincjonalne
    for (int i = 0; i < graph.n; i++) {
        for (int j = 0; j < graph.n; j++) {
            if (graph.matrix[i][j] == 1) {
                close(pipes[i][j][READ]);
                close(pipes[i][j][WRITE]);
            }
        }
    }

    // Zamykamy odczyt z potoku Rząd -> Prowincja 0
    close(boss_to_p0[READ]);

    printf("Casa Rosada: Oczekiwanie na dyrektywy w FIFO 'CasaRosada'...\n");

    // --- NOWE (ETAP 3): Odczyt z FIFO i wysyłanie do Prowincji 0 ---
    int fifo_fd = open(fifo_name, O_RDONLY); // Zablokuje się, dopóki nie zaczniesz wysyłać!
    if (fifo_fd == -1) ERR("open fifo");

    char c;
    while (read(fifo_fd, &c, 1) > 0 && keep_running) 
    {
        if (c >= 'a' && c <= 'z') 
        {
            if (write(boss_to_p0[WRITE], &c, 1) < 0) {
                if (errno == EPIPE) break;
                ERR("write to p0");
            }
        }
    }

    // Kiedy zamkniesz wejście do FIFO (Ctrl+D), rodzic wyjdzie z pętli i zamknie potok.
    // Zamknięcie boss_to_p0 wywoła kaskadę zamykania u prowincji.
    close(fifo_fd);
    close(boss_to_p0[WRITE]); 
    unlink(fifo_name);

    // Oczekiwanie na kaskadowe zakończenie prowincji
    while (wait(NULL) > 0 || errno == EINTR) {
        errno = 0;
    }

    printf("Casa Rosada: Dystrybucja zakonczona. Otwarte deskryptory: %d\n", count_descriptors());

    return EXIT_SUCCESS;
}