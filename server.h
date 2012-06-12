#ifndef _FM_SERVER_H_
#define _FM_SERVER_H_

#include <sys/select.h>

typedef struct {
    char addr[16];
    char port[8];
    
    int listen_fd;
    fd_set fds;
    int fd_max;

    int should_quit;
} fm_server_t;

typedef void (*server_handle)(void *ptr, char *input, char *output);

int fm_server_setup(fm_server_t *server);
void fm_server_run(fm_server_t *server, server_handle handle, void *handle_data);

#endif
