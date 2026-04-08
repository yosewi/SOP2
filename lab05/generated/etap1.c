#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

#define MAX_N 10

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), exit(EXIT_FAILURE))

// --- ETAP 1: STRUKTURA BEZ PODWÓJNYCH WSKAŹNIKÓW ---
typedef struct {
    int n;
    int matrix[MAX_N][MAX_N]; // Statyczna tablica to najbezpieczniejszy wybór dla N <= 10
} graph_t;

// --- ETAP 1: PARSOWANIE PLIKU ---
void load_graph(const char *filename, graph_t *graph) 
{
    FILE *file = fopen(filename, "r");
    if (file == NULL) ERR("fopen");

    // Wczytanie liczby N
    if (fscanf(file, "%d", &graph->n) != 1) {
        fprintf(stderr, "Błąd wczytywania rozmiaru grafu.\n");
        fclose(file);
        exit(EXIT_FAILURE);
    }

    if (graph->n < 2 || graph->n > MAX_N) {
        fprintf(stderr, "Nieprawidlowy rozmiar grafu (2 <= N <= 10).\n");
        fclose(file);
        exit(EXIT_FAILURE);
    }

    // "Sprytne parsowanie" - po prostu czytamy kolejne liczby ignorując białe znaki (spacje/entery)
    for (int i = 0; i < graph->n; i++) {
        for (int j = 0; j < graph->n; j++) {
            if (fscanf(file, "%d", &graph->matrix[i][j]) != 1) {
                fprintf(stderr, "Blad formatu macierzy w pliku.\n");
                fclose(file);
                exit(EXIT_FAILURE);
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

    // Opcjonalne: Wypisanie dla pewności, że wczytało się poprawnie
    printf("Wczytano graf o rozmiarze N = %d\n", graph.n);

    // --- ETAP 1: TWORZENIE PROCESÓW PROWINCJI ---
    for (int i = 0; i < graph.n; i++) 
    {
        pid_t pid = fork();
        
        if (pid == 0) // Kod dziecka (Prowincji)
        {
            printf("[%d]: Provincia %d gotowa na rozkazy\n", getpid(), i);
            
            // Tutaj w kolejnych etapach będzie pętla robocza Prowincji
            
            exit(EXIT_SUCCESS);
        }
        else if (pid < 0) 
        {
            ERR("fork");
        }
    }

    // Rodzic (Casa Rosada) czeka, aż wszystkie prowincje zakończą działanie
    while (wait(NULL) > 0 || errno == EINTR) {
        errno = 0;
    }

    printf("Casa Rosada: Wszystkie prowincje zakonczyly inicjalizacje.\n");

    return EXIT_SUCCESS;
}