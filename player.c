#include "player.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/stat.h>
#include <string.h>
#include <libgen.h>

static char *tmpstream_fname = "fmdstream.mp3";
static char *tmpimage_fname = "cover.jpg";
static double download_delta_margin = 0.0000001;

void fm_player_download_info_unrate(fm_player_t *pl)
{
    pl->download.like = 0;
}

void fm_player_download_info_rate(fm_player_t *pl)
{
    pl->download.like = 1;
}

static size_t download_callback(char *ptr, size_t size, size_t nmemb, void *userp)
{
    fm_player_t *pl = (fm_player_t*) userp;
    if (pl->status == FM_PLAYER_STOP) {
        return 0;
    }
    else {
        size_t bytes = size * nmemb;
        pl->info.file_size += bytes;

        // if the url does not start with file then we can download it
        if (pl->download.tmpstream) {
            fwrite(ptr, size, nmemb, pl->download.tmpstream);
            printf("Appended transfer of size %d from %s \n", (int) bytes, pl->download.audio);
        }

        mpg123_feed(pl->mh, (unsigned char*) ptr, bytes);
        pthread_cond_signal(&pl->cond_play);
        return bytes;
    }
}

static void* download_thread(void *data)
{
    fm_player_t *pl = (fm_player_t*) data;

    curl_easy_perform(pl->curl);
    pthread_cond_signal(&pl->cond_play);

    mpg123_set_filesize(pl->mh, pl->info.file_size);
    pl->info.samples = mpg123_length(pl->mh);

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
        switch (err) {
            case MPG123_OK:
                ao_play(pl->dev, (char*) audio, size);
                break;
            case MPG123_NEED_MORE:
                if (pthread_kill(pl->tid_dl, 0) == 0) {
                    pthread_mutex_lock(&pl->mutex_status);
                    pthread_cond_wait(&pl->cond_play, &pl->mutex_status);
                    pthread_mutex_unlock(&pl->mutex_status);
                }
                else {
                    if (pl->tid_ack > 0) {
                        pthread_kill(pl->tid_ack, pl->sig_ack);
                    }
                    return pl;
                }
                break;
            case MPG123_NEW_FORMAT:
                break;
            default:
                fprintf(stderr, "mpg123 deocde return: %d\n", err);
                break;
        }
    }

    return pl;
}

int fm_player_open(fm_player_t *pl, fm_player_config_t *config)
{
    pl->config = *config;

    pl->mh = mpg123_new(NULL, NULL);
    mpg123_format_none(pl->mh);
    mpg123_format(pl->mh, config->rate, config->channels, config->encoding);

    ao_sample_format ao_fmt;
    ao_fmt.rate = config->rate;
    ao_fmt.channels = config->channels;
    ao_fmt.bits = mpg123_encsize(config->encoding) * 8;
    ao_fmt.byte_format = AO_FMT_NATIVE;
    ao_fmt.matrix = 0;

    int driver = ao_driver_id(config->driver);
    if (driver == -1) {
        return -1;
    }

    ao_info *driver_info = ao_driver_info(driver);
    printf("Player audio driver: %s\n", driver_info->name);
    printf("Player sample rate: %d Hz\n", pl->config.rate);
    ao_option *options = NULL;
    if (config->dev[0] != '\0') {
        ao_append_option(&options, "dev", config->dev);
    }
    pl->dev = ao_open_live(driver, &ao_fmt, options);
    ao_free_options(options);
    if (pl->dev == NULL)
        return -1;

    pl->curl = curl_easy_init();
    curl_easy_setopt(pl->curl, CURLOPT_WRITEFUNCTION, download_callback);
    curl_easy_setopt(pl->curl, CURLOPT_WRITEDATA, pl);

    pl->tid_ack = 0;

    pthread_mutex_init(&pl->mutex_status, NULL);
    pthread_cond_init(&pl->cond_play, NULL);

    pl->status = FM_PLAYER_STOP;

    pl->download.tmpstream = NULL;
    sprintf(pl->download.tmpstream_path, "%s/%s", config->tmp_dir, tmpstream_fname);
    sprintf(pl->download.tmpimage_path, "%s/%s", config->tmp_dir, tmpimage_fname);

    return 0;
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

    // free the tmpstream
    if (pl->download.tmpstream)
        fclose(pl->download.tmpstream);
}

void fm_player_set_url(fm_player_t *pl, fm_song_t *song)
{
    // close the file handler first
    if (pl->download.tmpstream) {
        fclose(pl->download.tmpstream);
        // download the file 
        if (pl->download.like) {
            double dl_size = 0;
            curl_easy_getinfo(pl->curl, CURLINFO_SIZE_DOWNLOAD, &dl_size);
            printf("curlinfo_size_download: %f\n", dl_size);
            /*long req_size = 0;*/
            /*curl_easy_getinfo(pl->curl, CURLINFO_REQUEST_SIZE, &req_size);*/
            /*printf("curlinfo_request_size: %ld\n", req_size);*/
            /*double content_size = 0;*/
            /*curl_easy_getinfo(pl->curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &content_size);*/
            /*printf("curlinfo_content_length_download: %f\n", content_size);*/

            struct stat st;
            stat(pl->download.tmpstream_path, &st);
            double filesize = st.st_size;
            printf("Actual size of the tmp file: %f\n", filesize);

            if (dl_size - filesize < download_delta_margin) {
                printf("Attemping to mv and tag the file\n");
                // first move the file to a secure location to avoid it being truncated later
                char cmd[2048];
                char btp[128], bart[128], btitle[128], balb[128], bmd[128], btm[128], bcover[128], burl[128]; 
                sprintf(cmd, 
                        "src=$'%s';"
                        "artist=$'%s'; title=$'%s'; album=$'%s'; date='%d';"
                        "[[ \"$date\" =~ [0-9]{4} ]] && datearg=\"--release-year $date\" || datearg=;"
                        "dest=$'%s'\"/${artist//\\//|}/${title//\\//|}.mp3\";"
                        "[ -f \"$dest\" ] && exit 0;"
                        "mkdir -p \"$(dirname \"$dest\")\";"
                        "mv -f \"$src\" \"$dest\";" 
                        "tmpimg=$'%s'; cover=$'%s';"
                        "(curl --connect-timeout 15 -m 60 -o \"$tmpimg\" \"$cover\";"
                        "([ -f \"$tmpimg\" ] && identify \"$tmpimg\") && cover=\"$tmpimg\" || cover=\"${cover//:/\\:}\";"
                        "page_url=$'%s';"
                        "page_url=\"${page_url//:/\\:}\";"
                        "eyeD3 --artist \"$artist\" --album \"$album\" --title \"$title\" $datearg --add-image \"$cover:FRONT_COVER\" --url-frame \"WORS:$page_url\" \"$dest\";"
                        "rm -f \"$tmpimg\") &", 
                        escapesh(btp, pl->download.tmpstream_path), 
                        escapesh(bart, pl->download.artist), 
                        escapesh(btitle, pl->download.title), 
                        escapesh(balb, pl->download.album), 
                        pl->download.pubdate,
                        escapesh(bmd, pl->config.music_dir),
                        escapesh(btm, pl->download.tmpimage_path), 
                        escapesh(bcover, pl->download.cover),
                        escapesh(burl, pl->download.url));
                printf("Move and tag command: %s\n", cmd);
                system(cmd);
            }
        }
    }

    char *url = song->audio;

    // remove the last tmp file
    if (strncmp(url, "file:/", 6) == 0)
        pl->download.tmpstream = NULL;
    else {
        pl->download.tmpstream = fopen(pl->download.tmpstream_path, "w");
        strcpy(pl->download.title, song->title);
        strcpy(pl->download.artist, song->artist);
        strcpy(pl->download.album, song->album);
        strcpy(pl->download.cover, song->cover);
        strcpy(pl->download.url, song->url);
        strcpy(pl->download.audio, song->audio);
        pl->download.pubdate = song->pubdate;
        pl->download.like = song->like;
    }

    printf("Player set url: %s\n", url);
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
    return mpg123_tell(pl->mh) / pl->config.rate;
}

int fm_player_length(fm_player_t *pl)
{
    return pl->info.samples / pl->config.rate;
}

void fm_player_play(fm_player_t *pl)
{
    printf("Player play\n");
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
    printf("Player pause\n");
    pthread_mutex_lock(&pl->mutex_status);
    pl->status = FM_PLAYER_PAUSE;
    pthread_mutex_unlock(&pl->mutex_status);
}

void fm_player_stop(fm_player_t *pl)
{
    printf("Player stop\n");
    if (pl->status != FM_PLAYER_STOP) {
        pthread_mutex_lock(&pl->mutex_status);
        pl->status = FM_PLAYER_STOP;
        pthread_mutex_unlock(&pl->mutex_status);
        pthread_cond_signal(&pl->cond_play);

        pthread_join(pl->tid_dl, NULL);
        pthread_join(pl->tid_play, NULL);
        mpg123_close(pl->mh);
    }
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
