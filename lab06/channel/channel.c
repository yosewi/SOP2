
#include "channel.h"
#include "macros.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <semaphore.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

channel_t* channel_open(const char* path) {

    // Use sem_name to initialize semaphore accompanied by channel
    char sem_name[PATH_MAX] = "sem_";
    strncpy(sem_name + 4, path, PATH_MAX - 5);
    sem_name[PATH_MAX-1] = 0;

    sem_t *sem = sem_open(sem_name, O_CREAT, 0666, 1);
    if(sem == SEM_FAILED){
        ERR("sem_open");
    }
    sem_wait(sem);

    int fd = shm_open(path, O_CREAT | O_RDWR, 0666);
    if(fd == -1){
        sem_post(sem);
        ERR("shm_open");
    }

    if(ftruncate(fd, sizeof(channel_t)) == -1){
        sem_post(sem);
        ERR("ftruncate");
    }

    channel_t *channel = mmap(NULL, sizeof(channel_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(channel == MAP_FAILED){
        sem_post(sem);
        ERR("mmap");
    }
    close(fd);

    if(channel->status == CHANNEL_UNINITIALIZED){
        pthread_mutexattr_t mattr;
        pthread_mutexattr_init(&mattr);
        pthread_mutexattr_setpshared(&mattr, PTHREAD_PROCESS_SHARED);
        pthread_mutex_init(&channel->data_mtx, &mattr);
        pthread_mutexattr_destroy(&mattr);

        pthread_condattr_t cattr;
        pthread_condattr_init(&cattr);
        pthread_condattr_setpshared(&cattr, PTHREAD_PROCESS_SHARED);
        pthread_cond_init(&channel->producer_cv, &cattr);
        pthread_cond_init(&channel->consumer_cv, &cattr);
        pthread_condattr_destroy(&cattr);

        channel->status = CHANNEL_EMPTY;
        channel->length = 0;
    }

    sem_post(sem);
    sem_close(sem);

    return channel;
}

void channel_close(channel_t* channel) {
    // Implement stage 1 here
    if(munmap(channel, sizeof(channel_t)) == -1){
        ERR("munmap");
    }
}

int channel_produce(channel_t* channel, const char* produced_data, uint16_t length) {
    // Implement stage 3 here
    pthread_mutex_lock(&channel->data_mtx);
    while(channel->status == CHANNEL_OCCUPIED){
        pthread_cond_wait(&channel->producer_cv, &channel->data_mtx);
    }

    memcpy(channel->data, produced_data, length);
    channel->length = length;
    channel->status = CHANNEL_OCCUPIED;

    pthread_cond_signal(&channel->consumer_cv);
    pthread_mutex_unlock(&channel->data_mtx);

    return 0;
}

int channel_consume(channel_t* channel, char* consumed_data, uint16_t* length) {
    // Implement stage 2 here
    pthread_mutex_lock(&channel->data_mtx);
    while(channel->status == CHANNEL_EMPTY){
        pthread_cond_wait(&channel->consumer_cv, &channel->data_mtx);
    }

    if(channel->status == CHANNEL_DEPLETED){
        pthread_mutex_unlock(&channel->data_mtx);
        return 1;
    }

    memcpy(consumed_data, channel->data, channel->length);
    *length = channel->length;

    channel->status = CHANNEL_EMPTY;
    pthread_cond_signal(&channel->producer_cv);
    pthread_mutex_unlock(&channel->data_mtx);

    return 0;
}
