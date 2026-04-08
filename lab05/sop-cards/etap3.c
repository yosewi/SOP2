#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include "pipe-utils.h"

#define MIN_N 2
#define MAX_N 5
#define MIN_M 5
#define MAX_M 10
#define RESIGN_PROBABILITY 5
#define MSG_SIZE (16 * sizeof(char))

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

void usage(char *name)
{
    fprintf(stderr, "USAGE: %s %d <= N <= %d, %d <= M <= %d\n", name, MIN_N, MAX_N, MIN_M, MAX_M);
    exit(EXIT_FAILURE);
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
    while (nanosleep(&tt, &tt) == -1)
    {
    }
}

void game_server_work(int *rps, int *wps, int n, int m){
    int res;
    char buf[MSG_SIZE];
    char* new_round = "new_round";
    memset(buf, 0, MSG_SIZE);
    for(int j = 1;j<=m;j++){
        printf("\n--- NEW ROUND %d ---\n", j);
        memset(buf, 0, MSG_SIZE);
        snprintf(buf, MSG_SIZE, "%s", new_round);
        for(int i = 0;i<n;i++){
            if(wps[i] == 0) continue;
            res = write(wps[i], buf, MSG_SIZE);
        if(res == -1 && errno == EPIPE){
            close_pipe(wps[i]);
            wps[i] = 0;
            return;
        }
        else if(res == -1){
            ERR("write");
        }
    }

        for(int i = 0;i<n;i++){
            if(rps[i] == 0) continue;
            memset(buf, 0, MSG_SIZE);
        res = read(rps[i], buf, MSG_SIZE);
        if(res == -1){
            ERR("read");
        }
        else if(res == 0){
            close_pipe(rps[i]);
            rps[i] = 0;
            continue;
        }
        else if(res > 0){
            printf("Got number %d from player %d\n", atoi(buf), i);
        }
        }
    }
}



void player_work(int rp, int wp, int player_idx, int m){
    srand(getpid() * time(NULL));

    fprintf(stdout, "Player %d: Joined the game!\n", player_idx);

    char* new_round = "new_round";
    char msg[MSG_SIZE];
    int cards[m];
    for(int i = 0;i<m;i++){
        cards[i] = i+1;
    }

    for(int i = 1;i<=m;i++){

        int res;
        char buf[MSG_SIZE];
        memset(buf, 0, MSG_SIZE);

        res = read(rp, buf, MSG_SIZE);
        if(res == -1){
            ERR("res");
        }
        else if(res == 0){
            close_pipe(rp);
            rp = 0;
            continue;
        }
        else if(res > 0){
            if(strcmp(buf, new_round) != 0){
                ERR("new_round");
            }
        }
        memset(buf, 0, MSG_SIZE);
        int liczba = rand() % m;
        snprintf(buf, MSG_SIZE, "%d", liczba);
        res = write(wp, buf, MSG_SIZE);
        if(res == -1 && errno == EPIPE){
            close_pipe(wp);
            return;
        }
        else if(res == -1){
            ERR("write");
        }
    }
}

int main(int argc, char **argv){
    if(argc != 3){
        usage(argv[0]);
    }
    int n,m;
    n = atoi(argv[1]);
    m = atoi(argv[2]);
    if(n < 2 || n > 5 || m < 5 || m > 10){
        usage(argv[0]);
    }

    set_handler(SIG_IGN, SIGPIPE);

    pipe_t to_player[n];
    pipe_t from_player[n];
    create_pipes(to_player, n);
    create_pipes(from_player, n);

    for(int i = 0;i<n;i++){
        int res = fork();
            if(res == 0){
                close_pipes_except(to_player, n, i, READ);
                close_pipes_except(from_player, n, i, WRITE);
                int rp = to_player[i][READ];
                int wp = from_player[i][WRITE];
                player_work(rp, wp, i, m);
                close_pipe(rp);
                close_pipe(wp);
                exit(EXIT_SUCCESS);
            }
            if(res == -1){
                ERR("fork");
            }
        }
        close_pipes_all_one_end(to_player, n, READ);
        close_pipes_all_one_end(from_player, n, WRITE);

        int rps[n];
        int wps[n];
        for(int i = 0;i<n;i++){
            rps[i] = from_player[i][READ];
            wps[i] = to_player[i][WRITE];
        }

        game_server_work(rps, wps, n,m);

        for(int i = 0;i<n;i++){
            if(rps[i] != 0){
                close_pipe(rps[i]);
            }
            if(wps[i] != 0){
                close_pipe(wps[i]);
            }
        }

        while(wait(NULL) > 0){

        };

        return EXIT_SUCCESS;
    }