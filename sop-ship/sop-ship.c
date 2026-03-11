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

// --- NOWE (ETAP 4): Flaga do obsługi Ctrl+C ---
volatile sig_atomic_t keep_running = 1;

void sig_handler(int sig)
{
    keep_running = 0;
}

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

int set_handler(void (*f)(int), int sig)
{
    struct sigaction act = {0};
    act.sa_handler = f;
    if (sigaction(sig, &act, NULL) == -1) return -1;
    return 0;
}

void msleep(int millisec)
{
    struct timespec tt;
    tt.tv_sec = millisec / 1000;
    tt.tv_nsec = (millisec % 1000) * 1000000;
    while (nanosleep(&tt, &tt) == -1 && errno == EINTR);
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
    for (int i = size - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        SWAP(deck[i], deck[j]);
    }
}

// --- LOGIKA GRACZA ---

int is_winning(int *hand, int m)
{
    int suit = hand[0] % 4;
    for (int i = 1; i < m; i++) {
        if (hand[i] % 4 != suit) return 0;
    }
    return 1;
}

int choose_card_to_discard(int *hand, int m)
{
    int suit_counts[4] = {0};
    for (int i = 0; i < m; i++) suit_counts[hand[i] % 4]++;

    int best_suit = 0;
    for (int i = 1; i < 4; i++) {
        if (suit_counts[i] > suit_counts[best_suit]) best_suit = i;
    }

    int bad_cards[52];
    int bad_count = 0;
    for (int i = 0; i < m; i++) {
        if (hand[i] % 4 != best_suit) {
            bad_cards[bad_count++] = i;
        }
    }
    
    if (bad_count > 0) return bad_cards[rand() % bad_count];
    return rand() % m; 
}

// ZMIANA: Dodano shared_write_fd
void player_work(int deal_read_fd, int ring_read_fd, int ring_write_fd, int shared_write_fd, int m)
{
    srand(time(NULL) ^ getpid()); 

    int *hand = malloc(m * sizeof(int));
    if (!hand) ERR("malloc");

    int bytes_read = 0;
    while (bytes_read < m * sizeof(int)) {
        ssize_t ret = read(deal_read_fd, ((char*)hand) + bytes_read, m * sizeof(int) - bytes_read);
        if (ret < 0) {
            if (errno == EINTR) continue;
            ERR("read deal");
        }
        if (ret == 0) break; 
        bytes_read += ret;
    }
    close(deal_read_fd); 

    pid_t pid = getpid();
    printf("[%d]: Initial Cards: ", pid);
    for (int i = 0; i < m; i++) printf("%d ", hand[i]);
    printf("\n");

    // --- NOWE (ETAP 4): Pętla warunkowana przez keep_running (Ctrl+C) ---
    // ... wewnątrz player_work ...
    while (keep_running)
    {
        if (is_winning(hand, m))
        {
            printf("[%d]: My ship sails!\n", pid);
            if (write(shared_write_fd, &pid, sizeof(pid_t)) < 0) {
                if (errno != EPIPE) ERR("write to shared pipe");
            }
            break;
        }

        int discard_idx = choose_card_to_discard(hand, m);
        int card_to_pass = hand[discard_idx];

        ssize_t w_ret;
        do {
            w_ret = write(ring_write_fd, &card_to_pass, sizeof(int));
        } while (w_ret < 0 && errno == EINTR);

        if (w_ret < 0) {
            if (errno == EPIPE) break; 
            ERR("write to ring");
        }

        // DODANE LOGI I SPOWALNIACZ:
        printf("  -> [%d] passed card %d\n", pid, card_to_pass);

        ssize_t r_ret;
        do {
            r_ret = read(ring_read_fd, &hand[discard_idx], sizeof(int));
        } while (r_ret < 0 && errno == EINTR);

        if (r_ret < 0) {
            ERR("read from ring");
        } else if (r_ret == 0) { 
            break; 
        }
        
        // DODANE OPÓŹNIENIE (np. 150 milisekund)
        msleep(150);
    }

    free(hand);
}

// --- MAIN ---

int main(int argc, char **argv)
{
    if (argc != 3) usage(argv[0]);

    int n = atoi(argv[1]);
    int m = atoi(argv[2]);

    if (n < 4 || n > 7 || m < 4 || n * m > 52) usage(argv[0]);

    // --- NOWE (ETAP 4): Podpięcie sygnału SIGINT ---
    set_handler(SIG_IGN, SIGPIPE);
    set_handler(sig_handler, SIGINT);
    
    srand(time(NULL)); 

    int deck[52];
    for (int i = 0; i < 52; i++) deck[i] = i;
    shuffle(deck, 52);

    int (*deal_pipes)[2] = malloc(n * sizeof(int[2]));
    int (*ring_pipes)[2] = malloc(n * sizeof(int[2]));
    if (!deal_pipes || !ring_pipes) ERR("malloc");

    // --- NOWE (ETAP 3): Potok współdzielony do ogłaszania zwycięzcy ---
    int shared_pipe[2];
    if (pipe(shared_pipe) == -1) ERR("pipe shared");

    for (int i = 0; i < n; i++) {
        if (pipe(deal_pipes[i]) == -1) ERR("pipe deal");
        if (pipe(ring_pipes[i]) == -1) ERR("pipe ring"); 
    }

    for (int i = 0; i < n; i++)
    {
        pid_t pid = fork();
        if (pid == 0) 
        {
            // Dziecko zamyka odczyt z potoku współdzielonego
            close(shared_pipe[0]);

            int read_ring_idx = i;              
            int write_ring_idx = (i + 1) % n;   

            for (int j = 0; j < n; j++)
            {
                close(deal_pipes[j][1]); 
                if (j != i) close(deal_pipes[j][0]); 

                if (j != read_ring_idx) close(ring_pipes[j][0]);
                if (j != write_ring_idx) close(ring_pipes[j][1]);
            }

            int deal_read_fd = deal_pipes[i][0];
            int ring_read_fd = ring_pipes[read_ring_idx][0];
            int ring_write_fd = ring_pipes[write_ring_idx][1];
            int shared_write_fd = shared_pipe[1];
            
            player_work(deal_read_fd, ring_read_fd, ring_write_fd, shared_write_fd, m);
            
            close(ring_read_fd);
            close(ring_write_fd);
            close(shared_write_fd);
            
            free(deal_pipes);
            free(ring_pipes); 
            exit(EXIT_SUCCESS);
        }
        else if (pid == -1) ERR("fork");
    }

    for (int i = 0; i < n; i++)
    {
        close(deal_pipes[i][0]);
        close(ring_pipes[i][0]);
        close(ring_pipes[i][1]);
    }
    
    // Serwer zamyka zapis do potoku współdzielonego (będzie z niego tylko czytał)
    close(shared_pipe[1]);

    int deck_index = 0;
    for (int i = 0; i < n; i++)
    {
        int *player_cards = &deck[deck_index];
        ssize_t w_ret;
        do {
            w_ret = write(deal_pipes[i][1], player_cards, m * sizeof(int));
        } while (w_ret < 0 && errno == EINTR);

        if (w_ret < 0) ERR("write cards");
        
        deck_index += m;
        close(deal_pipes[i][1]); 
    }

    // --- NOWE (ETAP 3): Serwer czeka na PID od zwycięzcy ---
    pid_t winner_pid;
    ssize_t ret;
    do {
        ret = read(shared_pipe[0], &winner_pid, sizeof(pid_t));
    } while (ret < 0 && errno == EINTR);

    if (ret > 0) {
        printf("Server: [%d] won!\n", winner_pid);
    }
    
    close(shared_pipe[0]);

    // Oczekujemy, aż kaskada zamknie wszystkie procesy dzieci (lub aż padną od Ctrl+C)
    while (wait(NULL) > 0 || errno == EINTR) {
        errno = 0; 
    }

    free(deal_pipes);
    free(ring_pipes); 
    printf("Server: Open descriptors at exit = %d\n", count_descriptors());

    return EXIT_SUCCESS;
}