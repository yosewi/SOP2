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

#define ERR(source) \
    (fprintf(stderr, "%s:%d\n", __FILE__, __LINE__), perror(source), kill(0, SIGKILL), exit(EXIT_FAILURE))

#define MAX_KNIGHT_NAME_LENGTH 20

typedef struct knight_t
{
    char name[MAX_KNIGHT_NAME_LENGTH];
    int HP;
    int attack;
} knight_t;

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

int count_descriptors()
{
    int count = 0;
    DIR* dir;
    struct dirent* entry;
    struct stat stats;
    if ((dir = opendir("/proc/self/fd")) == NULL)
        ERR("opendir");
    char path[PATH_MAX];
    getcwd(path, PATH_MAX);
    chdir("/proc/self/fd");
    do
    {
        errno = 0;
        if ((entry = readdir(dir)) != NULL)
        {
            if (lstat(entry->d_name, &stats))
                ERR("lstat");
            if (!S_ISDIR(stats.st_mode))
                count++;
        }
    } while (entry != NULL);
    if (chdir(path))
        ERR("chdir");
    if (closedir(dir))
        ERR("closedir");
    return count - 1;  // one descriptor for open directory
}


void create_pipes_and_fork(knight_t* franci_k, int franciNo, knight_t* saraceni_k, int saraceniNo){
    int* pipeFranci;
    int* pipeSaraceni;

    if((pipeFranci = (int*)malloc(sizeof(int) * franciNo * 2)) == NULL){
        ERR("malloc");
    }
    if((pipeSaraceni = (int*)malloc(sizeof(int) * saraceniNo * 2)) == NULL){
        ERR("malloc");
    }

    for(int i = 0;i < franciNo;i++){
        if(pipe(&pipeFranci[2*i]) == -1){
            ERR("pipe");
        }
    }
    for(int i = 0; i<saraceniNo;i++){
        if(pipe(&pipeSaraceni[2*i]) == -1){
            ERR("pipe");
        }
    }

    for(int i = 0; i<franciNo;i++){
        int res = fork();


        if(res == 0){
            for(int j = 0;j<franciNo;j++){
                if(i!=j){
                    close(pipeFranci[2*j]);
                }
                close(pipeFranci[2*j+1]);
            }

            for(int j = 0;j<saraceniNo;j++){
                close(pipeSaraceni[2*j]);
            }
            printf("I am Frankish knight %s,. I will serve my king with my %d HP and %d attack\n", franci_k[i].name,
                   franci_k[i].HP, franci_k[i].attack);

            printf("Opened descriptors: %d\n", count_descriptors());
            
            close(pipeFranci[2*i]);
            for(int j = 0;j<saraceniNo;j++){
                close(pipeSaraceni[2*j+1]);
            }

            free(franci_k);
            free(saraceni_k);
            free(pipeFranci);
            free(pipeSaraceni);
            exit(EXIT_SUCCESS);
        }
        if (res == -1)
            ERR("fork");
    
    }

    for(int i = 0; i <saraceniNo;i++){
        int res = fork();
        if(res == 0){
            for(int j = 0;j<saraceniNo;j++){
                if(i!=j){
                    close(pipeSaraceni[2*j]);
                }
                close(pipeSaraceni[2*j+1]);
            }
            for(int j = 0;j<franciNo;j++){
                close(pipeFranci[2*j]);
            }

            printf("I am Spanish knight %s. I will serve my king with my %d HP and %d attack\n", saraceni_k[i].name,
                   saraceni_k[i].HP, saraceni_k[i].attack);

            printf("Opened descriptors: %d\n", count_descriptors());

            close(pipeSaraceni[2 * i]);
            for (int j = 0; j < franciNo; j++)
            {
                close(pipeFranci[2 * j + 1]);
            }

            free(franci_k);
            free(saraceni_k);
            free(pipeFranci);
            free(pipeSaraceni);
            exit(EXIT_SUCCESS);
        }
        if(res == -1){
            ERR("fork");
        }
    }

    for(int i = 0;i<franciNo*2;i++){
        close(pipeFranci[i]);
    }
    for(int i = 0;i<saraceniNo*2;i++){
        close(pipeSaraceni[i]);
    }

    free(pipeFranci);
    free(pipeSaraceni);
}

int main(int argc, char* argv[])
{
    FILE* franci_f;
    FILE* saraceni_f;

    if((franci_f = fopen("franci.txt", "r")) == NULL){
        ERR("fopen");
    }

    if((saraceni_f = fopen("saraceni.txt", "r")) == NULL){
        ERR("fopen");
    }

    int franciNo;
    fscanf(franci_f, "%d", &franciNo);

    int saraceniNo;
    fscanf(saraceni_f, "%d", &saraceniNo);

    knight_t* franci;
    if((franci = (knight_t*)malloc(sizeof(knight_t) * franciNo)) == NULL){
        ERR("malloc");
    }

    knight_t* saraceni;
    if((saraceni = (knight_t*)malloc(sizeof(knight_t) * saraceniNo)) == NULL){
        ERR("malloc");
    }

    for (int i = 0; i < franciNo; i++)
    {
        fscanf(franci_f, "%s %d %d", franci[i].name, &franci[i].HP, &franci[i].attack);
    }

    for (int i = 0; i < saraceniNo; i++)
    {
        fscanf(saraceni_f, "%s %d %d", saraceni[i].name, &saraceni[i].HP, &saraceni[i].attack);
    }

    fclose(saraceni_f);
    fclose(franci_f);

    create_pipes_and_fork(franci, franciNo, saraceni, saraceniNo);

    while (wait(NULL) > 0);

    free(franci);
    free(saraceni);

    return EXIT_SUCCESS;
}