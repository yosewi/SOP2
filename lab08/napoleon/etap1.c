#include "common.h"

#define MAX_EVENTS 10

volatile sig_atomic_t do_work = 1;
void sigint_handler(int sig) { do_work = 0; }

void usage(char *name) {
    fprintf(stderr, "USAGE: %s port\n", name);
    exit(EXIT_FAILURE);
}

int bind_udp_socket(uint16_t port) {
    int socketfd = socket(PF_INET, SOCK_DGRAM, 0);
    if (socketfd < 0) ERR("socket");
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    int t = 1;
    if (setsockopt(socketfd, SOL_SOCKET, SO_REUSEADDR, &t, sizeof(t))) ERR("setsockopt");
    if (bind(socketfd, (struct sockaddr *)&addr, sizeof(addr)) < 0) ERR("bind");
    return socketfd;
}

int main(int argc, char **argv) {
    if (argc != 2) usage(argv[0]);

    sigset_t mask, oldmask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigprocmask(SIG_BLOCK, &mask, &oldmask);
    sethandler(sigint_handler, SIGINT);

    uint16_t port = atoi(argv[1]);
    int udp_sfd = bind_udp_socket(port);

    // Gniazdo UDP ustawiamy na tryb nieblokujący
    int flags = fcntl(udp_sfd, F_GETFL, 0) | O_NONBLOCK;
    fcntl(udp_sfd, F_SETFL, flags);

    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) ERR("epoll_create1");

    struct epoll_event ev, events[MAX_EVENTS];
    ev.events = EPOLLIN;
    ev.data.fd = udp_sfd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, udp_sfd, &ev)) ERR("epoll_ctl");

    char buf[512];
    struct sockaddr_in client_addr;
    socklen_t client_len;

    while (do_work) {
        int nfds = epoll_pwait(epoll_fd, events, MAX_EVENTS, -1, &oldmask);
        if (nfds < 0) {
            if (errno != EINTR) ERR("epoll_pwait");
            continue;
        }

        for (int i = 0; i < nfds; i++) {
            if (events[i].data.fd == udp_sfd) {
                client_len = sizeof(client_addr);
                memset(buf, 0, sizeof(buf));
                
                ssize_t size = recvfrom(udp_sfd, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&client_addr, &client_len);
                if (size < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) continue;
                    ERR("recvfrom");
                }

                int x, y, p;
                char div_name[129];
                memset(div_name, 0, sizeof(div_name));

                // Parsowanie wiadomości - [^\n] czyta spacje w nazwie, aż do końca linii
                if (sscanf(buf, "%d %d %d %128[^\n]", &x, &y, &p, div_name) == 4) {
                    if (x >= 0 && x <= 99 && y >= 0 && y <= 99 && (p == 0 || p == 1)) {
                        const char* type = (p == 1) ? "Nasz" : "Wrogi";
                        printf("%s oddział %s był widziany na pozycji %d:%d\n", type, div_name, x, y);
                    } else {
                        fprintf(stderr, "Błąd: Nieprawidłowe wartości w meldunku (X/Y/P poza zakresem).\n");
                    }
                } else {
                    fprintf(stderr, "Błąd: Źle sformatowana wiadomość z meldunkiem.\n");
                }
            }
        }
    }

    close(epoll_fd);
    close(udp_sfd);
    return EXIT_SUCCESS;
}