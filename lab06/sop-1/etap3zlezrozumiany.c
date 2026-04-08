#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <sys/stat.h>


#define ERR(source)                                     \
    do                                                  \
    {                                                   \
        fprintf(stderr, "%s:%d\n", __FILE__, __LINE__); \
        perror(source);                                 \
        kill(0, SIGKILL);                               \
        exit(EXIT_FAILURE);                             \
    } while (0)

#define MAX_LENGTH 1000
#define MAX_PROC 10

void usage(char* program_name)
{
    fprintf(stderr, "Usage: \n");
    fprintf(stderr, "\t%s file_name\n", program_name);
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

typedef struct signs {
    int count[256];
} signs_t;

typedef struct forparent{
    signs_t signs[MAX_PROC];
} forparent_t;

void program(char* log, off_t size, signs_t* signs) {
    for(off_t i = 0; i < size; i++) {
        unsigned char c = (unsigned char)log[i];
        signs->count[c]++;
    }
}

void create_children(int N, int M, forparent_t* shared_data, char* file){
    while(N-- > 0){
        switch(fork()){
            case 0:
                int fd;
                if((fd = open(file, O_RDWR, 0666)) == -1){
                    ERR("open");
                }
                struct stat filestat;
                if(fstat(fd, &filestat) == -1){
                    ERR("fstat");
                }
                char *log;
                if((log = mmap(NULL, filestat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED){
                    ERR("mmap");
                }
                program(log, filestat.st_size, &shared_data->signs[N]);
                if(munmap(log, filestat.st_size)){
                    ERR("munmap");
                }
                exit(EXIT_SUCCESS);
            case -1:
                perror("fork");
                exit(EXIT_FAILURE);
        }
    }
}

int main(int argc, char** argv) { 
    if(argc != 3){
        usage(argv[0]);
    }
    char* file;
    int N = atoi(argv[2]);
    file = argv[1];
    int fd;
    if((fd = open(file, O_RDWR, 0666)) == -1){
        ERR("open");
    }
    struct stat filestat;
    if(fstat(fd, &filestat) == -1){
        ERR("fstat");
    }
    char *log;
    if((log = mmap(NULL, filestat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED){
        ERR("mmap");
    }
    forparent_t *shared_data;
    if((shared_data = mmap(NULL, sizeof(forparent_t), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0)) == MAP_FAILED){
        ERR("mmap");
    }
    for(int i = 0;i<filestat.st_size;i++){
        printf("%c", log[i]);
    }
    printf("\n");
    create_children(N, N, shared_data, file);
    while (wait(NULL) > 0);
    for(int j = 0;j<N;j++){
        printf("results from child %d\n", j);
        for(int i = 0;i<256;i++){
        if(shared_data->signs[j].count[i] > 0){
            printf("%c: %d\n", i, shared_data[j]->signs[j].count[i]);
        }
    }
    }
    if(munmap(log, filestat.st_size)){
        ERR("munmap");
    }
    if(munmap(shared_data, sizeof(forparent_t))){
        ERR("munmap");
    }
    return EXIT_SUCCESS;
}