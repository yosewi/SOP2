#include "pipe-utils.h"

#define NUMBERS_MAX 49
#define NUMBERS 6
#define BET_PRICE 3
#define RESIGN_PROBABILITY 100
#define MSG_SIZE (NUMBERS * sizeof(int))

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
    fprintf(stderr, "USAGE: %s N T\n", name);
    fprintf(stderr, "N: N >= 1 - number of players\n");
    fprintf(stderr, "T: T >= 1 - number of weeks (iterations)\n");
    exit(EXIT_FAILURE);
}

int should_resign(int probability)
{
    return rand() % probability == 0;
}

int get_reward(int matches)
{
    int rewards[] = {0, 0, 0, 24, 160, 6000, 10000000};
    return rewards[matches];
}

void draw(int *numbers)
{
    int numbers_all[NUMBERS_MAX];
    for (int i = 1; i <= NUMBERS_MAX; ++i)
    {
        numbers_all[i - 1] = i;
    }
    for (int i = 0; i < NUMBERS_MAX - 1; i++)
    {
        int j = i + rand() / (RAND_MAX / (NUMBERS_MAX - i) + 1);
        SWAP(numbers_all[i], numbers_all[j]);
    }
    for (int i = 0; i < NUMBERS; ++i)
    {
        numbers[i] = numbers_all[i];
    }
}

int compare(const int *bet, const int *numbers)
{
    int result = 0;
    for (int i = 0; i < NUMBERS; ++i)
    {
        int number = numbers[i];
        for (int j = 0; j < NUMBERS; ++j)
        {
            if (number == bet[j])
            {
                result++;
            }
        }
    }
    return result;
}

void player_work(int rp, int wp, int t)
{
    srand((unsigned)time(NULL) * getpid());
    int ret, matches, reward;
    pid_t pid = getpid();
    int numbers[NUMBERS], winning[NUMBERS];

    for (int i = 0; i < t; i++)
    {
        if (should_resign(RESIGN_PROBABILITY))
        {
            fprintf(stdout, "[%d]: This is a waste of money!\n", getpid());
            break;
        }
        
        draw(numbers);
        
        if ((ret = write(wp, &pid, sizeof(pid_t))) < 0) {
            if (errno == EPIPE) break;
            ERR("write pid");
        }
        if ((ret = write(wp, numbers, MSG_SIZE)) < 0) {
            if (errno == EPIPE) break;
            ERR("write numbers");
        }
        
        if ((ret = read(rp, winning, MSG_SIZE)) < 0) {
            ERR("read winning");
        } else if (ret == 0) {
            break;
        }

        matches = compare(winning, numbers);
        reward = get_reward(matches);
        
        if (reward > 0) {
            fprintf(stdout, "[%d]: I won %d$\n", getpid(), reward);
        }
    }
}

void totalisator_work(int *rps, int *wps, int n, int t)
{
    srand((unsigned)time(NULL) * getpid());
    int ret, total_bet = 0, total_reward = 0, matches, reward, any;
    pid_t player_pid;
    int player_numbers[NUMBERS], winning[NUMBERS];

    for (int j = 0; j < t; j++)
    {
        any = 0;
        draw(winning);
        for (int i = 0; i < n; i++)
        {
            if (rps[i] == 0) continue;
            else any = 1;

            if ((ret = read(rps[i], &player_pid, sizeof(pid_t))) < 0) ERR("read pid");
            if (ret == 0)
            {
                close_pipe(rps[i]);
                rps[i] = 0;
                continue;
            }
            
            if ((ret = read(rps[i], player_numbers, MSG_SIZE)) < 0) ERR("read numbers");
            if (ret == 0) {
                close_pipe(rps[i]);
                rps[i] = 0;
                continue;
            }

            fprintf(stdout, "Sport Totaliser: [%d] bet: {%d, %d, %d, %d, %d, %d}\n",
                    player_pid, player_numbers[0], player_numbers[1], player_numbers[2],
                    player_numbers[3], player_numbers[4], player_numbers[5]);
            
            matches = compare(winning, player_numbers);
            reward = get_reward(matches);
            total_reward += reward;
            total_bet += BET_PRICE;
            
            errno = 0;
            if ((ret = write(wps[i], winning, MSG_SIZE)) < 0)
            {
                if (errno == EPIPE) 
                {
                    close_pipe(wps[i]);
                    wps[i] = 0;
                }
                else ERR("write");
            }
        }
        
        if (any == 0) break;

        fprintf(stdout, "Sport Totaliser: {%d, %d, %d, %d, %d, %d} are today lucky numbers!\n",
                winning[0], winning[1], winning[2],
                winning[3], winning[4], winning[5]);
    }
    
    fprintf(stdout, "Sport Totaliser:\n\tTotal bet: %d$\n\tTotal reward: %d$\n", total_bet, total_reward);
}

int main(int argc, char** argv){
    if(argc != 3){
        usage(argv[0]);
    }

    set_handler(SIG_IGN, SIGPIPE);

    int N, T;
    N = atoi(argv[1]);
    T = atoi(argv[2]);
    if(N < 1 || T < 1){
        usage(argv[0]);
    }

    pipe_t* to_player;
    pipe_t* from_player;
    to_player = malloc(sizeof(pipe_t) * N);
    from_player = malloc(sizeof(pipe_t) * N);
    if(to_player == NULL || from_player == NULL){
        ERR("malloc");
    }

    create_pipes(to_player, N);
    create_pipes(from_player, N);

    int res;
    for(int i = 0;i<N;i++){
        res = fork();
        if(res == 0){
            fprintf(stderr, "[%d]: I'm going to play Lotto!\n", getpid());
            close_pipes_except(to_player, N, i, READ);
            close_pipes_except(from_player, N, i, WRITE);
            int rp = to_player[i][READ];
            int wp = from_player[i][WRITE];
            player_work(rp, wp, T);
            close(to_player[i][READ]);
            close(to_player[i][WRITE]);
            free(to_player);
            free(from_player);
            exit(EXIT_SUCCESS);
        }
        else if(res == -1){
            ERR("fork");
        }
    }

    close_pipes_all_one_end(to_player, N, READ);
    close_pipes_all_one_end(from_player, N, WRITE);

    int rps[N];
    int wps[N];
    for(int i = 0;i<N;i++){
        rps[i] = from_player[i][READ];
        wps[i] = to_player[i][WRITE];
    }

    totalisator_work(rps, wps, N, T);

    for(int i = 0;i<N;i++){
        if(rps[i] != 0){
            close(rps[i]);
        }
        if(wps[i] != 0){
            close(wps[i]);
        }
    }

    while(wait(NULL) > 0) {};


    exit(EXIT_SUCCESS);
}