#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include <limits.h>

#define MAX_N 10

// Dla czytelności w kodzie
#define READ 0
#define WRITE 1

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), exit(EXIT_FAILURE))

// --- FUNKCJE POMOCNICZE ---
int count_descriptors()
{
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

    graph_t graph;
    load_graph(argv[1], &graph);

    // --- NOWE (ETAP 2): TWORZENIE POTOKÓW GRAFU ---
    // Trójwymiarowa tablica: z jakiej prowincji [i], do jakiej prowincji [j], [0-odczyt, 1-zapis]
    int pipes[MAX_N][MAX_N][2];

    for (int i = 0; i < graph.n; i++) {
        for (int j = 0; j < graph.n; j++) {
            // Jeśli istnieje krawędź z i do j, tworzymy fizyczny potok
            if (graph.matrix[i][j] == 1) {
                if (pipe(pipes[i][j]) == -1) ERR("pipe");
            }
        }
    }

    // --- TWORZENIE PROCESÓW PROWINCJI ---
    for (int k = 0; k < graph.n; k++) 
    {
        pid_t pid = fork();
        
        if (pid == 0) // Kod dziecka (Prowincja k)
        {
            // --- NOWE (ETAP 2): PRECYZYJNE ZAMYKANIE DESKRYPTORÓW ---
            for (int i = 0; i < graph.n; i++) {
                for (int j = 0; j < graph.n; j++) {
                    
                    if (graph.matrix[i][j] == 1) { // Rozpatrujemy tylko istniejące potoki
                        
                        if (i == k) {
                            // Ja jestem nadawcą (piszę do j). Zamykam odczyt.
                            close(pipes[i][j][READ]);
                        } 
                        else if (j == k) {
                            // Ja jestem odbiorcą (czytam z i). Zamykam zapis.
                            close(pipes[i][j][WRITE]);
                        } 
                        else {
                            // Ten potok mnie nie dotyczy. Zamykam oba końce.
                            close(pipes[i][j][READ]);
                            close(pipes[i][j][WRITE]);
                        }
                    }
                }
            }

            printf("[%d]: Provincia %d gotowa na rozkazy. Otwarte deskryptory: %d\n", getpid(), k, count_descriptors());
            
            exit(EXIT_SUCCESS);
        }
        else if (pid < 0) 
        {
            ERR("fork");
        }
    }

    // --- NOWE (ETAP 2): RODZIC ZAMYKA WSZYSTKIE POTOKI ---
    // Rodzic nie uczestniczy w bezpośredniej komunikacji między prowincjami
    for (int i = 0; i < graph.n; i++) {
        for (int j = 0; j < graph.n; j++) {
            if (graph.matrix[i][j] == 1) {
                close(pipes[i][j][READ]);
                close(pipes[i][j][WRITE]);
            }
        }
    }

    // Oczekiwanie na wszystkie prowincje
    while (wait(NULL) > 0 || errno == EINTR) {
        errno = 0;
    }

    printf("Casa Rosada: Wszystkie prowincje zakonczyly inicjalizacje.\n");

    return EXIT_SUCCESS;
}