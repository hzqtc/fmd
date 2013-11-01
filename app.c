#include "server.h"
#include "playlist.h"
#include "player.h"
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
#include <wordexp.h>

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
            char btitle[128], bart[128], balb[128], bcover[128], burl[128]; 
            sprintf(output, "{\"status\":\"%s\",\"kbps\":\"%s\",\"channel\":\"%s\",\"user\":\"%s\","
                    "\"title\":\"%s\",\"artist\":\"%s\", \"album\":\"%s\",\"year\":%d,"
                    "\"cover\":\"%s\",\"url\":\"%s\",\"sid\":%d,"
                    "\"like\":%d,\"pos\":%d,\"len\":%d}",
                    app->player.status == FM_PLAYER_PLAY? "play": "pause",
                    current->kbps,app->playlist.config.channel, app->playlist.config.uname,
                    escapejson(btitle, current->title), 
                    escapejson(bart, current->artist), 
                    escapejson(balb, current->album),
                    current->pubdate, 
                    escapejson(bcover, current->cover), 
                    escapejson(burl, current->url),
                    current->sid, current->like, fm_player_pos(&app->player),
                    fm_player_length(&app->player));
            break;
        case FM_PLAYER_STOP:
            sprintf(output, "{\"status\":\"stop\",\"kbps\":%s,\"channel\":\"%s\",\"user\":\"%s\"}",
                    app->playlist.config.kbps,app->playlist.config.channel, app->playlist.config.uname);
            break;
        default:
            break;
    }
}

void app_client_handler(void *ptr, char *input, char *output)
{
    fm_app_t *app = (fm_app_t*) ptr;
    char *cmd = input;
    char *arg = split(input, ' ');

    if (strcmp(cmd, "play") == 0) {
        if (app->player.status != FM_PLAYER_STOP || fm_player_set_song(&app->player, fm_playlist_current(&app->playlist)) == 0) {
            fm_player_play(&app->player);
        }
        get_fm_info(app, output);
    }
    else if(strcmp(cmd, "stop") == 0) {
        fm_player_stop(&app->player);
        get_fm_info(app, output);
    }
    else if(strcmp(cmd, "pause") == 0) {
        fm_player_pause(&app->player);
        get_fm_info(app, output);
    }
    else if(strcmp(cmd, "toggle") == 0) {
        switch (app->player.status) {
            case FM_PLAYER_PLAY:
                fm_player_pause(&app->player);
                break;
            case FM_PLAYER_PAUSE:
                fm_player_play(&app->player);
                break;
            case FM_PLAYER_STOP:
                if (fm_player_set_song(&app->player, fm_playlist_current(&app->playlist)) == 0)
                    fm_player_play(&app->player);
                break;
        }
        get_fm_info(app, output);
    }
    else if(strcmp(cmd, "skip") == 0 || strcmp(cmd, "next") == 0) {
        if (fm_player_set_song(&app->player, fm_playlist_skip(&app->playlist, 0)) == 0)
            fm_player_play(&app->player);
        get_fm_info(app, output);
    }
    else if(strcmp(cmd, "ban") == 0) {
        if (fm_player_set_song(&app->player, fm_playlist_ban(&app->playlist)) == 0)
            fm_player_play(&app->player);
        get_fm_info(app, output);
    }
    else if(strcmp(cmd, "rate") == 0) {
        fm_playlist_rate(&app->playlist);
        get_fm_info(app, output);
    }
    else if(strcmp(cmd, "unrate") == 0) {
        fm_playlist_unrate(&app->playlist);
        get_fm_info(app, output);
    }
    else if(strcmp(cmd, "info") == 0) {
        get_fm_info(app, output);
    }
    else if(strcmp(cmd, "end") == 0) {
        app->server.should_quit = 1;
    }
    else if(strcmp(cmd, "setch") == 0) {
        if (arg == NULL) {
            sprintf(output, "{\"status\":\"error\",\"message\":\"Missing argument: %s\"}", input);
        }
        else {
            if (strcmp(arg, app->playlist.config.channel) != 0) {
                strcpy(app->playlist.config.channel, arg);
                fm_playlist_update_mode(&app->playlist);
                if (fm_player_set_song(&app->player, fm_playlist_skip(&app->playlist, 1)) == 0)
                    fm_player_play(&app->player);
            }
            get_fm_info(app, output);
        }
    }
    else if(strcmp(cmd, "kbps") == 0) {
        if (app->playlist.mode == plDouban) {
            if (arg == NULL) {
                sprintf(output, "{\"status\":\"error\",\"message\":\"Missing argument: %s\"}", input);
            }
            else if (strcmp(arg, "64") != 0 || strcmp(arg, "128") != 0 || strcmp(arg, "192") != 0) {
                sprintf(output, "{\"status\":\"error\",\"message\":\"Wrong argument: %s\"}", arg);
            }
            else {
                if (strcmp(arg, app->playlist.config.kbps) != 0) {
                    strcpy(app->playlist.config.kbps, arg);
                    if (fm_player_set_song(&app->player, fm_playlist_skip(&app->playlist, 0)) == 0)
                        fm_player_play(&app->player);
                }
                get_fm_info(app, output);
            }
        } else {
            sprintf(output, "{\"status\":\"error\",\"message\":\"Current channel does not support bitrate switch: %s\"}", input);
        }
    }
    // allowing an additional argument to be specified as the browser
    else if(strcmp(cmd, "webpage") == 0) {
        switch (app->player.status) {
            case FM_PLAYER_PLAY:
            case FM_PLAYER_PAUSE: {
                char sh[1024];
                // the url can become rather long after escaping
                char url[896];
                fm_song_t *current = fm_playlist_current(&app->playlist);
                if (escapesh(url, current->url)[0] == '\0') {
                    // we need to make a custom url to open
                    sprintf(url, "%s %s", current->artist, current->album);
                    // first obtain a curl instance to escape the query
                    downloader_t *d = stack_get_idle_downloader(app->playlist.stack, dAny);
                    char *st = curl_easy_escape(d->curl, url, 0);
                    sprintf(url, "http://music.douban.com/subject_search?search_text=%s&cat=1003", st);
                    curl_free(st);
                }
                printf("url is %s\n", url);
                sprintf(sh, "%s $'%s' &", arg ? arg : "$BROWSER", url);
                printf("cmd is %s\n", sh);
                system(sh);
                get_fm_info(app, output);
                break;
            }
            default:
                sprintf(output, "{\"status\":\"error\",\"message\":\"Page information not available\"}");
        }
    } else {
        sprintf(output, "{\"status\":\"error\",\"message\":\"Wrong command: %s\"}", input);
    }
}

void daemonize(const char *log_file, const char *err_file)
{
    pid_t pid;
    int fd0, fd1, fd2;

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

    chdir("/");

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    fd0 = open("/dev/null", O_RDONLY);
    fd1 = open(log_file, O_WRONLY | O_TRUNC | O_CREAT, FILE_MODE);
    fd2 = open(err_file, O_WRONLY | O_TRUNC | O_CREAT, FILE_MODE);

    if (fd0 != STDIN_FILENO || fd1 != STDOUT_FILENO || fd2 != STDERR_FILENO) {
        fprintf(stderr, "wrong fds\n");
        exit(1);
    }

    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IOLBF, 0);
}

void player_end_handler(int sig)
{
    if (fm_player_set_song(&app.player, fm_playlist_next(&app.playlist)) == 0)
        fm_player_play(&app.player);
}

void install_player_end_handler(fm_player_t *player)
{
    int player_end_sig = SIGUSR1;
    struct sigaction player_end_act;
    player_end_act.sa_handler = player_end_handler;
    sigemptyset(&player_end_act.sa_mask);
    player_end_act.sa_flags = 0;
    sigaction(player_end_sig, &player_end_act, NULL);
    fm_player_set_ack(player, pthread_self(), player_end_sig);
}

void stop_player()
{
    fm_player_stop(&app.player);
}

int start_fmd(fm_playlist_config_t *playlist_conf, fm_player_config_t *player_conf)
{
    fm_player_init();
    if (fm_player_open(&app.player, player_conf) < 0) {
        perror("Open audio output");
        return 1;
    }
    install_player_end_handler(&app.player);

    fm_playlist_init(&app.playlist, playlist_conf, stop_player);

    if (fm_server_setup(&app.server) < 0) {
        perror("Server");
        return 1;
    }
    fm_server_run(&app.server, app_client_handler, &app);

    fm_playlist_cleanup(&app.playlist);
    fm_player_close(&app.player);
    fm_player_exit();

    return 0;
}

int main() {
    struct passwd *pwd = getpwuid(getuid());
    const int MAX_DIR_LEN = 128;
    char fmd_dir[MAX_DIR_LEN];
    char config_file[MAX_DIR_LEN];
    char log_file[MAX_DIR_LEN];
    char err_file[MAX_DIR_LEN];

    strcpy(fmd_dir, pwd->pw_dir);
    strcat(fmd_dir, "/.fmd");
    mkdir(fmd_dir, DIR_MODE);

    strcpy(config_file, pwd->pw_dir);
    strcat(config_file, "/.fmd/fmd.conf");

    strcpy(log_file, pwd->pw_dir);
    strcat(log_file, "/.fmd/fmd.log");

    strcpy(err_file, pwd->pw_dir);
    strcat(err_file, "/.fmd/fmd.err");

    daemonize(log_file, err_file);

    fm_playlist_config_t playlist_conf = {
        .channel = "0",
        .douban_uid = 0,
        .uname = "",
        .douban_token = "",
        .expire = 0,
        .kbps = "",
        .music_dir = "~/Music",
        .jing_uid = 0,
        .jing_atoken = "",
        .jing_rtoken = ""
    };
    fm_player_config_t player_conf = {
        .rate = 44100,
        .channels = 2,
        .driver = "alsa",
        .dev = "default",
    };
    fm_config_t configs[] = {
        {
            .type = FM_CONFIG_STR,
            .section = "DoubanFM",
            .key = "channel",
            .val.s = playlist_conf.channel
        },
        {
            .type = FM_CONFIG_INT,
            .section = "DoubanFM",
            .key = "uid",
            .val.i = &playlist_conf.douban_uid
        },
        {
            .type = FM_CONFIG_STR,
            .section = "DoubanFM",
            .key = "uname",
            .val.s = playlist_conf.uname
        },
        {
            .type = FM_CONFIG_STR,
            .section = "DoubanFM",
            .key = "token",
            .val.s = playlist_conf.douban_token
        },
        {
            .type = FM_CONFIG_INT,
            .section = "DoubanFM",
            .key = "expire",
            .val.i = &playlist_conf.expire
        },
        {
            .type = FM_CONFIG_STR,
            .section = "DoubanFM",
            .key = "kbps",
            .val.s = playlist_conf.kbps
        },
        {
            .type = FM_CONFIG_STR,
            .section = "Local",
            .key = "music_dir",
            .val.s = playlist_conf.music_dir
        },
        {
            .type = FM_CONFIG_STR,
            .section = "Output",
            .key = "driver",
            .val.s = player_conf.driver
        },
        {
            .type = FM_CONFIG_STR,
            .section = "Output",
            .key = "device",
            .val.s = player_conf.dev
        },
        {
            .type = FM_CONFIG_INT,
            .section = "Output",
            .key = "rate",
            .val.i = &player_conf.rate
        },
        {
            .type = FM_CONFIG_STR,
            .section = "Server",
            .key = "address",
            .val.s = app.server.addr
        },
        {
            .type = FM_CONFIG_STR,
            .section = "Server",
            .key = "port",
            .val.s = app.server.port
        },
        // for jing
        {
            .type = FM_CONFIG_STR,
            .section = "JingFM",
            .key = "atoken",
            .val.s = playlist_conf.jing_atoken
        },
        {
            .type = FM_CONFIG_STR,
            .section = "JingFM",
            .key = "rtoken",
            .val.s = playlist_conf.jing_rtoken
        },
        {
            .type = FM_CONFIG_INT,
            .section = "JingFM",
            .key = "uid",
            .val.i = &playlist_conf.jing_uid
        }
    };
    fm_config_parse(config_file, configs, sizeof(configs) / sizeof(fm_config_t));

    // need to process the directory and pass the arguments into the configs
    wordexp_t exp_result;
    wordexp(playlist_conf.music_dir, &exp_result, 0);
    strcpy(playlist_conf.music_dir, exp_result.we_wordv[0]);
    printf("The music dir path: %s\n", playlist_conf.music_dir);

    wordfree(&exp_result);
    return start_fmd(&playlist_conf, &player_conf);
}
