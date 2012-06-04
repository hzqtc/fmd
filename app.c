#include "server.h"
#include "player.h"
#include "playlist.h"
#include "config.h"
#include "util.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <pwd.h>
#include <signal.h>

#define FILE_MODE S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH
#define DIR_MODE S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH

typedef struct {
    fm_server_t server;
    fm_playlist_t playlist;
    fm_player_t player;
} fm_app_t;

fm_app_t app = {
    .server = {
        .addr = "localhost",
        .port = "10098"
    }
};

void get_fm_info(fm_app_t *app, char *output)
{
    fm_song_t *current;

    switch (app->player.status) {
        case FM_PLAYER_PLAY:
        case FM_PLAYER_PAUSE:
            current = fm_playlist_current(&app->playlist);
            sprintf(output, "{\"status\":\"%s\",\"channel\":%d,\"user\":\"%s\","
                    "\"title\":\"%s\",\"artist\":\"%s\", \"album\":\"%s\",\"year\":%d,"
                    "\"cover\":\"%s\",\"url\":\"%s\",\"sid\":%d,"
                    "\"like\":%d,\"pos\":%d,\"len\":%d}",
                    app->player.status == FM_PLAYER_PLAY? "play": "pause",
                    app->playlist.channel, app->playlist.uname,
                    current->title, current->artist, current->album,
                    current->pubdate, current->cover, current->url,
                    current->sid, current->like, fm_player_pos(&app->player),
                    fm_player_length(&app->player));
            break;
        case FM_PLAYER_STOP:
            sprintf(output, "{\"status\":\"stop\",\"channel\":%d,\"user\":\"%s\"}",
                    app->playlist.channel, app->playlist.uname);
            break;
        default:
            break;
    }
}

void player_end_handler(int sig)
{
    fm_player_set_url(&app.player, fm_playlist_next(&app.playlist)->audio);
    fm_player_play(&app.player);
}

void app_client_handler(void *ptr, const char *input, char *output)
{
    fm_app_t *app = (fm_app_t*) ptr;
    if (strcmp(input, "play") == 0) {
        if (app->player.status == FM_PLAYER_STOP) {
            fm_player_set_url(&app->player, fm_playlist_current(&app->playlist)->audio);
        }
        fm_player_play(&app->player);
        get_fm_info(app, output);
    }
    else if(strcmp(input, "stop") == 0) {
        fm_player_stop(&app->player);
        get_fm_info(app, output);
    }
    else if(strcmp(input, "pause") == 0) {
        fm_player_pause(&app->player);
        get_fm_info(app, output);
    }
    else if(strcmp(input, "toggle") == 0) {
        fm_player_toggle(&app->player);
        get_fm_info(app, output);
    }
    else if(strcmp(input, "skip") == 0) {
        fm_player_set_url(&app->player, fm_playlist_skip(&app->playlist)->audio);
        fm_player_play(&app->player);
        get_fm_info(app, output);
    }
    else if(strcmp(input, "ban") == 0) {
        fm_player_set_url(&app->player, fm_playlist_ban(&app->playlist)->audio);
        fm_player_play(&app->player);
        get_fm_info(app, output);
    }
    else if(strcmp(input, "rate") == 0) {
        fm_playlist_rate(&app->playlist);
        get_fm_info(app, output);
    }
    else if(strcmp(input, "unrate") == 0) {
        fm_playlist_unrate(&app->playlist);
        get_fm_info(app, output);
    }
    else if(strcmp(input, "info") == 0) {
        get_fm_info(app, output);
    }
    else if(strcmp(input, "end") == 0) {
        app->server.should_quit = 1;
    }
    else {
        sprintf(output, "{\"status\":\"error\",\"message\":\"wrong command: %s\"}", input);
    }
}

void daemonize(const char *lock_file, const char *log_file, const char *err_file)
{
    pid_t pid;
    int fd0, fd1, fd2;
    int lock_fd;

    if ((pid = fork()) < 0) {
        perror("fork");
    }
    else if(pid > 0) {
        exit(0);
    }

    if ((pid = fork()) < 0) {
        perror("fork");
    }
    else if(pid > 0) {
        exit(0);
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    fd0 = open("/dev/null", O_RDONLY);
    fd1 = open(log_file, O_WRONLY | O_APPEND | O_CREAT, FILE_MODE);
    fd2 = open(err_file, O_WRONLY | O_TRUNC | O_CREAT, FILE_MODE);

    if (fd0 != STDIN_FILENO || fd1 != STDOUT_FILENO || fd2 != STDERR_FILENO) {
        fprintf(stderr, "wrong fds\n");
        exit(1);
    }

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);

    lock_fd = open(lock_file, O_RDWR | O_CREAT, FILE_MODE);
    if (lockf(lock_fd, F_TLOCK, 0) < 0) {
        fprintf(stderr, "failed to lock file\n");
        exit(1);
    }
}

int main(int argc, char *argv[])
{
    struct passwd *pwd = getpwuid(getuid());
    char fmd_dir[sysconf(LOGIN_NAME_MAX) + 20];
    char config_file[sysconf(LOGIN_NAME_MAX) + 20];
    char log_file[sysconf(LOGIN_NAME_MAX) + 20];
    char err_file[sysconf(LOGIN_NAME_MAX) + 20];
    char lock_file[sysconf(LOGIN_NAME_MAX) + 20];

    strcpy(fmd_dir, pwd->pw_dir);
    strcat(fmd_dir, "/.fmd");
    mkdir(fmd_dir, DIR_MODE);

    strcpy(config_file, pwd->pw_dir);
    strcat(config_file, "/.fmd/fmd.conf");

    strcpy(log_file, pwd->pw_dir);
    strcat(log_file, "/.fmd/fmd.log");

    strcpy(err_file, pwd->pw_dir);
    strcat(err_file, "/.fmd/fmd.err");

    strcpy(lock_file, pwd->pw_dir);
    strcat(lock_file, "/.fmd/fmd.lock");

    daemonize(lock_file, log_file, err_file);

    int c;
    while ((c = getopt(argc, argv, "a:p:")) != -1) {
        switch (c) {
            case 'a':
                app.server.addr = optarg;
                break;
            case 'p':
                app.server.port = optarg;
                break;
            default:
                break;
        }
    }

    fm_player_init();
    fm_player_open(&app.player);
    fm_playlist_init(&app.playlist);

    int player_end_sig = SIGUSR1;
    struct sigaction player_end_act;
    player_end_act.sa_handler = player_end_handler;
    sigemptyset(&player_end_act.sa_mask);
    player_end_act.sa_flags = 0;
    sigaction(player_end_sig, &player_end_act, NULL);
    fm_player_set_ack(&app.player, pthread_self(), player_end_sig);

    fm_config_t configs[] = {
        { .type = FM_CONFIG_INT, .section = "DoubanFM", .key = "channel", .val.i = &app.playlist.channel },
        { .type = FM_CONFIG_INT, .section = "DoubanFM", .key = "uid", .val.i = &app.playlist.uid },
        { .type = FM_CONFIG_STR, .section = "DoubanFM", .key = "uname", .val.s = app.playlist.uname },
        { .type = FM_CONFIG_STR, .section = "DoubanFM", .key = "token", .val.s = app.playlist.token },
        { .type = FM_CONFIG_INT, .section = "DoubanFM", .key = "expire", .val.i = &app.playlist.expire }
    };
    fm_config_parse(config_file, configs, sizeof(configs) / sizeof(fm_config_t));
    
    fm_server_run(&app.server, app_client_handler, &app);

    fm_playlist_cleanup(&app.playlist);
    fm_player_close(&app.player);
    fm_player_exit();

    return 0;
}
