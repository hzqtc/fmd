#ifndef _FM_PLAYER_H_
#define _FM_PLAYER_H_

#include <mpg123.h>
#include <ao/ao.h>
#include <curl/curl.h>

#include <pthread.h>

enum fm_player_status {
    FM_PLAYER_PLAY,
    FM_PLAYER_PAUSE,
    FM_PLAYER_STOP
};

typedef struct {
    size_t file_size;
    int samples;
} fm_player_info_t;

typedef struct {
    int rate;
    int channels;
    int encoding;
    char driver[16];
    char dev[16];
} fm_player_config_t;

typedef struct fm_player {
    mpg123_handle *mh;
    ao_device *dev;
    CURL *curl;

    fm_player_info_t info;
    fm_player_config_t config;
    enum fm_player_status status;

    pthread_t tid_ack;
    int sig_ack;

    pthread_t tid_dl;
    pthread_t tid_play;
    pthread_mutex_t mutex_status;
    pthread_cond_t cond_play;
} fm_player_t;

void fm_player_set_url(fm_player_t *pl, const char *url);
void fm_player_set_ack(fm_player_t *pl, pthread_t tid, int sig);

int fm_player_pos(fm_player_t *pl);
int fm_player_length(fm_player_t *pl);

void fm_player_play(fm_player_t *pl);
void fm_player_pause(fm_player_t *pl);
void fm_player_toggle(fm_player_t *pl);
void fm_player_stop(fm_player_t *pl);

int fm_player_open(fm_player_t *pl, fm_player_config_t *config);
void fm_player_close(fm_player_t *pl);
void fm_player_init();
void fm_player_exit();

#endif
