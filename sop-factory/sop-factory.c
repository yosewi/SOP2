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

#define FIFO_NAME "warehouse"

// --- NOWE (ETAP 3): Struktura raportu dla Szefa ---
typedef struct {
    pid_t pid;
    int brigade_id;
    int items_processed;
} worker_report_t;

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

// --- ETAP 3: ZMODYFIKOWANA PRACA BRYGAD ---

// ZMIANA: Dodano argument const char* fifo_path
void first_brigade_work(int production_pipe_write, int boss_pipe, const char* fifo_path)
{
    srand(time(NULL) ^ getpid());
    worker_report_t report = { .pid = getpid(), .brigade_id = 1, .items_processed = 0 };

    // NOWE: Otwieranie FIFO (zablokuje się, dopóki ktoś nie zacznie pisać!)
    int fifo_fd = open(fifo_path, O_RDONLY);
    if (fifo_fd == -1) ERR("open fifo");

    char letter;
    while (keep_running)
    {
        ssize_t ret;
        do {
            ret = read(fifo_fd, &letter, 1);
        } while (ret < 0 && errno == EINTR);

        if (ret < 0) ERR("read from fifo");
        if (ret == 0) break; // Zewnętrzny proces zamknął FIFO (EOF)

        // NOWE: Akceptujemy tylko 'a' - 'z'
        if (letter < 'a' || letter > 'z') continue;

        msleep((rand() % 10) + 1); 

        ssize_t w_ret;
        do {
            w_ret = write(production_pipe_write, &letter, 1);
        } while (w_ret < 0 && errno == EINTR);

        if (w_ret < 0) {
            if (errno == EPIPE) break; 
            ERR("write in brigade 1");
        }
        
        report.items_processed++;
    }

    close(fifo_fd);

    // NOWE: Raportowanie do Szefa przed wyjściem
    if (write(boss_pipe, &report, sizeof(worker_report_t)) < 0 && errno != EPIPE)
        ERR("report to boss");
}

void second_brigade_work(int production_pipe_read, int production_pipe_write, int boss_pipe)
{
    srand(time(NULL) ^ getpid());
    worker_report_t report = { .pid = getpid(), .brigade_id = 2, .items_processed = 0 };

    char letter;
    while (keep_running)
    {
        ssize_t ret;
        do {
            ret = read(production_pipe_read, &letter, 1);
        } while (ret < 0 && errno == EINTR);

        if (ret < 0) ERR("read in brigade 2");
        if (ret == 0) break; 

        msleep((rand() % 10) + 1); 
        int copies = (rand() % 4) + 1; 

        for (int i = 0; i < copies; i++) {
            ssize_t w_ret;
            do {
                w_ret = write(production_pipe_write, &letter, 1);
            } while (w_ret < 0 && errno == EINTR);

            if (w_ret < 0) {
                if (errno == EPIPE) { keep_running = 0; break; }
                ERR("write in brigade 2");
            }
        }
        report.items_processed++; // Liczymy obsłużone znaki wejściowe
    }

    if (write(boss_pipe, &report, sizeof(worker_report_t)) < 0 && errno != EPIPE)
        ERR("report to boss");
}

void third_brigade_work(int production_pipe_read, int boss_pipe)
{
    srand(time(NULL) ^ getpid());
    worker_report_t report = { .pid = getpid(), .brigade_id = 3, .items_processed = 0 };

    char word[6];
    word[5] = '\0';
    int count = 0;

    while (keep_running)
    {
        msleep((rand() % 3) + 1); 

        char letter;
        ssize_t ret;
        do {
            ret = read(production_pipe_read, &letter, 1);
        } while (ret < 0 && errno == EINTR);

        if (ret < 0) ERR("read in brigade 3");
        if (ret == 0) break; 

        word[count++] = letter;
        
        if (count == 5) {
            printf("Worker %d assembled: %s\n", getpid(), word);
            count = 0; 
            report.items_processed++; // Liczymy zmontowane SŁOWA
        }
    }

    if (write(boss_pipe, &report, sizeof(worker_report_t)) < 0 && errno != EPIPE)
        ERR("report to boss");
}

// --- MAIN ---

int main(int argc, char* argv[])
{
    if (argc != 4) usage(argv[0]);
    int w1 = atoi(argv[1]), w2 = atoi(argv[2]), w3 = atoi(argv[3]);
    if (w1 < 1 || w1 > 10 || w2 < 1 || w2 > 10 || w3 < 1 || w3 > 10) usage(argv[0]);

    set_handler(SIG_IGN, SIGPIPE);
    set_handler(sig_handler, SIGINT); 

    // --- NOWE (ETAP 3): Tworzenie nazwanego potoku (FIFO) ---
    unlink(FIFO_NAME); // Usuwamy stary, jeśli program wcześniej padł
    if (mkfifo(FIFO_NAME, 0666) == -1) ERR("mkfifo");

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
            close(pipe12[0]); close(pipe23[0]); close(pipe23[1]); 

            for (int j = 0; j < w1; j++) {
                close(boss_pipes1[j][0]); 
                if (j != i) close(boss_pipes1[j][1]); 
            }
            for (int j = 0; j < w2; j++) { close(boss_pipes2[j][0]); close(boss_pipes2[j][1]); }
            for (int j = 0; j < w3; j++) { close(boss_pipes3[j][0]); close(boss_pipes3[j][1]); }

            first_brigade_work(pipe12[1], boss_pipes1[i][1], FIFO_NAME); // Przekazujemy nazwę FIFO

            close(pipe12[1]); close(boss_pipes1[i][1]);
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
            close(pipe12[1]); close(pipe23[0]); 

            for (int j = 0; j < w1; j++) { close(boss_pipes1[j][0]); close(boss_pipes1[j][1]); }
            for (int j = 0; j < w2; j++) {
                close(boss_pipes2[j][0]);
                if (j != i) close(boss_pipes2[j][1]);
            }
            for (int j = 0; j < w3; j++) { close(boss_pipes3[j][0]); close(boss_pipes3[j][1]); }

            second_brigade_work(pipe12[0], pipe23[1], boss_pipes2[i][1]);

            close(pipe12[0]); close(pipe23[1]); close(boss_pipes2[i][1]);
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
            close(pipe12[0]); close(pipe12[1]); close(pipe23[1]); 

            for (int j = 0; j < w1; j++) { close(boss_pipes1[j][0]); close(boss_pipes1[j][1]); }
            for (int j = 0; j < w2; j++) { close(boss_pipes2[j][0]); close(boss_pipes2[j][1]); }
            for (int j = 0; j < w3; j++) {
                close(boss_pipes3[j][0]);
                if (j != i) close(boss_pipes3[j][1]);
            }

            third_brigade_work(pipe23[0], boss_pipes3[i][1]);

            close(pipe23[0]); close(boss_pipes3[i][1]);
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

    // Oczekujemy na zakończenie dzieci (skończą się naturalnie po zamknięciu FIFO)
    while (wait(NULL) > 0 || errno == EINTR) { errno = 0; }

    // --- NOWE (ETAP 3): Szef zbiera raporty od nieżyjących już pracowników ---
    printf("\n--- BOSS SHIFT SUMMARY ---\n");
    worker_report_t report;
    
    for (int i = 0; i < w1; i++) {
        if (read(boss_pipes1[i][0], &report, sizeof(report)) > 0)
            printf("Brigade 1 Worker [%d] read %d valid letters.\n", report.pid, report.items_processed);
        close(boss_pipes1[i][0]);
    }
    for (int i = 0; i < w2; i++) {
        if (read(boss_pipes2[i][0], &report, sizeof(report)) > 0)
            printf("Brigade 2 Worker [%d] processed %d letters.\n", report.pid, report.items_processed);
        close(boss_pipes2[i][0]);
    }
    for (int i = 0; i < w3; i++) {
        if (read(boss_pipes3[i][0], &report, sizeof(report)) > 0)
            printf("Brigade 3 Worker [%d] assembled %d words.\n", report.pid, report.items_processed);
        close(boss_pipes3[i][0]);
    }

    printf("Boss: descriptors: %d\n", count_descriptors());

    free(boss_pipes1); free(boss_pipes2); free(boss_pipes3);
    
    // Sprzątanie po FIFO
    unlink(FIFO_NAME);

    return EXIT_SUCCESS;
}