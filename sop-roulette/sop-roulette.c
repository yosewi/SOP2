#include "pipe-utils.h"

#define MIN_N 1
#define MIN_M 100
#define MAX_ROULETTE 37
#define PROBABILITY 10
#define PRIZE_FACTOR 35

typedef unsigned int UINT;
typedef int pipe_t[2];

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
    while (nanosleep(&tt, &tt) == -1)
    {
    }
}

void usage(char *name)
{
    fprintf(stderr, "USAGE: %s N M\n", name);
    fprintf(stderr, "N: N >= 1 - number of players\n");
    fprintf(stderr, "M: M >= 100 - initial amount of money\n");
    exit(EXIT_FAILURE);
}

int should_resign(int probability)
{
    return rand() % probability == 0;
}

void player_work(int wp, int rp, int m){
    srand(time(NULL) * getpid());
    int ret, lucky_number;
    int amount = m;
    int pid = getpid();

    while(amount > 0){
        if (should_resign(PROBABILITY))
        {
            fprintf(stdout, "[%d]: I saved %d$!\n", pid, amount);
            fflush(stdout);
            break;
        }
        int bet = rand() % amount + 1;
        amount -= bet;
        int number = rand() % 37;
        if((ret = write(wp, &pid, sizeof(int))) < 0){
            ERR("write");
        }
        if((ret = write(wp, &number, sizeof(int))) < 0){
            ERR("write");
        }
        if((ret = write(wp, &bet, sizeof(int)))<0){
            ERR("write");
        }
        if((ret = read(rp, &lucky_number, sizeof(int))) < 0){
            ERR("read");
        }
        if(lucky_number == number){
            fprintf(stdout, "[%d]: Whoa, I won %d!\n", pid, 35 * bet);
            fflush(stdout);
            amount += 35 * bet;
        }
    }
    if(amount <= 0){
        fprintf(stdout, "[%d]: I'm broke :(\n", pid);
        fflush(stdout);
    }
}

void server_work(int *wps, int *rps, int n){
    srand(time(NULL) * getpid());
    int player_bet, player_number, player_pid, ret, any;
    while(1){
        int lucky_number = rand() % 37;
        any = 0;
        for(int i = 0;i<n;i++){
            if(rps[i] == 0){
                continue;
            }
            any = 1;
            if((ret = read(rps[i], &player_pid, sizeof(int))) < 0){
                ERR("ret");
            }
            if(ret == 0){
                close_pipe(rps[i]);
                rps[i] = 0;
                continue;
            }
            if((ret = read(rps[i], &player_number, sizeof(int))) < 0){
                ERR("ret");
            }
            if(ret == 0){
                close_pipe(rps[i]);
                rps[i] = 0;
                continue;
            }
            if((ret = read(rps[i], &player_bet, sizeof(int))) < 0){
                ERR("ret");
            }
            if(ret == 0){
                close_pipe(rps[i]);
                rps[i] = 0;
                continue;
            }

            if((ret = write(wps[i], &lucky_number, sizeof(int))) < 0){
                if(errno == EPIPE){
                    if(wps[i] != 0){
                        close_pipe(wps[i]);
                        wps[i] = 0;
                        continue;
                    }
                }
                else{
                    ERR("write");
                }
            }

            fprintf(stdout, "Croupier: [%d] placed %d on a %d!\n", player_pid, player_bet, player_number);
            fflush(stdout);
        }
        if (any == 0)
            break;

        fprintf(stdout, "Croupier: %d is the lucky number!\n", lucky_number);
        fflush(stdout);
    }

    fprintf(stdout, "Croupier: Casino always wins!\n");
    fflush(stdout);
}


int main(int argc, char **argv){
    set_handler(SIG_IGN, SIGPIPE);

    if(argc != 3){
        usage(argv[0]);
    }

    int n,m;
    n = atoi(argv[1]);
    m = atoi(argv[2]);
    if(n < 1 || m < 100){
        usage(argv[0]);
    }

    pipe_t* to_player = malloc(sizeof(pipe_t) * n);
    if(to_player == NULL){
        ERR("malloc");
    }

    pipe_t* from_player = malloc(sizeof(pipe_t) * n);
    if(from_player == NULL){
        ERR("malloc");
    }

    create_pipes(to_player, n);
    create_pipes(from_player, n);

    for(int i = 0;i<n;i++){
        int res = fork();
        if(res == 0){
            fprintf(stdout, "[%d]: I have %d and I'm going to play roulette!\n", getpid(), m);
            close_pipes_except(to_player, n, i, READ);
            close_pipes_except(from_player, n, i, WRITE);
            int rp = to_player[i][READ];
            int wp = from_player[i][WRITE];
            player_work(wp, rp, m);
            close_pipe(rp);
            close_pipe(wp);
            free(to_player);
            free(from_player);
            exit(EXIT_SUCCESS);
        }
        else if(res == -1){
            ERR("fork");
            break;
        }
    }
    close_pipes_all_one_end(to_player, n, READ);
    close_pipes_all_one_end(from_player, n, WRITE);

    int* rps = malloc(sizeof(int) * n);
    int* wps = malloc(sizeof(int) * n);
    if(rps == NULL || wps == NULL){
        ERR("malloc");
    }

    for(int i = 0;i<n;i++){
        rps[i] = from_player[i][READ];
        wps[i] = to_player[i][WRITE];
    }

    free(to_player);
    free(from_player);

    server_work(wps, rps, n);

    for(int i = 0;i<n;i++){
        if(rps[i] != 0){
            close_pipe(rps[i]);
        }
        if(wps[i] != 0){
            close_pipe(wps[i]);
        }
    }

    free(wps);
    free(rps);
    while(wait(NULL) > 0) {};
    exit(EXIT_SUCCESS);
}