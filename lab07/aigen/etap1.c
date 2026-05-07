#include "common.h"

void usage(char* name) {
    fprintf(stderr, "USAGE: %s local_socket_name\n", name);
    exit(EXIT_FAILURE);
}

int main(int argc, char** argv) {
    if(argc != 2) usage(argv[0]);

    int sfd = bind_local_socket(argv[1], 10);
    if(sfd < 0) ERR("bind_local_socket");

    int client_fd = add_new_client(sfd);
    if(client_fd >= 0) {
        char buf[16];
        memset(buf, 0, sizeof(buf));
        
        ssize_t size = read(client_fd, buf, sizeof(buf) - 1);
        if(size > 0) {
            if(buf[0] == 'M') {
                printf("Zglosil sie Monteki: %s", buf);
            } 
            else if(buf[0] == 'C') {
                printf("Zglosila sie Kapulet: %s", buf);
            } 
            else {
                printf("Nieznany ród!\n");
            }
        }
        close(client_fd);
    }

    close(sfd);
    unlink(argv[1]);
    return EXIT_SUCCESS;
}