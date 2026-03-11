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

#define N 3
#define BUFSIZE 11

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

volatile sig_atomic_t last_signal = 0;

void sig_handler(int sig) { last_signal = sig; }

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

void send_initial_message(int wp, char *buffer, int n){
    snprintf(buffer, n, "%d", 1);
    if(write(wp, buffer, n) < 0){
        ERR("write");
    }
    fprintf(stderr, "[%d] initial writing %s\n", getpid(), buffer);
}

void work(int readEnd, int writeEnd, int id){
    srand(getpid());

        int res;
    char buf[BUFSIZE];

    if(id == 0){
        memset(buf, 0, BUFSIZE);
        send_initial_message(writeEnd, buf, BUFSIZE);
    }

    memset(buf, 0, BUFSIZE);
    int val = rand() % 99 + 1;

    while(last_signal != SIGINT){
        memset(buf, 0, BUFSIZE);

        res = read(readEnd, buf, BUFSIZE);

    if(res == -1 && errno == EAGAIN){
        break;
    }
    else if(res == 0){
        break;
    }
    else if(res > 0){
        printf("PID:%d dostal liczbe: %s\n",getpid(), buf);
    }
    else if(res == -1){
        ERR("read");
    }

    msleep(250);

        res = write(writeEnd, &buf, BUFSIZE);
    if(res == -1 && errno == EPIPE){
        close(writeEnd);
        break;
    }
    else if(res == -1){
        ERR("write");
    }
    }
}

void create_pipes_and_fork(int n){
    int *pipes;
    pipes = (int*)malloc(sizeof(int) * 2 * n);
    if(pipes == NULL){
        ERR("pipes");
    }

    for(int i = 0;i<n;i++){
        if(pipe(&pipes[2*i]) == -1){
            ERR("pipe");
        }
    }

    int id = 0;

    for(int i = 1;i<n;i++){
        int res = fork();

        if(res == 0){
            id = i;
            break;
        }
        if(res == -1){
            ERR("fork");
        }
    }

    int readid = (id + n - 1) % n;
    int writeid = id;

    int readfd = pipes[2 * readid];
    int writefd = pipes[2 * writeid + 1];

    for(int i = 0; i<n;i++){
        if(i != readid){
            close(pipes[2*i]);
        }
        if(i != writeid){
            close(pipes[2*i+1]);
        }
    }

    printf("\n---> [ID: %d, PID: %d] GOTOWY! Zostawia otwarte: odczyt z %d, zapis do %d\n\n", 
           id, getpid(), readfd, writefd);

    work(readfd, writefd, id);

    free(pipes);

    close(readfd);
    close(writefd);

    if(id != 0){
        exit(EXIT_SUCCESS);
    }
}

int main(int argc, char** argv){

    set_handler(sig_handler, SIGINT);

    set_handler(SIG_IGN, SIGPIPE);

    create_pipes_and_fork(N);

    while(wait(NULL) > 0);

    return EXIT_SUCCESS;
}