#ifndef _FM_PLAYLIST_H_
#define _FM_PLAYLIST_H_

#include "downloader.h"
#include <curl/curl.h>
#define LOCAL_CHANNEL "999"
// out of them 1 is used for emergency (when all downloaders are downloading
#define N_SONG_DOWNLOADERS 2
#define N_LOCAL_CHANNEL_FETCH 25
#define PLAYLIST_REFILL_THRESHOLD 3


enum fm_playlist_mode {
    plLocal,
    plDouban,
    plJing
};

typedef struct fm_song {
    char title[128];
    char artist[128];
    char album[128];
    int pubdate;
    char cover[128];
    char url[128];
    char audio[128];
    char kbps[8];
    // the extension for the song // used during caching process
    char ext[4];
    int sid;
    int like;
    struct fm_song *next;
    // the total length for the song (in seconds)
    int length;
    // the corresponding file path for this song
    char filepath[128];
    // the corresponding downloader (null if it's not being downloaded)
    downloader_t *downloader;
} fm_song_t;

typedef struct fm_history {
    int sid;
    char state;
    struct fm_history *next;
} fm_history_t;

typedef struct {
    char channel[64];

    // douban mode
    int douban_uid;
    char uname[16];
    char douban_token[16];
    int expire;
    char kbps[8];

    // local mode
    char music_dir[128];

    // jing mode
    int jing_uid;
    char jing_atoken[50];
    char jing_rtoken[50];
} fm_playlist_config_t;

typedef struct {
    // the current playlist mode
    enum fm_playlist_mode mode;
    fm_song_t *current;

    // douban mode
    fm_history_t *history;
    char *douban_api;
    char *douban_channel_api;
    char *app_name;
    char *version;

    // jing mode
    char *jing_api;

    fm_playlist_config_t config;

    // the downloader stack will handle all the download tasks
    downloader_stack_t *stack;

    // holding a reference to the stop function; needs to be provided by the delegate
    void (*fm_player_stop)();
    //// song download section
    downloader_t *song_downloaders[N_SONG_DOWNLOADERS];
    // a flag telling the song download thread to stop download
    int song_download_stop;
    pthread_t tid_download;
    fm_song_t *current_download;
    pthread_mutex_t mutex_song_download_stop;
    pthread_mutex_t mutex_current_download;
    pthread_cond_t cond_song_download_restart;
} fm_playlist_t;

void fm_playlist_init(fm_playlist_t *pl, fm_playlist_config_t *config, void (*fm_player_stop)());
void fm_playlist_cleanup(fm_playlist_t *pl);

void fm_playlist_update_mode(fm_playlist_t *pl);
fm_song_t* fm_playlist_current(fm_playlist_t *pl);
fm_song_t* fm_playlist_next(fm_playlist_t *pl);
fm_song_t* fm_playlist_skip(fm_playlist_t *pl, int force_refresh);
fm_song_t* fm_playlist_ban(fm_playlist_t *pl);

void fm_playlist_rate(fm_playlist_t *pl);
void fm_playlist_unrate(fm_playlist_t *pl);

#endif
