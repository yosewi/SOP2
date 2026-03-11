#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

#define SWAP(x, y)        \
    do                    \
    {                     \
        typeof(x) _x = x; \
        typeof(y) _y = y; \
        x = _y;           \
        y = _x;           \
    } while (0)

// --- FUNKCJE POMOCNICZE ---

int count_descriptors()
{
    int count = 0;
    DIR* dir;
    struct dirent* entry;
    struct stat stats;
    if ((dir = opendir("/proc/self/fd")) == NULL)
        ERR("opendir");
    char path[PATH_MAX];
    getcwd(path, PATH_MAX);
    chdir("/proc/self/fd");
    do
    {
        errno = 0;
        if ((entry = readdir(dir)) != NULL)
        {
            if (lstat(entry->d_name, &stats))
                ERR("lstat");
            if (!S_ISDIR(stats.st_mode))
                count++;
        }
    } while (entry != NULL);
    if (chdir(path))
        ERR("chdir");
    if (closedir(dir))
        ERR("closedir");
    return count - 1; 
}

int set_handler(void (*f)(int), int sig)
{
    struct sigaction act = {0};
    act.sa_handler = f;
    if (sigaction(sig, &act, NULL) == -1)
        return -1;
    return 0;
}

void usage(char *name)
{
    fprintf(stderr, "USAGE: %s N M\n", name);
    fprintf(stderr, "N: 4 <= N <= 7 (number of players)\n");
    fprintf(stderr, "M: M >= 4 and M * N <= 52 (cards per player)\n");
    exit(EXIT_FAILURE);
}

void shuffle(int *deck, int size)
{
    for (int i = size - 1; i > 0; i--)
    {
        int j = rand() % (i + 1);
        SWAP(deck[i], deck[j]);
    }
}

// --- LOGIKA GRACZA ---

void player_work(int read_fd, int m)
{
    int *hand = malloc(m * sizeof(int));
    if (!hand) ERR("malloc");

    // Odczyt M kart od serwera
    if (read(read_fd, hand, m * sizeof(int)) < 0)
        ERR("read cards");

    pid_t pid = getpid();
    printf("[%d]: Cards: ", pid);
    for (int i = 0; i < m; i++)
    {
        printf("%d ", hand[i]);
    }
    printf("\n");

    free(hand);
}

// --- MAIN ---

int main(int argc, char **argv)
{
    if (argc != 3)
        usage(argv[0]);

    int n = atoi(argv[1]);
    int m = atoi(argv[2]);

    if (n < 4 || n > 7 || m < 4 || n * m > 52)
        usage(argv[0]);

    set_handler(SIG_IGN, SIGPIPE);
    srand(time(NULL)); // Inicjalizacja RNG tylko raz w rodzicu!

    // Inicjalizacja talii
    int deck[52];
    for (int i = 0; i < 52; i++)
        deck[i] = i;
    
    shuffle(deck, 52);

    // Tworzenie potoków do rozdawania kart (Serwer -> Gracz)
    int (*deal_pipes)[2] = malloc(n * sizeof(int[2]));
    if (!deal_pipes) ERR("malloc");

    for (int i = 0; i < n; i++)
    {
        if (pipe(deal_pipes[i]) == -1)
            ERR("pipe");
    }

    // Tworzenie procesów graczy
    for (int i = 0; i < n; i++)
    {
        pid_t pid = fork();
        if (pid == 0) // Dziecko (Gracz)
        {
            // Zamykanie nieużywanych końców potoków w dziecku
            for (int j = 0; j < n; j++)
            {
                close(deal_pipes[j][1]); // Dziecko nigdy nie pisze do serwera tym łączem
                if (j != i)
                    close(deal_pipes[j][0]); // Zamykamy odczyty innych dzieci
            }

            int read_fd = deal_pipes[i][0];
            
            player_work(read_fd, m);
            
            close(read_fd);
            free(deal_pipes);
            exit(EXIT_SUCCESS);
        }
        else if (pid == -1)
        {
            ERR("fork");
        }
    }

    // Rodzic (Serwer) zamyka końce do odczytu
    for (int i = 0; i < n; i++)
    {
        close(deal_pipes[i][0]);
    }

    // Rozdawanie kart
    int deck_index = 0;
    for (int i = 0; i < n; i++)
    {
        int *player_cards = &deck[deck_index];
        if (write(deal_pipes[i][1], player_cards, m * sizeof(int)) < 0)
            ERR("write cards");
        
        deck_index += m;
        close(deal_pipes[i][1]); // Zamykamy potok po wysłaniu kart do gracza 'i'
    }

    // Oczekiwanie na zakończenie dzieci
    while (wait(NULL) > 0);

    free(deal_pipes);
    printf("Server: Open descriptors at exit = %d\n", count_descriptors());

    return EXIT_SUCCESS;
}