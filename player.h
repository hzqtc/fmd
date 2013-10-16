#ifndef _FM_PLAYER_H_
#define _FM_PLAYER_H_

#include "playlist.h"
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
    char music_dir[128];
    char tmp_dir[128];
    char driver[16];
    char dev[16];
} fm_player_config_t;

typedef struct {
    char title[128];
    char artist[128];
    char album[128];
    int pubdate;
    char cover[128];
    char url[128];
    char audio[128];
    int like;

    // file related action
    FILE *tmpstream;
    char tmpstream_path[128];
    char tmpimage_path[128];
    char music_dir[128];
} fm_download_info_t;

typedef struct fm_player {
    mpg123_handle *mh;
    ao_device *dev;
    CURL *curl;

    // encapsulating all the necessary information for downloading purposes
    fm_download_info_t download;

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

// these two methods to transmit rating information to the downloader (determining whether download should be performed)
void fm_player_download_info_unrate(fm_player_t *pl);
void fm_player_download_info_rate(fm_player_t *pl);

void fm_player_set_url(fm_player_t *pl, fm_song_t *song);
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
