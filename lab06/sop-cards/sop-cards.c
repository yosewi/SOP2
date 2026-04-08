#define _GNU_SOURCE
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#define UNUSED(x) ((void)(x))
#define ERR(source) (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

#define MIN_N 2
#define MAX_N 5
#define MIN_M 5
#define MAX_M 10
#define RESIGN_PROBABILITY 5
#define PLAYING 0
#define RESIGNED 1
#define INVALID_CARD -1
#define PERM 0666
#define SHM_NAME "/sop-cards"
#define SHM_SIZE(n, type) (sizeof(shm_data_t) + (n) * sizeof(type))

typedef struct player_data{
    int card;
    int score;
    int active;
} player_data_t;

typedef struct shared_data{
    pthread_barrier_t begin_barrier;
    pthread_barrier_t game_barrier;
    int n;
    player_data_t player_data[];
} shared_data_t;

void usage(char *name)
{
    fprintf(stderr, "USAGE: %s %d <= N <= %d, %d <= M <= %d\n", name, MIN_N, MAX_N, MIN_M, MAX_M);
    exit(EXIT_FAILURE);
}

int should_resign(int probability)
{
    return rand() % probability == 0;
}

int draw_card(int *cards, int *last)
{
    if (*last < 0)
        ERR("draw_card");

    int card_idx = rand() % (*last + 1);
    int drawn = cards[card_idx];
    for (int i = card_idx; i < *last; i++)
    {
        cards[i] = cards[i + 1];
    }
    cards[(*last)--] = drawn;
    return drawn;
}

void create_cards(int *cards, int m)
{
    for (int i = 0; i < m; i++)
        cards[i] = i + 1;
}

void player_work(shared_data_t *shm_ptr, int id, int m){
    srand(time(NULL) * getpid());
    int drawn, last = m-1, last_score = 0;

    fprintf(stdout, "Player [%d]: Joined the game!\n", id);

    int cards[m];
    create_cards(cards, m);

    for(int i = 1;i<=m;i++){
        drawn = draw_card(cards, &last);
        shm_ptr->player_data[id].card = drawn;
        fprintf(stdout, "Player [%d]: Drawing card %d in round %d!\n", id, drawn, i);

        pthread_barrier_wait(&shm_ptr->begin_barrier);
        pthread_barrier_wait(&shm_ptr->game_barrier);

        fprintf(stdout, "Player [%d]: Scoring %d in round %d!\n", id, shm_ptr->player_data[id].score - last_score, i);
        last_score = shm_ptr->player_data[id].score;
        shm_ptr->player_data[id].active = should_resign(RESIGN_PROBABILITY);

        pthread_barrier_wait(&shm_ptr->begin_barrier);
        pthread_barrier_wait(&shm_ptr->game_barrier);

        if(shm_ptr->player_data[id].active == RESIGNED){
            fprintf(stdout, "Player [%d]: Leaving the game in round %d!\n", id, i);
            break;
        }
    }
    if(munmap(shm_ptr, sizeof(player_data_t) * shm_ptr->n + sizeof(shared_data_t)) == -1){
        ERR("munmap");
    }
}

void game_server_work(shared_data_t *shm_ptr, int m, pthread_barrierattr_t *attr){
    int n = shm_ptr->n;
    int winning_card, winning_count, winning_score, active;

    fprintf(stdout, "Server: The game has started!\n");
    for(int i =1;i<=m;i++){
        active = 0;
        winning_card = -1;
        winning_count = 0;
        fprintf(stdout, "Server: Round %d!\n", i);

        pthread_barrier_wait(&shm_ptr->begin_barrier);

        for(int j =0 ;j<n;j++){
            if(shm_ptr->player_data[j].active == RESIGNED){
                continue;
            }

            fprintf(stdout, "Server: Player %d has drawn %d!\n", j, shm_ptr->player_data[j].card);

            if(shm_ptr->player_data[j].card > winning_card){
                winning_card = shm_ptr->player_data[j].card;
                winning_count = 1;
            }
            else if (shm_ptr->player_data[j].card == winning_card)
            {
                winning_count++;
            }
        }

        fprintf(stdout, "Server: %d is the winning_card!\n", winning_card);
        fprintf(stdout, "Server: There are %d winners this round!\n", winning_count);
        winning_score = n / winning_count;

        for(int j =0;j<n;j++){
            if(shm_ptr->player_data[j].card == winning_card){
                shm_ptr->player_data[j].score += winning_score;
            }
        }

        pthread_barrier_wait(&shm_ptr->game_barrier);
        pthread_barrier_wait(&shm_ptr->begin_barrier);

        for (int j = 0; j < n; j++)
            if (shm_ptr->player_data[j].active == PLAYING)
                active++;

        if (active == 0)
        {
            pthread_barrier_wait(&shm_ptr->game_barrier);
            break;
        }

        pthread_barrier_destroy(&shm_ptr->begin_barrier);
        pthread_barrier_init(&shm_ptr->begin_barrier, attr, active + 1);
        pthread_barrier_wait(&shm_ptr->game_barrier);
        pthread_barrier_destroy(&shm_ptr->game_barrier);
        pthread_barrier_init(&shm_ptr->game_barrier, attr, active + 1);

        fprintf(stdout, "Server: There are %d players left.\n", active);

    }
    fprintf(stdout, "Server: It's time to summarise the game!\n");
    for (int i = 0; i < n; i++)
    {
        fprintf(stdout, "\tPlayer %d: %d points\n", i, shm_ptr->player_data[i].score);
    }
}

int main(int argc, char **argv)
{
    shm_unlink(SHM_NAME);
    if(argc != 3){
        usage(argv[0]);
    }
    int n,m;
    n = atoi(argv[1]);
    m = atoi(argv[2]);
    if(n < 2 || n > 5 || m < 5 || m > 10){
        usage(argv[0]);
    }

    srand(time(NULL) * getpid());

    int shm_fd;
    if((shm_fd = shm_open(SHM_NAME, O_CREAT | O_EXCL | O_RDWR, PERM)) == -1){
        ERR("shm_open");
    }
    if(ftruncate(shm_fd, n * sizeof(player_data_t) + sizeof(shared_data_t)) == -1){
        ERR("ftruncate");
    }

    shared_data_t *shm_ptr;
    if((shm_ptr = mmap(NULL, sizeof(shared_data_t) + n * sizeof(player_data_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0)) == MAP_FAILED){
        ERR("mmap");
    }

    memset(shm_ptr, 0, sizeof(shared_data_t) + n * sizeof(player_data_t));
    pthread_barrierattr_t attr;
    pthread_barrierattr_init(&attr);
    pthread_barrierattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);

    pthread_barrier_init(&shm_ptr->begin_barrier, &attr, n+1);
    pthread_barrier_init(&shm_ptr->game_barrier, &attr, n+1);
    shm_ptr->n = n;

    for(int i = 0;i<n;i++){
        switch(fork()){
            case 0:
                player_work(shm_ptr, i, m);
                exit(EXIT_SUCCESS);
            case -1:
                ERR("fork");
        }
    }

    game_server_work(shm_ptr, m, &attr);

    return EXIT_SUCCESS;
}