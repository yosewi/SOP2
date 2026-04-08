#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <limits.h>
#include <fcntl.h>
#include <signal.h>

#define MAX_N 10
#define MAX_PATH 15
#define MAX_MSG 64

#define READ 0
#define WRITE 1
#define FIFO_NAME "CasaRosada"

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

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

void set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) ERR("fcntl get");
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) ERR("fcntl set");
}

// --- STRUKTURY DANYCH ---
typedef struct {
    int path[MAX_PATH];    
    int path_len;          
    int current_step;      
    char message[MAX_MSG]; 
} packet_t;

typedef struct {
    int n_provinces;
    int n_routes;
    packet_t routes[20];
    int matrix[MAX_N][MAX_N]; 
} config_t;

// Etap 4: Struktura raportu
typedef struct {
    int province_id;
    pid_t pid;
    int messages_relayed;
} worker_report_t;

// --- ETAP 1: PARSOWANIE ---
void load_config(const char *filename, config_t *cfg) {
    FILE *file = fopen(filename, "r");
    if (!file) ERR("fopen");

    char line[256];
    if (fgets(line, sizeof(line), file) == NULL || sscanf(line, "%d", &cfg->n_provinces) != 1) {
        ERR("Blad N");
    }

    cfg->n_routes = 0;
    memset(cfg->matrix, 0, sizeof(cfg->matrix));

    while (fgets(line, sizeof(line), file)) {
        char *colon = strchr(line, ':');
        if (!colon) continue; 
        
        *colon = '\0'; 
        
        packet_t *pkt = &cfg->routes[cfg->n_routes];
        pkt->path_len = 0;
        pkt->current_step = 0;
        sscanf(colon + 1, "%63s", pkt->message); 

        int node, offset;
        char *ptr = line;
        
        while (sscanf(ptr, "%d%n", &node, &offset) == 1) {
            pkt->path[pkt->path_len++] = node;
            ptr += offset; 
            
            if (pkt->path_len > 1) {
                int prev_node = pkt->path[pkt->path_len - 2];
                cfg->matrix[prev_node][node] = 1;
            }
        }
        cfg->n_routes++;
    }
    fclose(file);
}

// --- MAIN ---
int main(int argc, char *argv[]) 
{
    if (argc != 2) {
        fprintf(stderr, "USAGE: %s <mapa.txt>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    set_handler(SIG_IGN, SIGPIPE);
    set_handler(sig_handler, SIGINT);

    config_t cfg;
    load_config(argv[1], &cfg);

    // Etap 3: FIFO
    unlink(FIFO_NAME);
    if (mkfifo(FIFO_NAME, 0666) == -1) ERR("mkfifo");

    // Etap 4: Współdzielony potok do raportów
    int reports_pipe[2];
    if (pipe(reports_pipe) == -1) ERR("pipe reports");

    // Etap 3: Specjalny potok Rząd -> Prowincja 0
    int boss_to_p0[2];
    if (pipe(boss_to_p0) == -1) ERR("pipe boss_to_p0");

    // Etap 2: Potoki Point-to-Point prowincji
    int pipes[MAX_N][MAX_N][2]; 
    for (int i = 0; i < cfg.n_provinces; i++) {
        for (int j = 0; j < cfg.n_provinces; j++) {
            if (cfg.matrix[i][j] == 1) {
                if (pipe(pipes[i][j]) == -1) ERR("pipe");
            }
        }
    }

    // --- TWORZENIE PROCESÓW PROWINCJI ---
    for (int k = 0; k < cfg.n_provinces; k++) 
    {
        pid_t pid = fork();
        
        if (pid == 0) 
        {
            worker_report_t report = { .province_id = k, .pid = getpid(), .messages_relayed = 0 };

            close(reports_pipe[READ]); // Prowincja tylko pisze raporty

            int in_fds[MAX_N + 1];  
            int in_count = 0;

            // Zamykanie potoków międzyprowincjonalnych i zbieranie wejść
            for (int i = 0; i < cfg.n_provinces; i++) {
                for (int j = 0; j < cfg.n_provinces; j++) {
                    if (cfg.matrix[i][j] == 1) {
                        if (i == k) {
                            close(pipes[i][j][READ]);
                        } else if (j == k) {
                            close(pipes[i][j][WRITE]);
                            in_fds[in_count++] = pipes[i][j][READ]; 
                        } else {
                            close(pipes[i][j][READ]);
                            close(pipes[i][j][WRITE]);
                        }
                    }
                }
            }

            // Odbiór od rządu tylko dla Prowincji 0
            if (k == 0) {
                close(boss_to_p0[WRITE]);
                in_fds[in_count++] = boss_to_p0[READ];
            } else {
                close(boss_to_p0[READ]);
                close(boss_to_p0[WRITE]);
            }

            // Ustawienie wejść w tryb nieblokujący
            for (int i = 0; i < in_count; i++) {
                set_nonblock(in_fds[i]);
            }

            printf("[%d]: Provincia %d podpieta do sieci.\n", getpid(), k);
            
            int active_inputs = in_count;
            packet_t pkt;

            // --- ETAP 3: GŁÓWNA PĘTLA PROPAGACJI ---
            while (active_inputs > 0 && keep_running) 
            {
                for (int i = 0; i < in_count; i++) 
                {
                    if (in_fds[i] == -1) continue;

                    ssize_t ret;
                    do {
                        ret = read(in_fds[i], &pkt, sizeof(packet_t));
                    } while (ret < 0 && errno == EINTR);

                    if (ret == sizeof(packet_t)) 
                    {
                        printf("[%d]: Provincia %d odebrala paczke: '%s'\n", getpid(), k, pkt.message);
                        report.messages_relayed++;

                        // Routing Źródłowy: Jeśli to nie jest koniec ścieżki, podaj dalej
                        if (pkt.current_step + 1 < pkt.path_len) 
                        {
                            pkt.current_step++;
                            int next_node = pkt.path[pkt.current_step];
                            
                            ssize_t w_ret;
                            do {
                                w_ret = write(pipes[k][next_node][WRITE], &pkt, sizeof(packet_t));
                            } while (w_ret < 0 && errno == EINTR);

                            if (w_ret < 0 && errno != EPIPE) {
                                ERR("write to next province");
                            } else if (w_ret > 0) {
                                printf("  -> Przekazano do Prowincji %d\n", next_node);
                            }
                        }
                    } 
                    else if (ret == 0) 
                    {
                        close(in_fds[i]);
                        in_fds[i] = -1;
                        active_inputs--;
                    }
                }
                msleep(10); 
            }

            // --- ETAP 4: RAPORTOWANIE I CZYSZCZENIE ---
            if (write(reports_pipe[WRITE], &report, sizeof(worker_report_t)) < 0 && errno != EPIPE) {
                ERR("write report");
            }
            close(reports_pipe[WRITE]);

            for (int i = 0; i < cfg.n_provinces; i++) {
                if (cfg.matrix[k][i] == 1) close(pipes[k][i][WRITE]);
            }
            
            exit(EXIT_SUCCESS);
        }
        else if (pid < 0) ERR("fork");
    }

    // --- RODZIC (Rząd Centralny) ---

    // Zamykamy potoki międzyprowincjonalne 
    for (int i = 0; i < cfg.n_provinces; i++) {
        for (int j = 0; j < cfg.n_provinces; j++) {
            if (cfg.matrix[i][j] == 1) {
                close(pipes[i][j][READ]);
                close(pipes[i][j][WRITE]);
            }
        }
    }
    
    close(boss_to_p0[READ]);
    close(reports_pipe[WRITE]); // Rząd tylko czyta raporty

    printf("Casa Rosada: Oczekiwanie na impuls w %s...\n", FIFO_NAME);

    // Otwarcie FIFO blokuje rodzica do czasu, aż ktoś zacznie pisać (np. cat > CasaRosada)
    int fifo_fd = open(FIFO_NAME, O_RDONLY);
    if (fifo_fd == -1) ERR("open fifo");

    char trigger_char;
    while (read(fifo_fd, &trigger_char, 1) > 0 && keep_running) 
    {
        // Reagujemy na małe litery (ignorujemy np. klawisz Enter)
        if (trigger_char >= 'a' && trigger_char <= 'z') 
        {
            printf("\nCasa Rosada: Start dystrybucji %d dyrektyw!\n", cfg.n_routes);
            for (int r = 0; r < cfg.n_routes; r++) {
                ssize_t w_ret;
                do {
                    w_ret = write(boss_to_p0[WRITE], &cfg.routes[r], sizeof(packet_t));
                } while (w_ret < 0 && errno == EINTR);
                
                if (w_ret < 0 && errno != EPIPE) ERR("write packet to p0");
            }
        }
    }

    // --- KASKADOWE ZAKOŃCZENIE ---
    // Po wciśnięciu Ctrl+D w `cat`, rodzic wychodzi z pętli i zamyka potok do Prowincji 0
    close(fifo_fd);
    unlink(FIFO_NAME);
    close(boss_to_p0[WRITE]); 

    // Odbieranie raportów od gasnących prowincji
    printf("\n--- RAPORT KONCOWY KAMPANII ---\n");
    worker_report_t report;
    while (read(reports_pipe[READ], &report, sizeof(worker_report_t)) > 0) {
        printf("Prowincja %d (PID %d) przekazala %d paczek.\n", report.province_id, report.pid, report.messages_relayed);
    }
    close(reports_pipe[READ]);

    while (wait(NULL) > 0 || errno == EINTR) {
        errno = 0;
    }

    printf("\nCasa Rosada: System wylaczony. Otwarte deskryptory: %d\n", count_descriptors());

    return EXIT_SUCCESS;
}