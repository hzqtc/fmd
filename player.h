#ifndef _FM_PLAYER_H_
#define _FM_PLAYER_H_

#include "playlist.h"
#include <ao/ao.h>
#include <curl/curl.h>
#include <pthread.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/samplefmt.h>
#include <libavutil/common.h>
#include <libswresample/swresample.h>

#define IOBUF_SIZE 20480
#define HEADERBUF_SIZE 32768
#define AUDIO_REFILL_THRESH 4096

enum fm_player_status {
    FM_PLAYER_PLAY,
    FM_PLAYER_PAUSE,
    FM_PLAYER_STOP
};

typedef struct {
    // in time_base format
    int duration;
    // in seconds
    int length;
    AVRational time_base;
} fm_player_info_t;

typedef struct {
    int rate;
    int channels;
    int encoding;
    char driver[16];
    char dev[16];
} fm_player_config_t;

typedef struct {
    enum AVSampleFormat sample_fmt;
    uint64_t channel_layout;
    int sample_rate;
    int bits;
} SwrFormat;

typedef struct fm_player {
    ao_device *dev;

    // the current playing song; note that it can become a dangling pointer if the playlist frees that song
    // so make sure that the player is stopped first
    fm_song_t *song;

    // the more complicated avcodec data
    AVCodec *codec;
    AVCodecContext *context;
    AVPacket avpkt;
    AVFormatContext *format_context;
    int audio_stream_idx;
    // decoding
    AVFrame *frame;
    // interweaving buffer
    uint8_t *interweave_buf;
    int interweave_buf_size;
    // resampling
    int resampled;
    struct SwrContext *swr_context;
    SwrFormat src_swr_format;
    SwrFormat dest_swr_format;
    uint8_t **swr_buf;
    int dest_swr_nb_samples;

    fm_player_info_t info;
    fm_player_config_t config;
    enum fm_player_status status;

    pthread_t tid_ack;
    int sig_ack;

    pthread_t tid_play;
    pthread_cond_t cond_play;
    pthread_mutex_t mutex_status;
} fm_player_t;

int fm_player_set_song(fm_player_t *pl, fm_song_t *song);
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
