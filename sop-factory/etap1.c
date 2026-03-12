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

#ifndef TEMP_FAILURE_RETRY
#define TEMP_FAILURE_RETRY(expression)             \
    (__extension__({                               \
        long int __result;                         \
        do                                         \
            __result = (long int)(expression);     \
        while (__result == -1L && errno == EINTR); \
        __result;                                  \
    }))
#endif

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

int set_handler(void (*f)(int), int sig)
{
    struct sigaction act = {0};
    act.sa_handler = f;
    if (sigaction(sig, &act, NULL) == -1)
        return -1;
    return 0;
}

void msleep(int millisec)
{
    struct timespec tt;
    tt.tv_sec = millisec / 1000;
    tt.tv_nsec = (millisec % 1000) * 1000000;
    while (nanosleep(&tt, &tt) == -1 && errno == EINTR);
}

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

void usage(char* program)
{
    printf("USAGE: %s w1 w2 w3 (1 <= w1,w2,w3 <= 10)\n", program);
    exit(EXIT_FAILURE);
}

// --- PRACA BRYGAD (Na razie tylko wypisują deskryptory i się kończą) ---

void first_brigade_work(int production_pipe_write, int boss_pipe)
{
    srand(time(NULL) ^ getpid());
    printf("Worker %d from the FIRST brigade: descriptors: %d\n", getpid(), count_descriptors());
}

void second_brigade_work(int production_pipe_read, int production_pipe_write, int boss_pipe)
{
    srand(time(NULL) ^ getpid());
    printf("Worker %d from the SECOND brigade: descriptors: %d\n", getpid(), count_descriptors());
}

void third_brigade_work(int production_pipe_read, int boss_pipe)
{
    srand(time(NULL) ^ getpid());
    printf("Worker %d from the THIRD brigade: descriptors: %d\n", getpid(), count_descriptors());
}

// --- MAIN ---

int main(int argc, char* argv[])
{
    if (argc != 4) usage(argv[0]);
    int w1 = atoi(argv[1]), w2 = atoi(argv[2]), w3 = atoi(argv[3]);
    if (w1 < 1 || w1 > 10 || w2 < 1 || w2 > 10 || w3 < 1 || w3 > 10) usage(argv[0]);

    set_handler(SIG_IGN, SIGPIPE);

    // Dynamiczna alokacja potoków dla szefa, używamy wskaźników do tablic 2-elementowych
    int (*boss_pipes1)[2] = malloc(w1 * sizeof(int[2]));
    int (*boss_pipes2)[2] = malloc(w2 * sizeof(int[2]));
    int (*boss_pipes3)[2] = malloc(w3 * sizeof(int[2]));
    if (!boss_pipes1 || !boss_pipes2 || !boss_pipes3) ERR("malloc");

    int pipe12[2], pipe23[2];

    // Inicjalizacja wszystkich potoków
    if (pipe(pipe12) == -1) ERR("pipe");
    if (pipe(pipe23) == -1) ERR("pipe");
    
    for (int i = 0; i < w1; i++) if (pipe(boss_pipes1[i]) == -1) ERR("pipe");
    for (int i = 0; i < w2; i++) if (pipe(boss_pipes2[i]) == -1) ERR("pipe");
    for (int i = 0; i < w3; i++) if (pipe(boss_pipes3[i]) == -1) ERR("pipe");

    // --- TWORZENIE PROCESÓW ---

    // Brygada 1
    for (int i = 0; i < w1; i++)
    {
        pid_t pid = fork();
        if (pid == 0)
        {
            close(pipe12[0]); // Nie czyta z 12
            close(pipe23[0]); close(pipe23[1]); // Nie korzysta z 23

            for (int j = 0; j < w1; j++) {
                close(boss_pipes1[j][0]); // Nie czyta od szefa
                if (j != i) close(boss_pipes1[j][1]); // Zamyka zapis innych z brygady 1
            }
            for (int j = 0; j < w2; j++) { close(boss_pipes2[j][0]); close(boss_pipes2[j][1]); }
            for (int j = 0; j < w3; j++) { close(boss_pipes3[j][0]); close(boss_pipes3[j][1]); }

            first_brigade_work(pipe12[1], boss_pipes1[i][1]);

            close(pipe12[1]);
            close(boss_pipes1[i][1]);
            free(boss_pipes1); free(boss_pipes2); free(boss_pipes3);
            exit(EXIT_SUCCESS);
        }
        else if (pid == -1) ERR("fork");
    }

    // Brygada 2
    for (int i = 0; i < w2; i++)
    {
        pid_t pid = fork();
        if (pid == 0)
        {
            close(pipe12[1]); // Nie pisze do 12
            close(pipe23[0]); // Nie czyta z 23

            for (int j = 0; j < w1; j++) { close(boss_pipes1[j][0]); close(boss_pipes1[j][1]); }
            for (int j = 0; j < w2; j++) {
                close(boss_pipes2[j][0]);
                if (j != i) close(boss_pipes2[j][1]);
            }
            for (int j = 0; j < w3; j++) { close(boss_pipes3[j][0]); close(boss_pipes3[j][1]); }

            second_brigade_work(pipe12[0], pipe23[1], boss_pipes2[i][1]);

            close(pipe12[0]);
            close(pipe23[1]);
            close(boss_pipes2[i][1]);
            free(boss_pipes1); free(boss_pipes2); free(boss_pipes3);
            exit(EXIT_SUCCESS);
        }
        else if (pid == -1) ERR("fork");
    }

    // Brygada 3
    for (int i = 0; i < w3; i++)
    {
        pid_t pid = fork();
        if (pid == 0)
        {
            close(pipe12[0]); close(pipe12[1]); // Nie korzysta z 12
            close(pipe23[1]); // Nie pisze do 23

            for (int j = 0; j < w1; j++) { close(boss_pipes1[j][0]); close(boss_pipes1[j][1]); }
            for (int j = 0; j < w2; j++) { close(boss_pipes2[j][0]); close(boss_pipes2[j][1]); }
            for (int j = 0; j < w3; j++) {
                close(boss_pipes3[j][0]);
                if (j != i) close(boss_pipes3[j][1]);
            }

            third_brigade_work(pipe23[0], boss_pipes3[i][1]);

            close(pipe23[0]);
            close(boss_pipes3[i][1]);
            free(boss_pipes1); free(boss_pipes2); free(boss_pipes3);
            exit(EXIT_SUCCESS);
        }
        else if (pid == -1) ERR("fork");
    }

    // --- RODZIC (BOSS) ---

    // Rodzic nie używa potoków międzystopniowych
    close(pipe12[0]); close(pipe12[1]);
    close(pipe23[0]); close(pipe23[1]);

    // Rodzic tylko czyta od pracowników, zamyka wszystkie końcówki do zapisu
    for (int i = 0; i < w1; i++) close(boss_pipes1[i][1]);
    for (int i = 0; i < w2; i++) close(boss_pipes2[i][1]);
    for (int i = 0; i < w3; i++) close(boss_pipes3[i][1]);

    while (wait(NULL) > 0);

    // Sprzątanie po pracy w rodzicu
    for (int i = 0; i < w1; i++) close(boss_pipes1[i][0]);
    for (int i = 0; i < w2; i++) close(boss_pipes2[i][0]);
    for (int i = 0; i < w3; i++) close(boss_pipes3[i][0]);

    printf("Boss: descriptors: %d\n", count_descriptors());

    free(boss_pipes1); free(boss_pipes2); free(boss_pipes3);

    return EXIT_SUCCESS;
}