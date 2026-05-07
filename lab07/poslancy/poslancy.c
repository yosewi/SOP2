#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>
#include "common.h"

#define MAX_EVENTS 10
#define MAX_CLIENTS 4

typedef struct {
    char owner;
    int num;
} city;

volatile sig_atomic_t do_work = 1;
void sigint_handler(int sig) { do_work = 0; }

void usage(char* name)
{
    fprintf(stderr, "USAGE: %s port\n", name);
    exit(EXIT_FAILURE);
}

// Funkcja pomocnicza do bezpiecznego usuwania klientów
void remove_client(int fd, int clients[], int *current_clients) {
    printf("Rozlaczam klienta (FD: %d)\n", fd);
    for(int i = 0; i < MAX_CLIENTS; i++) {
        if(clients[i] == fd) {
            clients[i] = -1; // Zwalniamy miejsce w tablicy
            break;
        }
    }
    (*current_clients)--;
    close(fd);
}

int main(int argc, char** argv){

    if(argc != 2){
        usage(argv[0]);
    }

    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);
    sethandler(sigint_handler, SIGINT);
    
    // Ignorujemy SIGPIPE - zamiast ubijać serwer, write zwróci -1 i ustawi errno = EPIPE
    sethandler(SIG_IGN, SIGPIPE); 

    city cities[20];
    for(int i = 0; i < 20; i++){
        cities[i].owner = 'g';
        cities[i].num = i+1;
    }

    int clients[MAX_CLIENTS];
    for(int i = 0; i < MAX_CLIENTS; i++) {
        clients[i] = -1; 
    }
    
    int current_clients = 0;
    uint16_t port = atoi(argv[1]);
    int sfd = bind_tcp_socket(port, 10);
    if(sfd < 0){
        ERR("bind_tcp_socket");
    }    

    int epoll_fd = epoll_create1(0);
    if(epoll_fd < 0) ERR("epoll_create1");

    struct epoll_event ev, events[MAX_EVENTS];
    ev.events = EPOLLIN;
    ev.data.fd = sfd;
    if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sfd, &ev)) ERR("epoll_ctl");

    while(do_work){ 
        int nfds = epoll_pwait(epoll_fd, events, MAX_EVENTS, -1, &oldmask);
        if(nfds < 0){
            if (errno != EINTR) ERR("epoll_wait");
            continue;
        }

        for(int i = 0; i < nfds; i++){
            int fd = events[i].data.fd;

            if(fd == sfd){
                if(current_clients < MAX_CLIENTS){
                    int client_fd = add_new_client(sfd);
                    if (client_fd >= 0) {
                        for(int j = 0; j < MAX_CLIENTS; j++) {
                            if(clients[j] == -1) {
                                clients[j] = client_fd;
                                break;
                            }
                        }
                        current_clients++;
                        
                        ev.events = EPOLLIN;
                        ev.data.fd = client_fd;
                        if(epoll_ctl(epoll_fd, EPOLL_CTL_ADD, client_fd, &ev)) ERR("epoll_ctl");

                        int flags = fcntl(client_fd, F_GETFL, 0) | O_NONBLOCK;
                        fcntl(client_fd, F_SETFL, flags);
                    }
                }
                else{
                    printf("Odrzucam nadmiarowego klienta\n");
                    int rejected_fd = add_new_client(sfd);
                    if (rejected_fd >= 0) close(rejected_fd); 
                }
            }
            else{
                char buf[5];
                memset(buf, 0, sizeof(buf));
                
                ssize_t size = read(fd, buf, 4); 
                
                if(size <= 0){
                    // Rozłączenie (0) lub błąd (< 0)
                    remove_client(fd, clients, &current_clients);
                }
                else if(size == 4){
                    // --- WALIDACJA FORMATU ---
                    int is_valid = 1;
                    if(buf[0] != 'p' && buf[0] != 'g') is_valid = 0;
                    if(buf[1] < '0' || buf[1] > '9') is_valid = 0;
                    if(buf[2] < '0' || buf[2] > '9') is_valid = 0;
                    if(buf[3] != '\n') is_valid = 0;

                    if(is_valid){
                        int city_index = (buf[1] - '0') * 10 + (buf[2] - '0');
                        
                        // Walidacja zakresu (1 do 20)
                        if(city_index >= 1 && city_index <= 20) {
                            
                            // Aktualizacja właściciela
                            char new_owner = buf[0];
                            if(cities[city_index - 1].owner != new_owner){
                                cities[city_index - 1].owner = new_owner;
                                
                                // Broadcast do reszty
                                for(int k = 0; k < MAX_CLIENTS; k++){
                                    int target_fd = clients[k];
                                    if(target_fd != -1 && target_fd != fd){ 
                                        if(write(target_fd, buf, 4) < 0){
                                            // OBSŁUGA EPIPE W TRAKCIE PISANIA
                                            if(errno == EPIPE){
                                                remove_client(target_fd, clients, &current_clients);
                                            } else {
                                                ERR("write");
                                            }
                                        }
                                    }
                                }
                            }
                        } else {
                            // Zły zakres miasta -> odłączamy
                            printf("Nieznane miasto: %d. ", city_index);
                            remove_client(fd, clients, &current_clients);
                        }
                    } else {
                        // Zły format wiadomości -> odłączamy
                        printf("Bledny format wiadomosci. ");
                        remove_client(fd, clients, &current_clients);
                    }
                }
                else {
                    // Wczytano 1, 2 lub 3 bajty - to nie jest poprawny format
                    printf("Niepelna wiadomosc. ");
                    remove_client(fd, clients, &current_clients);
                }
            }
        }
    }

    printf("\n--- STAN KONTROLI MIAST ---\n");
    for(int i = 0; i < 20; i++){
        if (cities[i].owner == 'p') {
            printf("Miasto %d: Persowie\n", cities[i].num);
        } else {
            printf("Miasto %d: Grecy\n", cities[i].num);
        }
    }

    // Zamykamy wszystkich pozostałych klientów
    for(int i = 0; i < MAX_CLIENTS; i++) {
        if(clients[i] != -1) close(clients[i]);
    }
    
    close(epoll_fd);
    close(sfd);

    return EXIT_SUCCESS;
}