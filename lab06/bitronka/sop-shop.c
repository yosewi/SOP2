#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define SHOP_FILENAME "./shop"
#define MIN_SHELVES 8
#define MAX_SHELVES 256
#define MIN_WORKERS 1
#define MAX_WORKERS 64

#define ERR(source)                                     \
    do                                                  \
    {                                                   \
        fprintf(stderr, "%s:%d\n", __FILE__, __LINE__); \
        perror(source);                                 \
        kill(0, SIGKILL);                               \
        exit(EXIT_FAILURE);                             \
    } while (0)

#define SWAP(x, y)         \
    do                     \
    {                      \
        typeof(x) __x = x; \
        typeof(y) __y = y; \
        x = __y;           \
        y = __x;           \
    } while (0)

typedef struct shared_data{
    pthread_mutex_t mutexArr[MAX_SHELVES];
    int sorted;
    pthread_mutex_t mxSorted;
    int dead;
    pthread_mutex_t mxDead;
} shared_data_t;

void usage(char* program_name)
{
    fprintf(stderr, "Usage: \n");
    fprintf(stderr, "\t%s n m\n", program_name);
    fprintf(stderr, "\t  n - number of items (shelves), %d <= n <= %d\n", MIN_SHELVES, MAX_SHELVES);
    fprintf(stderr, "\t  m - number of workers, %d <= m <= %d\n", MIN_WORKERS, MAX_WORKERS);
    exit(EXIT_FAILURE);
}

void msleep(unsigned int milli)
{
    time_t sec = (int)(milli / 1000);
    milli = milli - (sec * 1000);
    struct timespec ts = {0};
    ts.tv_sec = sec;
    ts.tv_nsec = milli * 1000000L;
    if (nanosleep(&ts, &ts))
        ERR("nanosleep");
}

void shuffle(int* array, int n)
{
    for (int i = n - 1; i > 0; i--)
    {
        int j = rand() % (i + 1);
        SWAP(array[i], array[j]);
    }
}

void print_array(int* array, int n)
{
    for (int i = 0; i < n; ++i)
    {
        printf("%3d ", array[i]);
    }
    printf("\n");
}

void create_children(int M, int num, int N, int* log, shared_data_t* shared_data){
    while(num-- > 0){
        switch(fork()){
            case 0:
                printf("%d Worker reports for a night shift\n", getpid());
                srand(time(NULL) * getpid());
                while(shared_data->sorted!=1){
                    pthread_mutex_lock(&shared_data->mxSorted);
                    if(shared_data->sorted){
                        pthread_mutex_unlock(&shared_data->mxSorted);
                        break;
                    }
                    pthread_mutex_unlock(&shared_data->mxSorted);

                    int x = rand() % N;
                int y = rand() % N;
                while(x == y){
                    y = rand() % N;
                }
                int first, second;
                if(x < y){
                    first = x;
                    second = y;
                }
                else{
                    first = y;
                    second = x;
                }
                int ret = pthread_mutex_lock(&shared_data->mutexArr[first]);
                if(ret == EOWNERDEAD){
                    printf("%d Found a dead body in aisle %d\n", getpid(), first);
                    pthread_mutex_lock(&shared_data->mxDead);
                    shared_data->dead++;
                    pthread_mutex_unlock(&shared_data->mxDead);
                    pthread_mutex_consistent(&shared_data->mutexArr[first]);

                }
                ret = pthread_mutex_lock(&shared_data->mutexArr[second]);
                if(ret == EOWNERDEAD){
                    printf("%d Found a dead body in aisle %d\n", getpid(), second);
                    pthread_mutex_lock(&shared_data->mxDead);
                    shared_data->dead++;
                    pthread_mutex_unlock(&shared_data->mxDead);
                    pthread_mutex_consistent(&shared_data->mutexArr[second]);
                }
                if(x < y){
                    if(log[x] > log[y]){
                        if(rand() % 100 == 0){
                            printf("[%d] Trips over pallet and dies\n", getpid());
                            abort();
                        }
                        SWAP(log[x], log[y]);
                        msleep(100);
                    }
                }
                if(y < x){
                    if(log[x] < log[y]){
                        if(rand() % 100 == 0){
                            printf("[%d] Trips over pallet and dies\n", getpid());
                            
                            abort();
                        }
                        SWAP(log[x], log[y]);
                        msleep(100);
                    }
                }
                pthread_mutex_unlock(&shared_data->mutexArr[second]);
                pthread_mutex_unlock(&shared_data->mutexArr[first]);
                }

                exit(EXIT_SUCCESS);
            case -1:
                perror("fork");
                exit(EXIT_FAILURE);
        }
    }
    switch(fork()){
        case 0:
            printf("%d Manager reports for a night shift\n", getpid());
            int sorted = 0;
            while(!sorted){
                msync(log, N * sizeof(int), MS_SYNC);

                int ret;
                for(int i = 0;i<N;i++){
                    ret = pthread_mutex_lock(&shared_data->mutexArr[i]);
                    if(ret == EOWNERDEAD){
                        printf("%d Found a dead body in aisle %d\n", getpid(), i);
                        pthread_mutex_lock(&shared_data->mxDead);
                        shared_data->dead++;
                        pthread_mutex_unlock(&shared_data->mxDead);
                        pthread_mutex_consistent(&shared_data->mutexArr[i]);
                    }
                }

                print_array(log, N);

                sorted = 1;
                for(int i = 0;i<N;i++){
                    if(log[i] != i){
                        sorted = 0;
                        break;
                    }
                }

                for(int i = 0;i<N;i++){
                    pthread_mutex_unlock(&shared_data->mutexArr[i]);
                }

                pthread_mutex_lock(&shared_data->mxDead);
                printf("%d Workers alive: %d\n", getpid(), (M - shared_data->dead/2));
                if(shared_data->dead / 2 == M){
                    printf("%d All workers died, I hate my job\n", getpid());
                    pthread_mutex_unlock(&shared_data->mxDead);
                    exit(EXIT_SUCCESS);
                }
                pthread_mutex_unlock(&shared_data->mxDead);

                msleep(500);
            }

            printf("%d The shop shelves are sorted\n", getpid());

            pthread_mutex_lock(&shared_data->mxSorted);
            shared_data->sorted = 1;
            pthread_mutex_unlock(&shared_data->mxSorted);
            exit(EXIT_SUCCESS);
        case -1:
            perror("fork");
            exit(EXIT_FAILURE);
    }
}

int main(int argc, char** argv) { 
    int N, M;
    srand(time(NULL) * getpid());

    if(argc != 3){
        usage(argv[0]);
    }

    N = atoi(argv[1]);
    M = atoi(argv[2]);

    if(N < 8 || N > 256 || M < 1 || M > 64){
        usage(argv[0]);
    }

    int fd;
    if((fd = open(SHOP_FILENAME, O_CREAT | O_TRUNC | O_RDWR, 0666)) == -1){
        ERR("open");
    }
    if(ftruncate(fd, N * sizeof(int))){
        ERR("ftruncate");
    }
    int* log;
    if((log = mmap(NULL, N*sizeof(int), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED){
        ERR("mmap");
    }
    if(close(fd)){
        ERR("close");
    }
    int* shelf = log;
    for(int i = 0;i<N;i++){
        shelf[i] = i;
    }

    shared_data_t* shared_data;
    if((shared_data = mmap(NULL, sizeof(shared_data_t), PROT_READ | PROT_WRITE, MAP_SHARED | 
        MAP_ANONYMOUS, -1, 0)) == MAP_FAILED){
            ERR("mmap");
        }

    pthread_mutexattr_t mutex_attr;
    pthread_mutexattr_init(&mutex_attr);
    pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
    pthread_mutexattr_setrobust(&mutex_attr, PTHREAD_MUTEX_ROBUST);

    shared_data->sorted = 0;
    shared_data->dead = 0;

    for(int i = 0;i<N;i++){
        pthread_mutex_init(&shared_data->mutexArr[i], &mutex_attr);
    }

    pthread_mutex_init(&shared_data->mxSorted, &mutex_attr);
    pthread_mutex_init(&shared_data->mxDead, &mutex_attr);

    pthread_mutexattr_destroy(&mutex_attr);

    shuffle(shelf, N);
    print_array(shelf,N);

    create_children(M, M, N, shelf, shared_data);
    while (wait(NULL) > 0);
    print_array(shelf,N);
    printf("Night shift in Bitronka is over\n");

    for(int i = 0;i<N;i++){
        pthread_mutex_destroy(&shared_data->mutexArr[i]);
    }
    pthread_mutex_destroy(&shared_data->mxSorted);
    pthread_mutex_destroy(&shared_data->mxDead);
    if(msync(log, N * sizeof(int), MS_SYNC)){
        ERR("msync");
    }
    if(munmap(log, N * sizeof(int))){
        ERR("munmap");
    }
    if(munmap(shared_data, sizeof(shared_data_t))){
        ERR("munmap");
    }

    return(EXIT_SUCCESS);
}