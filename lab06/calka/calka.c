#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>      // Dodane do O_CREAT itd.
#include <sys/mman.h>   // Dodane do shm_open, mmap
#include <sys/stat.h>   // Dodane do trybów (0666)
#include <semaphore.h>  // Dodane do sem_open
#include <signal.h>     // Dodane do kill, SIGKILL
#include <string.h>     // Dodane do memset
#include <errno.h>

#define ERR(source) \
    (perror(source), fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), kill(0, SIGKILL), exit(EXIT_FAILURE))
#define SHM_NAME "/sop_shm_integral" 
#define SEM_NAME "/sop_sem_integral"

volatile sig_atomic_t keep_running = 1;

void sigint_handler(int sig){
    keep_running = 0;
}

typedef struct shared_data{
    int procCount;
    pthread_mutex_t mxprocCount;
    int total_randomized_points;
    int hit_points;
    float a;
    float b;
    pthread_mutex_t mxResults;
} shared_data_t;

// Values of this function are in range (0,1]
double func(double x)
{
    usleep(2000);
    return exp(-x * x);
}

/**
 * It counts hit points by Monte Carlo method.
 * Use it to process one batch of computation.
 * @param N Number of points to randomize
 * @param a Lower bound of integration
 * @param b Upper bound of integration
 * @return Number of points which was hit.
 */
int randomize_points(int N, float a, float b)
{
    int result = 0;
    for (int i = 0; i < N; ++i)
    {
        double rand_x = ((double)rand() / RAND_MAX) * (b - a) + a;
        double rand_y = ((double)rand() / RAND_MAX);
        double real_y = func(rand_x);

        if (rand_y <= real_y)
            result++;
    }
    return result;
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

/**
 * This function calculates approximation of integral from counters of hit and total points.
 * @param total_randomized_points Number of total randomized points.
 * @param hit_points Number of hit points.
 * @param a Lower bound of integration
 * @param b Upper bound of integration
 * @return The approximation of integral
 */
double summarize_calculations(uint64_t total_randomized_points, uint64_t hit_points, float a, float b)
{
    return (b - a) * ((double)hit_points / (double)total_randomized_points);
}

/**
 * This function locks mutex and can sometime die (it has 2% chance to die).
 * It cannot die if lock would return an error.
 * It doesn't handle any errors. It's users responsibility.
 * Use it only in STAGE 4.
 *
 * @param mtx Mutex to lock
 * @return Value returned from pthread_mutex_lock.
 */
int random_death_lock(pthread_mutex_t* mtx)
{
    int ret = pthread_mutex_lock(mtx);
    if (ret)
        return ret;

    // 2% chance to die
    if (rand() % 20 == 0)
        abort();
    return ret;
}

void usage(char* argv[])
{
    printf("%s a b N - calculating integral with multiple processes\n", argv[0]);
    printf("a - Start of segment for integral (default: -1)\n");
    printf("b - End of segment for integral (default: 1)\n");
    printf("N - Size of batch to calculate before reporting to shared memory (default: 1000)\n");
}

int main(int argc, char* argv[])
{
    srand(getpid() * time(NULL));
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigint_handler;
    if(sigaction(SIGINT, &sa, NULL) == -1){
        ERR("sigaction");
    }

    float a = -1.0f;
    float b = 1.0f;
    int N = 1000;

    if (argc > 1) a = atof(argv[1]);
    if (argc > 2) b = atof(argv[2]);
    if (argc > 3) N = atoi(argv[3]);

    sem_t* sem_ptr;
    shared_data_t *shared_data;
    if((sem_ptr = sem_open(SEM_NAME, O_CREAT, 0666, 1)) == SEM_FAILED){
        ERR("sem-open");
    }
    sem_wait(sem_ptr);

    errno = 0;
    int shm_fd;
    shm_fd = shm_open(SHM_NAME, O_RDWR | O_EXCL | O_CREAT, 0666);
    if(shm_fd >= 0){
        if(ftruncate(shm_fd, sizeof(shared_data_t))){
            sem_post(sem_ptr);
            shm_unlink(SHM_NAME);
            ERR("ftruncate");
        }
        if((shared_data = mmap(NULL, sizeof(shared_data_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0)) == MAP_FAILED){
            sem_post(sem_ptr);
            shm_unlink(SHM_NAME);
            ERR("mmap");
        }
        close(shm_fd);
        memset(shared_data, 0, sizeof(shared_data_t));
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
        pthread_mutexattr_setrobust(&attr, PTHREAD_MUTEX_ROBUST);

        pthread_mutex_init(&shared_data->mxprocCount, &attr);
        pthread_mutex_init(&shared_data->mxResults, &attr);
        pthread_mutexattr_destroy(&attr);

        shared_data->procCount = 1;

        shared_data->a = a;
        shared_data->b = b;
        shared_data->total_randomized_points = 0;
        shared_data->hit_points = 0;

        sem_post(sem_ptr);
    } 
    else if(errno == EEXIST){
        if((shm_fd = shm_open(SHM_NAME, O_RDWR, 0666)) < 0){
            sem_post(sem_ptr);
            ERR("shm_open");
        }
        if((shared_data = mmap(NULL, sizeof(shared_data_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0)) == MAP_FAILED){
            sem_post(sem_ptr);
            ERR("mmap");
        }

        close(shm_fd);

        if(shared_data->a != a || shared_data->b != b){
            printf("Zla granica calkowania\n");
            sem_post(sem_ptr);
            munmap(shared_data, sizeof(shared_data_t));
            exit(EXIT_FAILURE);
        }

        sem_post(sem_ptr);

        int ret;
        ret = random_death_lock(&shared_data->mxprocCount);
        if(ret == EOWNERDEAD){
            pthread_mutex_consistent(&shared_data->mxprocCount);
            shared_data->procCount--;
        }
        shared_data->procCount++;
        pthread_mutex_unlock(&shared_data->mxprocCount);
    }
    else{
        sem_post(sem_ptr);
        ERR("shm_open");
    }

    int current_procs;
    int ret_print = random_death_lock(&shared_data->mxprocCount);
    if(ret_print == EOWNERDEAD){
        pthread_mutex_consistent(&shared_data->mxprocCount);
        shared_data->procCount--;
    }
    current_procs = shared_data->procCount;
    pthread_mutex_unlock(&shared_data->mxprocCount);
    printf("Wspolpracujace procesy: %d\n", current_procs);
    
    for(int i = 0;i<3 && keep_running;i++){
        int hits = randomize_points(N, a, b);
        int ret = random_death_lock(&shared_data->mxResults);
        if(ret == EOWNERDEAD){
            pthread_mutex_consistent(&shared_data->mxResults);
            int ret_proc = random_death_lock(&shared_data->mxprocCount);
            if(ret_proc == EOWNERDEAD){
                pthread_mutex_consistent(&shared_data->mxprocCount);
                shared_data->procCount--;
            }
            shared_data->procCount--;
            pthread_mutex_unlock(&shared_data->mxprocCount);
        }
        shared_data->total_randomized_points += N;
        shared_data->hit_points += hits;
        printf("Proces %d: Paczka %d/3. Wylosowano: %d, trafiono: %d\n", 
            getpid(), i+1, shared_data->total_randomized_points, shared_data->hit_points);
        pthread_mutex_unlock(&shared_data->mxResults);
    }

    if(!keep_running){
        printf("%d: Otrzymano SIGINT. Koniec\n", getpid());
    }

    int ret_end = random_death_lock(&shared_data->mxprocCount);
    if (ret_end == EOWNERDEAD) {
        pthread_mutex_consistent(&shared_data->mxprocCount);
        shared_data->procCount--;
    }
    shared_data->procCount--;
    int remaining_procs = shared_data->procCount;
    pthread_mutex_unlock(&shared_data->mxprocCount);

    if(remaining_procs == 0){
        double approx = summarize_calculations(shared_data->total_randomized_points, shared_data->hit_points, shared_data->a, shared_data->b);
        printf("Wyniki aproksymacji: %f\n", approx);
        pthread_mutex_destroy(&shared_data->mxResults);
        pthread_mutex_destroy(&shared_data->mxprocCount);
        munmap(shared_data, sizeof(shared_data_t));
        shm_unlink(SHM_NAME);
        sem_close(sem_ptr);
        sem_unlink(SEM_NAME);
    }
    else{
        munmap(shared_data, sizeof(shared_data_t));
        sem_close(sem_ptr);
    }
    return EXIT_SUCCESS;
}