#ifndef _FM_SERVER_H_
#define _FM_SERVER_H_

#include <sys/select.h>

typedef struct {
    char *addr;
    char *port;
    
    int listen_fd;
    fd_set fds;
    int fd_max;

    int should_quit;
} fm_server_t;

typedef void (*server_handle)(void *ptr, const char *input, char *output);

void fm_server_run(fm_server_t *server, server_handle handle, void *handle_data);

#endif
