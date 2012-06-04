#include "server.h"
#include "util.h"

#include <netinet/in.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

int fm_server_setup(fm_server_t *server)
{
    struct addrinfo hints, *results, *p;

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

void fm_server_run(fm_server_t *server, server_handle handle, void *handle_data)
{
    int i;
    int client;
    char input_buf[64];
    char output_buf[1024];
    int buf_size;
    fd_set read_fds;

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
                    close(i);
                    FD_CLR(i, &server->fds);
                }
                else if (buf_size > 0) {
                    trim(input_buf);
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
