#ifndef _FM_PLAYLIST_H_
#define _FM_PLAYLIST_H_

#include <curl/curl.h>

typedef struct fm_song {
    char *title;
    char *artist;
    char *album;
    int pubdate;
    char *cover;
    char *url;
    char *audio;
    int sid;
    int like;
    struct fm_song *next;
} fm_song_t;

typedef struct fm_history {
    int sid;
    char state;
    struct fm_history *next;
} fm_history_t;

typedef struct {
    int channel;
    int uid;
    char uname[16];
    char token[16];
    int expire;
    char kbps[8];
} fm_playlist_config_t;

typedef struct {
    fm_history_t *history;
    fm_song_t *playlist;

    char *api;
    char *channel_api;
    char *app_name;
    char *version;

    fm_playlist_config_t config;

    CURL* curl;
} fm_playlist_t;

void fm_playlist_init(fm_playlist_t *pl, fm_playlist_config_t *config);
void fm_playlist_cleanup(fm_playlist_t *pl);

fm_song_t* fm_playlist_current(fm_playlist_t *pl);
fm_song_t* fm_playlist_next(fm_playlist_t *pl);
fm_song_t* fm_playlist_skip(fm_playlist_t *pl);
fm_song_t* fm_playlist_ban(fm_playlist_t *pl);

void fm_playlist_rate(fm_playlist_t *pl);
void fm_playlist_unrate(fm_playlist_t *pl);

#endif
