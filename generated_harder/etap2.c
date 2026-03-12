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

#define MAX_N 10
#define MAX_PATH 15
#define MAX_MSG 64

#define READ 0
#define WRITE 1

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), exit(EXIT_FAILURE))

// --- STRUKTURY DANYCH (Zgodnie z przeciekiem) ---

// 1. To jest "paczka", która będzie fizycznie wędrować potokami
typedef struct {
    int path[MAX_PATH];    // Ścieżka, np. [0, 2, 1, 3]
    int path_len;          // Długość tej ścieżki
    int current_step;      // Na którym kroku ścieżki aktualnie jesteśmy
    char message[MAX_MSG]; // Treść wiadomości ("coś jeszcze")
} packet_t;

// 2. To jest struktura konfiguracji do wczytania pliku przez rodzica
typedef struct {
    int n_provinces;
    int n_routes;
    packet_t routes[20];
    int matrix[MAX_N][MAX_N]; // Macierz posłuży nam tylko do łatwego zbudowania potoków
} config_t;

// --- FUNKCJE POMOCNICZE ---

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

// --- ETAP 1: SPRYTNE PARSOWANIE ŚCIEŻEK ---
void load_config(const char *filename, config_t *cfg) 
{
    FILE *file = fopen(filename, "r");
    if (!file) ERR("fopen");

    char line[256];
    
    // 1. Liczba prowincji
    if (fgets(line, sizeof(line), file) == NULL || sscanf(line, "%d", &cfg->n_provinces) != 1) {
        ERR("Blad N");
    }

    // Wyzerowanie konfiguracji
    cfg->n_routes = 0;
    memset(cfg->matrix, 0, sizeof(cfg->matrix));

    // 2. Parsowanie ścieżek i wiadomości (Zmieścisz to na kolokwium w pamięci!)
    while (fgets(line, sizeof(line), file)) 
    {
        // Szukamy dwukropka, który oddziela trasę od wiadomości
        char *colon = strchr(line, ':');
        if (!colon) continue; 
        
        *colon = '\0'; // Ucinamy stringa w miejscu dwukropka (dzielimy go na dwie części)
        
        packet_t *pkt = &cfg->routes[cfg->n_routes];
        pkt->path_len = 0;
        pkt->current_step = 0;
        
        // Kopiujemy wiadomość (to co było za dwukropkiem)
        // colon + 1, żeby pominąć sam znak ':'
        sscanf(colon + 1, "%63s", pkt->message); 

        // Parsujemy liczby z pierwszej części stringa (przed dwukropkiem)
        int node, offset;
        char *ptr = line;
        
        while (sscanf(ptr, "%d%n", &node, &offset) == 1) 
        {
            pkt->path[pkt->path_len++] = node;
            ptr += offset; // Przesuwamy się po stringu
            
            // Rejestrujemy krawędź w macierzy potoków (Z poprzedniego węzła do aktualnego)
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

    config_t cfg;
    load_config(argv[1], &cfg);

    // --- ETAP 2: POTOKI POINT-TO-POINT ---
    // Tworzymy fizyczne rury tylko tam, gdzie ścieżki tego wymagały (na podstawie wygenerowanej macierzy)
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
            // Zamykanie zbędnych potoków w dziecku 'k'
            for (int i = 0; i < cfg.n_provinces; i++) {
                for (int j = 0; j < cfg.n_provinces; j++) {
                    if (cfg.matrix[i][j] == 1) {
                        if (i == k) {
                            // Prowincja 'k' wysyła do 'j'
                            close(pipes[i][j][READ]);
                        } else if (j == k) {
                            // Prowincja 'k' odbiera od 'i'
                            close(pipes[i][j][WRITE]);
                        } else {
                            // Nie dotyczy prowincji 'k'
                            close(pipes[i][j][READ]);
                            close(pipes[i][j][WRITE]);
                        }
                    }
                }
            }

            printf("[%d]: Provincia %d podpieta do sieci. Deskryptory: %d\n", getpid(), k, count_descriptors());
            
            // W Etapie 3 tutaj dodamy:
            // 1. Zbieranie naszych wejść do tablicy in_fds (żeby ułatwić czytanie)
            // 2. Pętlę odbierającą packet_t i przesyłającą go dalej wzdłuż packet_t->path
            
            // Sprzątanie przed wyjściem 
            for (int i = 0; i < cfg.n_provinces; i++) {
                if (cfg.matrix[i][k] == 1) close(pipes[i][k][READ]);
                if (cfg.matrix[k][i] == 1) close(pipes[k][i][WRITE]);
            }
            
            exit(EXIT_SUCCESS);
        }
        else if (pid < 0) ERR("fork");
    }

    // --- RODZIC (Rząd) ---
    // Rodzic na razie zamyka wszystkie potoki między prowincjami (będzie miał dostęp tylko do Prowincji 0 przez specjalny kanał)
    for (int i = 0; i < cfg.n_provinces; i++) {
        for (int j = 0; j < cfg.n_provinces; j++) {
            if (cfg.matrix[i][j] == 1) {
                close(pipes[i][j][READ]);
                close(pipes[i][j][WRITE]);
            }
        }
    }

    while (wait(NULL) > 0 || errno == EINTR) {
        errno = 0;
    }

    printf("Casa Rosada: Koniec operacji.\n");

    return EXIT_SUCCESS;
}