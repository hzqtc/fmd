#include "player.h"

#include <stdio.h>
#include <signal.h>

static size_t download_callback(char *ptr, size_t size, size_t nmemb, void *userp)
{
    fm_player_t *pl = (fm_player_t*) userp;
    if (pl->status == FM_PLAYER_STOP) {
        return 0;
    }
    else {
        size_t bytes = size * nmemb;
        pl->info.file_size += bytes;
        mpg123_feed(pl->mh, (unsigned char*) ptr, bytes);
        pthread_cond_signal(&pl->cond_play);
        return bytes;
    }
}

static void* download_thread(void *data)
{
    fm_player_t *pl = (fm_player_t*) data;

    curl_easy_perform(pl->curl);

    mpg123_set_filesize(pl->mh, pl->info.file_size);
    pl->info.samples = mpg123_length(pl->mh);

    pthread_cond_signal(&pl->cond_play);

    return pl;
}

static void* play_thread(void *data)
{
    int err;
    off_t off;
    unsigned char* audio;
    size_t size;

    fm_player_t *pl = (fm_player_t*) data;

    while (pl->status != FM_PLAYER_STOP) {
        pthread_mutex_lock(&pl->mutex_status);
        while (pl->status == FM_PLAYER_PAUSE) {
            pthread_cond_wait(&pl->cond_play, &pl->mutex_status);
        }
        pthread_mutex_unlock(&pl->mutex_status);

        if (pl->status == FM_PLAYER_STOP) {
            break;
        }

        err = mpg123_decode_frame(pl->mh, &off, &audio, &size);
        if (err == MPG123_OK) {
            ao_play(pl->dev, (char*) audio, size);
        }
        else if (err == MPG123_NEED_MORE) {
            if (pthread_kill(pl->tid_dl, 0) == 0) {
                pthread_mutex_lock(&pl->mutex_status);
                pthread_cond_wait(&pl->cond_play, &pl->mutex_status);
                pthread_mutex_unlock(&pl->mutex_status);
            }
            else if (pl->tid_ack >= 0) {
                pthread_kill(pl->tid_ack, pl->sig_ack);
                break;
            }
        }
        else if (err == MPG123_NEW_FORMAT ) {
            ;
        }
        else {
            fprintf(stderr, "mpg123 deocde return: %d\n", err);
        }
    }

    return pl;
}

void fm_player_open(fm_player_t *pl)
{
    pl->format.rate = 44100;
    pl->format.channels = 2;
    pl->format.encoding = MPG123_ENC_SIGNED_16;

    pl->mh = mpg123_new(NULL, NULL);
    mpg123_format_none(pl->mh);
    mpg123_format(pl->mh, pl->format.rate, pl->format.channels, pl->format.encoding);

    ao_sample_format format;
    format.rate = pl->format.rate;
    format.channels = pl->format.channels;
    format.bits = mpg123_encsize(pl->format.encoding) * 8;
    format.byte_format = AO_FMT_NATIVE;
    format.matrix = 0;
    ao_option *options = NULL;
    ao_append_option(&options, "dev", "default");
    int driver = ao_driver_id("alsa");
    pl->dev = ao_open_live(driver, &format, options);
    ao_free_options(options);

    pl->curl = curl_easy_init();
    curl_easy_setopt(pl->curl, CURLOPT_WRITEFUNCTION, download_callback);
    curl_easy_setopt(pl->curl, CURLOPT_WRITEDATA, pl);

    pl->tid_ack = -1;

    pthread_mutex_init(&pl->mutex_status, NULL);
    pthread_cond_init(&pl->cond_play, NULL);

    pl->status = FM_PLAYER_STOP;
}

void fm_player_close(fm_player_t *pl)
{
    if (pl->status != FM_PLAYER_STOP) {
        fm_player_stop(pl);
    }

    ao_close(pl->dev);
    mpg123_delete(pl->mh);
    curl_easy_cleanup(pl->curl);

    pthread_mutex_destroy(&pl->mutex_status);
    pthread_cond_destroy(&pl->cond_play);
}

void fm_player_set_url(fm_player_t *pl, const char *url)
{
    if (pl->status != FM_PLAYER_STOP) {
        fm_player_stop(pl);
    }
    curl_easy_setopt(pl->curl, CURLOPT_URL, url);
}

void fm_player_set_ack(fm_player_t *pl, pthread_t tid, int sig)
{
    pl->tid_ack = tid;
    pl->sig_ack = sig;
}

int fm_player_pos(fm_player_t *pl)
{
    return mpg123_tell(pl->mh) / pl->format.rate;
}

int fm_player_length(fm_player_t *pl)
{
    return pl->info.samples / pl->format.rate;
}

void fm_player_play(fm_player_t *pl)
{
    if (pl->status == FM_PLAYER_STOP) {
        mpg123_open_feed(pl->mh);
        pl->status = FM_PLAYER_PLAY;
        pl->info.file_size = 0;
        pl->info.samples = 0;
        pthread_create(&pl->tid_dl, NULL, download_thread, pl);
        pthread_create(&pl->tid_play, NULL, play_thread, pl);
    }
    else if (pl->status == FM_PLAYER_PAUSE) {
        pthread_mutex_lock(&pl->mutex_status);
        pl->status = FM_PLAYER_PLAY;
        pthread_mutex_unlock(&pl->mutex_status);
        pthread_cond_signal(&pl->cond_play);
    }
}

void fm_player_pause(fm_player_t *pl)
{
    pthread_mutex_lock(&pl->mutex_status);
    pl->status = FM_PLAYER_PAUSE;
    pthread_mutex_unlock(&pl->mutex_status);
}

void fm_player_toggle(fm_player_t *pl)
{
    if (pl->status == FM_PLAYER_PLAY)
        fm_player_pause(pl);
    else
        fm_player_play(pl);
}

void fm_player_stop(fm_player_t *pl)
{
    pthread_mutex_lock(&pl->mutex_status);
    pl->status = FM_PLAYER_STOP;
    pthread_mutex_unlock(&pl->mutex_status);
    pthread_cond_signal(&pl->cond_play);

    pthread_cancel(pl->tid_dl);
    pthread_cancel(pl->tid_play);
    pthread_join(pl->tid_dl, NULL);
    pthread_join(pl->tid_play, NULL);
    mpg123_close(pl->mh);
}

void fm_player_init()
{
    ao_initialize();
    mpg123_init();
}

void fm_player_exit()
{
    mpg123_exit();
    ao_shutdown();
}
