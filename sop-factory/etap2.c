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

// --- FLAGI I SYGNAŁY ---
volatile sig_atomic_t keep_running = 1;

void sig_handler(int sig)
{
    keep_running = 0;
}

int set_handler(void (*f)(int), int sig)
{
    struct sigaction act = {0};
    act.sa_handler = f;
    if (sigaction(sig, &act, NULL) == -1)
        return -1;
    return 0;
}

// --- FUNKCJE POMOCNICZE ---

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

// --- ETAP 2: PRACA BRYGAD ---

void first_brigade_work(int production_pipe_write, int boss_pipe)
{
    srand(time(NULL) ^ getpid());
    printf("Worker %d from the FIRST brigade: descriptors: %d\n", getpid(), count_descriptors());

    while (keep_running)
    {
        msleep((rand() % 10) + 1); // Czeka 1-10 ms
        char letter = 'A' + (rand() % 26); // Generuje A-Z

        ssize_t ret;
        do {
            ret = write(production_pipe_write, &letter, 1);
        } while (ret < 0 && errno == EINTR);

        if (ret < 0) {
            if (errno == EPIPE) break; // Kolejne brygady już nie pracują
            ERR("write in brigade 1");
        }
    }
}

void second_brigade_work(int production_pipe_read, int production_pipe_write, int boss_pipe)
{
    srand(time(NULL) ^ getpid());
    printf("Worker %d from the SECOND brigade: descriptors: %d\n", getpid(), count_descriptors());

    char letter;
    while (keep_running)
    {
        ssize_t ret;
        do {
            ret = read(production_pipe_read, &letter, 1);
        } while (ret < 0 && errno == EINTR);

        if (ret < 0) ERR("read in brigade 2");
        if (ret == 0) break; // Brygada 1 skończyła wysyłać (EOF)

        msleep((rand() % 10) + 1); // Czeka 1-10 ms po odebraniu
        int copies = (rand() % 4) + 1; // 1-4 kopii

        for (int i = 0; i < copies; i++) {
            ssize_t w_ret;
            do {
                w_ret = write(production_pipe_write, &letter, 1);
            } while (w_ret < 0 && errno == EINTR);

            if (w_ret < 0) {
                if (errno == EPIPE) {
                    keep_running = 0; // Przerwij pętlę jeśli 3 brygada padnie
                    break;
                }
                ERR("write in brigade 2");
            }
        }
    }
}

void third_brigade_work(int production_pipe_read, int boss_pipe)
{
    srand(time(NULL) ^ getpid());
    printf("Worker %d from the THIRD brigade: descriptors: %d\n", getpid(), count_descriptors());

    char word[6];
    word[5] = '\0';
    int count = 0;

    while (keep_running)
    {
        msleep((rand() % 3) + 1); // Czeka 1-3 ms przed każdym odczytem

        char letter;
        ssize_t ret;
        do {
            ret = read(production_pipe_read, &letter, 1);
        } while (ret < 0 && errno == EINTR);

        if (ret < 0) ERR("read in brigade 3");
        if (ret == 0) break; // Brygada 2 skończyła pracę (EOF)

        word[count++] = letter;
        
        if (count == 5) {
            printf("Worker %d assembled: %s\n", getpid(), word);
            count = 0; // Reset na nowe słowo
        }
    }
}

// --- MAIN ---

int main(int argc, char* argv[])
{
    if (argc != 4) usage(argv[0]);
    int w1 = atoi(argv[1]), w2 = atoi(argv[2]), w3 = atoi(argv[3]);
    if (w1 < 1 || w1 > 10 || w2 < 1 || w2 > 10 || w3 < 1 || w3 > 10) usage(argv[0]);

    set_handler(SIG_IGN, SIGPIPE);
    set_handler(sig_handler, SIGINT); // Przechwytujemy Ctrl+C do czystego zakończenia

    int (*boss_pipes1)[2] = malloc(w1 * sizeof(int[2]));
    int (*boss_pipes2)[2] = malloc(w2 * sizeof(int[2]));
    int (*boss_pipes3)[2] = malloc(w3 * sizeof(int[2]));
    if (!boss_pipes1 || !boss_pipes2 || !boss_pipes3) ERR("malloc");

    int pipe12[2], pipe23[2];

    if (pipe(pipe12) == -1) ERR("pipe");
    if (pipe(pipe23) == -1) ERR("pipe");
    
    for (int i = 0; i < w1; i++) if (pipe(boss_pipes1[i]) == -1) ERR("pipe");
    for (int i = 0; i < w2; i++) if (pipe(boss_pipes2[i]) == -1) ERR("pipe");
    for (int i = 0; i < w3; i++) if (pipe(boss_pipes3[i]) == -1) ERR("pipe");

    // Tworzenie procesów Brygady 1
    for (int i = 0; i < w1; i++)
    {
        pid_t pid = fork();
        if (pid == 0)
        {
            close(pipe12[0]); 
            close(pipe23[0]); close(pipe23[1]); 

            for (int j = 0; j < w1; j++) {
                close(boss_pipes1[j][0]); 
                if (j != i) close(boss_pipes1[j][1]); 
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

    // Tworzenie procesów Brygady 2
    for (int i = 0; i < w2; i++)
    {
        pid_t pid = fork();
        if (pid == 0)
        {
            close(pipe12[1]); 
            close(pipe23[0]); 

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

    // Tworzenie procesów Brygady 3
    for (int i = 0; i < w3; i++)
    {
        pid_t pid = fork();
        if (pid == 0)
        {
            close(pipe12[0]); close(pipe12[1]); 
            close(pipe23[1]); 

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

    close(pipe12[0]); close(pipe12[1]);
    close(pipe23[0]); close(pipe23[1]);

    for (int i = 0; i < w1; i++) close(boss_pipes1[i][1]);
    for (int i = 0; i < w2; i++) close(boss_pipes2[i][1]);
    for (int i = 0; i < w3; i++) close(boss_pipes3[i][1]);

    // Oczekujemy, aż padną wszystkie dzieci (bądź przez Ctrl+C)
    while (wait(NULL) > 0 || errno == EINTR) { errno = 0; }

    for (int i = 0; i < w1; i++) close(boss_pipes1[i][0]);
    for (int i = 0; i < w2; i++) close(boss_pipes2[i][0]);
    for (int i = 0; i < w3; i++) close(boss_pipes3[i][0]);

    printf("Boss: descriptors: %d\n", count_descriptors());

    free(boss_pipes1); free(boss_pipes2); free(boss_pipes3);

    return EXIT_SUCCESS;
}