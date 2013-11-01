#include "server.h"
#include "util.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

int fm_server_setup(fm_server_t *server)
{
    struct addrinfo hints, *results, *p;

    printf("Server listen at %s:%s\n", server->addr, server->port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    if (getaddrinfo(server->addr, server->port, &hints, &results) != 0) {
        return -1;
    }

    for (p = results; p != NULL; p = p->ai_next) {
        server->listen_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (server->listen_fd < 0) {
            continue;
        }
        if (bind(server->listen_fd, p->ai_addr, p->ai_addrlen) == 0) {
            break;
        }
        close(server->listen_fd);
    }
    if (p == NULL) {
        return -1;
    }

    freeaddrinfo(results);

    if (listen(server->listen_fd, 5) < 0) {
        return -1;
    }

    FD_ZERO(&server->fds);
    FD_SET(server->listen_fd, &server->fds);
    server->fd_max = server->listen_fd;

    server->should_quit = 0;

    return 0;
}

static int send_all(int fd, const char *buf, int buf_size)
{
    int done = 0;
    int ret = 0;
    while (done < buf_size) {
        ret = send(fd, buf + done, buf_size - done, 0);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            else {
                break;
            }
        }
        else {
            done += ret;
        }
    }
    if (ret < 0) {
        return ret;
    }
    else {
        return done;
    }
}

static void getPeerInfo(int socket, char *ipstr, int iplen, int *port) {
    socklen_t len;
    struct sockaddr_storage addr;

    len = sizeof(addr);
    getpeername(socket, (struct sockaddr*) &addr, &len);

    if (addr.ss_family == AF_INET) {
        struct sockaddr_in *s = (struct sockaddr_in *) &addr;
        *port = ntohs(s->sin_port);
        inet_ntop(AF_INET, &s->sin_addr, ipstr, iplen);
    }
    else {
        struct sockaddr_in6 *s = (struct sockaddr_in6 *) &addr;
        *port = ntohs(s->sin6_port);
        inet_ntop(AF_INET6, &s->sin6_addr, ipstr, iplen);
    }
}

void fm_server_run(fm_server_t *server, server_handle handle, void *handle_data)
{
    int i;
    int client = 0;
    char input_buf[64];
    char output_buf[1024];
    int buf_size;
    fd_set read_fds;
    char ipstr[INET6_ADDRSTRLEN];
    int port;

    while (!server->should_quit) {
        read_fds = server->fds;
        if (select(server->fd_max + 1, &read_fds, NULL, NULL, NULL) < 0) {
            if (errno == EINTR) {
                continue;
            }
            else {
                perror("select");
                return;
            }
        }

        if (FD_ISSET(server->listen_fd, &read_fds)) {
            client = accept(server->listen_fd, NULL, NULL);
            if (client < 0) {
                perror("accept");
            }
            else {
                getPeerInfo(client, ipstr, sizeof(ipstr), &port);
                /*printf("Server has a new client from %s:%d\n", ipstr, port);*/
                FD_SET(client, &server->fds);
                if (client > server->fd_max) {
                    server->fd_max = client;
                }
            }
            FD_CLR(server->listen_fd, &read_fds);
        }

        for (i = 0; i <= server->fd_max; i++) {
            if (FD_ISSET(i, &read_fds)) {
                memset(input_buf, 0, sizeof(input_buf));
                buf_size = recv(i, input_buf, sizeof(input_buf), 0);
                if (buf_size == 0) {
                    getPeerInfo(client, ipstr, sizeof(ipstr), &port);
                    /*printf("Server says goodbye to client from %s:%d\n", ipstr, port);*/
                    close(i);
                    FD_CLR(i, &server->fds);
                }
                else if (buf_size > 0) {
                    trim(input_buf);
                    getPeerInfo(client, ipstr, sizeof(ipstr), &port);
                    /*printf("Server receives from client %s:%d => %s\n", ipstr, port, input_buf);*/
                    memset(output_buf, 0, sizeof(output_buf));
                    handle(handle_data, input_buf, output_buf);
                    buf_size = strlen(output_buf);
                    if (send_all(i, output_buf, buf_size) < 0) {
                        perror("client write");
                        close(i);
                        FD_CLR(i, &server->fds);
                    }
                }
                else if (errno != EINTR) {
                    perror("client read");
                    close(i);
                    FD_CLR(i, &server->fds);
                }
            }
        }
    }
    for (i = 0; i <= server->fd_max; i++) {
        if (FD_ISSET(i, &server->fds)) {
            close(i);
        }
    }
}
