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
static double download_delta_margin = 0.001;

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
    size_t bytes = size * nmemb;
    if (pl->status != FM_PLAYER_STOP) {
        pl->info.file_size += bytes;

        if (pl->download.tmpstream) {
            fwrite(ptr, size, nmemb, pl->download.tmpstream);
            /*printf("Appended transfer of size %d \n", (int) bytes);*/
        } 
        switch(pl->mode) {
            case plMP3:
                mpg123_feed(pl->mh, (unsigned char*) ptr, bytes);
                break;
            case plMP4:{
                char *start = pl->avpkt.data + pl->avpkt.size;
                if (start + bytes <= (char *) pl->inbuf + sizeof(pl->inbuf)) {
                    // just append it to the buf
                    printf("Appending to avptk buffer\n");
                    memcpy(start, ptr, bytes);
                    pl->avpkt.size += bytes;
                } else {
                    printf("Buffer full. Cannot add to buffer anymore\n");
                }
                break;
           }
        }
        pthread_cond_signal(&pl->cond_play);
    }
    return bytes;
}

static void* download_thread(void *data)
{
    fm_player_t *pl = (fm_player_t*) data;

    curl_easy_perform(pl->curl);
    pthread_cond_signal(&pl->cond_play);

    switch(pl->mode) {
        case plMP3:
            mpg123_set_filesize(pl->mh, pl->info.file_size);
            pl->info.samples = mpg123_length(pl->mh);
            break;
        case plMP4:
            break;
    }

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

        switch (pl->mode) {
            case plMP3:
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
                        } else {
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
                break;
            case plMP4: {
                int got_frame = 0;
                avcodec_get_frame_defaults(pl->decoded_frame);

                int len = avcodec_decode_audio4(pl->context, pl->decoded_frame, &got_frame, &pl->avpkt);
                if (len >= 0 && got_frame) {
                    /* if a frame has been decoded, output it */
                    int data_size = av_samples_get_buffer_size(NULL, pl->context->channels,
                                                               pl->decoded_frame->nb_samples,
                                                               pl->context->sample_fmt, 1);
                    ao_play(pl->dev, (char *) pl->decoded_frame->data[0], data_size);
                    printf("Successfully played a frame for the audio!\n");
                    pl->avpkt.size -= len;
                    pl->avpkt.data += len;
                    pl->avpkt.dts =
                    pl->avpkt.pts = AV_NOPTS_VALUE;
                    if (pl->inbuf + sizeof(pl->inbuf) - pl->avpkt.data < AUDIO_REFILL_THRESH) {
                        /* Refill the input buffer, to avoid trying to decode
                         * incomplete frames. Instead of this, one could also use
                         * a parser, or use a proper container format through
                         * libavformat. */
                        memmove(pl->inbuf, pl->avpkt.data, pl->avpkt.size);
                        pl->avpkt.data = pl->inbuf;
                    }
                } else if (pthread_kill(pl->tid_dl, 0) == 0) {
                    pthread_mutex_lock(&pl->mutex_status);
                    pthread_cond_wait(&pl->cond_play, &pl->mutex_status);
                    pthread_mutex_unlock(&pl->mutex_status);
                } else {
                    if (pl->tid_ack > 0) {
                        pthread_kill(pl->tid_ack, pl->sig_ack);
                    }
                    return pl;
                }
                break;
            }
        }
    }

    return pl;
}

int fm_player_open(fm_player_t *pl, fm_player_config_t *config, fm_playlist_t *playlist)
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
    curl_easy_setopt(pl->curl, CURLOPT_CONNECTTIMEOUT, 10);
    /*curl_easy_setopt(pl->curl, CURLOPT_VERBOSE, 1);*/
    curl_easy_setopt(pl->curl, CURLOPT_WRITEDATA, pl);
    curl_easy_setopt(pl->curl, CURLOPT_HEADER, 1);

    pl->tid_ack = 0;

    pthread_mutex_init(&pl->mutex_status, NULL);
    pthread_cond_init(&pl->cond_play, NULL);

    pl->status = FM_PLAYER_STOP;

    pl->download.tmpstream = NULL;
    sprintf(pl->download.tmpstream_path, "%s/%s", config->tmp_dir, tmpstream_fname);
    sprintf(pl->download.tmpimage_path, "%s/%s", config->tmp_dir, tmpimage_fname);

    // wiring up the playlist
    pl->playlist = playlist;

    // setting up the packet
    av_init_packet(&pl->avpkt);
    // wiring up the input buf
    pl->avpkt.data = pl->inbuf;
    if (!(pl->decoded_frame = avcodec_alloc_frame())) {
        fprintf(stderr, "Could not allocate audio frame\n");
        exit(1);
    }

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
    if (pl->download.tmpstream) {
        fclose(pl->download.tmpstream);
        pl->download.tmpstream = NULL;
    }
}

int fm_player_set_url(fm_player_t *pl, fm_song_t *song)
{
    if (pl->status != FM_PLAYER_STOP) {
        fm_player_stop(pl);
    }

    // close the file handler first
    if (pl->download.tmpstream) {
        fclose(pl->download.tmpstream);
        // download the file 
        if (pl->download.like) {
            double content_size = 0;
            curl_easy_getinfo(pl->curl, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &content_size);
            printf("curlinfo_content_length_download: %f\n", content_size);

            struct stat st;
            stat(pl->download.tmpstream_path, &st);
            double filesize = st.st_size;
            printf("Actual size of the tmp file: %f\n", filesize);

            if (content_size - filesize < download_delta_margin) {
                printf("Attemping to mv and tag the file\n");
                // first move the file to a secure location to avoid it being truncated later
                char cmd[2048];
                char btp[128], bart[128], btitle[128], balb[128], bmd[128], btm[128], bcover[128], burl[128]; 
                sprintf(cmd, 
                        "src=$'%s';"
                        "artist=$'%s'; title=$'%s'; album=$'%s'; date='%d';"
                        "[[ \"$date\" =~ [0-9]{4} ]] && datearg=\"--release-year $date\" || datearg=;"
                        "dest=$'%s'\"/${artist//\\//|}/${title//\\//|}.%s\";"
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
                        escapesh(bmd, pl->playlist->config.music_dir),
                        pl->mode == plMP3 ? "mp3" : "m4a",
                        escapesh(btm, pl->download.tmpimage_path), 
                        escapesh(bcover, pl->download.cover),
                        escapesh(burl, pl->download.url));
                printf("Move and tag command: %s\n", cmd);
                system(cmd);
            }
        }
    } 

    if (!song) {
        printf("No song to play\n");
        return -1;
    }

    char *url = song->audio;
    // set the mode depending the type of the file
    // a simple check on the extension
    // we need to determine the codec to use for the specific input stream
    printf("Determining the stream format\n");
    char *ext = url + strlen(url) - 4;
    printf("Extension is %s\n", ext);
    if (strcmp(ext, ".mp3") == 0) {
        printf("mp3 determined\n");
        pl->mode = plMP3;
    } else if (strcmp(ext, ".m4a") == 0) {
        printf("mp4 determined\n");
        pl->mode = plMP4;
        // initialize the m4a related avcodec details
        /* find the mpeg audio decoder */
        pl->codec = avcodec_find_decoder(AV_CODEC_ID_AAC);
        if (!pl->codec) {
            fprintf(stderr, "Codec not found\n");
            exit(1);
        }
        printf("try to alloc context\n");
        pl->context = avcodec_alloc_context3(pl->codec);
        printf("context set\n");
        if (!pl->context) {
            fprintf(stderr, "Could not allocate audio codec context\n");
            exit(1);
        }
        /* open it */
        printf("try to open codec\n");
        if (avcodec_open2(pl->context, pl->codec, NULL) < 0) {
            fprintf(stderr, "Could not open codec\n");
            exit(1);
        }
        pl->avpkt.size = 0;
        // reset all input buf
        memset(pl->inbuf, 0, sizeof(pl->inbuf));
    } else {
        printf("Unknown stream type\n");
        return -1;
    }

    // remove the last tmp file
    if (pl->playlist->config.mode == plLocal) {
        pl->download.tmpstream = NULL;
    } else {
        pl->download.tmpstream = fopen(pl->download.tmpstream_path, "w");
        strcpy(pl->download.title, song->title);
        strcpy(pl->download.artist, song->artist);
        strcpy(pl->download.album, song->album);
        strcpy(pl->download.cover, song->cover);
        strcpy(pl->download.url, song->url);
        pl->download.pubdate = song->pubdate;
        pl->download.like = song->like;
    }

    printf("Player set url: %s\n", url);
    curl_easy_setopt(pl->curl, CURLOPT_URL, url);
    return 0;
}

void fm_player_set_ack(fm_player_t *pl, pthread_t tid, int sig)
{
    pl->tid_ack = tid;
    pl->sig_ack = sig;
}

int fm_player_pos(fm_player_t *pl)
{
    switch (pl->mode) {
        case plMP3:
            return mpg123_tell(pl->mh) / pl->config.rate;
        case plMP4:
            return 0;
    }
}

int fm_player_length(fm_player_t *pl)
{
    switch (pl->mode) {
        case plMP3:
            return pl->info.samples / pl->config.rate;
        case plMP4:
            return 0;
    }
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
    avcodec_register_all();
}

void fm_player_exit()
{
    mpg123_exit();
    ao_shutdown();
}
