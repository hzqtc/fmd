#include "player.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/stat.h>
#include <string.h>
#include <libgen.h>
#include <unistd.h>

static void* play_thread(void *data)
{
    fm_player_t *pl = (fm_player_t*) data;

    int ret;

    // read the audio frames
    int got_frame;

    // interweaving
    uint8_t *interweave_data;
    int sample_is_planar = av_sample_fmt_is_planar(pl->dest_swr_format.sample_fmt);
    unsigned sample_size = av_get_bytes_per_sample(pl->dest_swr_format.sample_fmt);

    // for resampling
    int dest_nb_samples;

    // final read buffer
    char *ao_buf;
    int ao_size;

    while (pl->status != FM_PLAYER_STOP) {
        pthread_mutex_lock(&pl->mutex_status);
        while (pl->status == FM_PLAYER_PAUSE) {
            pthread_cond_wait(&pl->cond_play, &pl->mutex_status);
        }
        pthread_mutex_unlock(&pl->mutex_status);

        if (pl->status == FM_PLAYER_STOP) {
            break;
        }

        // decode the frame
        if ((ret = av_read_frame(pl->format_context, &pl->avpkt)) < 0) {
            // couldn't decode the frame
            printf("Could not decode the frame\n");
            continue;
        }
        if (pl->avpkt.stream_index == pl->audio_stream_idx) {
            avcodec_get_frame_defaults(pl->frame);
            ret = avcodec_decode_audio4(pl->context, pl->frame, &got_frame, &pl->avpkt);
            if (ret < 0) {
                fprintf(stderr, "Error decoding audio\n");
                continue;
            } else if (got_frame) {
                ao_size = av_samples_get_buffer_size(NULL, pl->frame->channels, pl->frame->nb_samples, pl->frame->format, 1);
                ao_buf = (char *) pl->frame->extended_data[0];

                // first resample the buffer if necessary 
                if (pl->resampled) {
                    dest_nb_samples = av_rescale_rnd(swr_get_delay(pl->swr_context, pl->src_swr_format.sample_rate) + pl->frame->nb_samples, pl->src_swr_format.sample_rate, pl->src_swr_format.sample_rate, AV_ROUND_UP);
                    if (dest_nb_samples > pl->dest_swr_nb_samples) {
                        if (pl->swr_buf)
                            av_freep(*pl->swr_buf);
                        if (av_samples_alloc_array_and_samples(&pl->swr_buf, pl->frame->linesize, pl->frame->channels, dest_nb_samples, pl->dest_swr_format.sample_fmt, 0) < 0) {
                            fprintf(stderr, "Could not allocate destination samples\n");
                            exit(-1);
                        }
                        pl->dest_swr_nb_samples = dest_nb_samples;
                    }
                    // covert to destination format
                    ret = swr_convert(pl->swr_context, pl->swr_buf, dest_nb_samples, (const uint8_t **) pl->frame->extended_data, pl->frame->nb_samples);
                    if (ret < 0) {
                        fprintf(stderr, "Could not resample the audio\n");
                        exit(-1);
                    } 
                    // get the resampled buffer size
                    ao_buf = (char *) *pl->swr_buf;
                    ao_size = av_samples_get_buffer_size(pl->frame->linesize, pl->frame->channels, ret, pl->dest_swr_format.sample_fmt, 1);
                }
                // copying the frame
                if (sample_is_planar && pl->frame->channels > 1) {
                    // printf("Copying multiple channels\n");
                    if(pl->interweave_buf_size < ao_size) {
                        if (pl->interweave_buf)
                            av_freep(pl->interweave_buf);
                        pl->interweave_buf = av_malloc(ao_size);
                        if (!pl->interweave_buf) {
                            // Not enough memory - shouldn't happen 
                            fprintf(stderr, "Unable to allocate the buffer\n");
                        }
                        pl->interweave_buf_size = ao_size;
                    }
                    interweave_data = pl->interweave_buf;
                    unsigned f, channel;
                    for (f = 0; f < pl->frame->nb_samples; ++f) {
                        for (channel = 0; channel < pl->frame->channels; ++channel) {
                            memcpy(interweave_data, pl->frame->extended_data[channel] + f * sample_size, sample_size);
                            interweave_data += sample_size;
                        }
                    }
                    ao_buf = (char *) pl->interweave_buf;
                }
                ao_play(pl->dev, ao_buf, ao_size);
                // add the duration to the info
                pl->info.duration += pl->avpkt.duration;
            }
        }
    }

    return pl;
}

static int ffmpeg_stream_read(void *opaque, uint8_t *buf, int size)
{
    fm_player_t *pl = (void *) opaque;
    if (!pl->file)
        return -1;
    if (feof(pl->file))
        return AVERROR_EOF;
    if (ferror(pl->file))
        return -1;
    // read in some data into the buffer
    return fread(buf, sizeof(uint8_t), size, pl->file);
}

static int64_t ffmpeg_stream_seek(void *opaque, int64_t pos, int whence)
{
    fm_player_t *pl = (void *) opaque;
    if (!pl->file)
        return -1;
    return fseek(pl->file, pos, whence);
    /*if (ret < 0)*/
        /*return ret;*/
    /*printf("Stream position seeked to %ld\n", n);*/
}


int fm_player_open(fm_player_t *pl, fm_player_config_t *config)
{
    pl->config = *config;

    ao_sample_format ao_fmt;
    ao_fmt.rate = config->rate;
    ao_fmt.channels = config->channels;
    // setting the bit rate to 16 for the time being
    ao_fmt.bits = 16;
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

    pl->tid_ack = 0;

    pthread_mutex_init(&pl->mutex_status, NULL);
    pthread_cond_init(&pl->cond_play, NULL);

    pl->status = FM_PLAYER_STOP;

    pl->context = NULL;
    pl->format_context = NULL;
    // create the avio layer
    /*pl->iobuf = av_malloc(IOBUF_SIZE);*/
    /*printf("Attempting to initiate the read/write functions\n");*/
    /*pl->io_context = avio_alloc_context(pl->iobuf, IOBUF_SIZE, 0, pl, ffmpeg_stream_read, NULL, ffmpeg_stream_seek);*/
    /*if (!pl->io_context) {*/
        /*fprintf(stderr, "Failed allocating avio context\n");*/
        /*exit(-1);*/
    /*}*/
    /*pl->io_context->direct = 1;*/

    // intialize the av frame
    pl->frame = av_frame_alloc();

    return 0;
}

void fm_player_close(fm_player_t *pl)
{
    fm_player_stop(pl);

    ao_close(pl->dev);

    pthread_mutex_destroy(&pl->mutex_status);
    pthread_cond_destroy(&pl->cond_play);

    // free the ffmpeg stuff
    /*av_free(pl->io_context);*/
    /*av_free(pl->iobuf);*/
    av_frame_free(&pl->frame);
    av_free_packet(&pl->avpkt);
    if (pl->interweave_buf)
        av_freep(pl->interweave_buf);
    if (pl->swr_context)
        av_free(&pl->swr_context);
    if (pl->swr_buf)
        av_freep(*pl->swr_buf);
}

static SwrFormat get_dest_sample_fmt_from_sample_fmt(struct SwrContext **swr_ctx, SwrFormat src)
{
    int i, ret;
    SwrFormat dest = src;
    dest.sample_fmt = AV_SAMPLE_FMT_NONE;
    struct sample_fmt_entry {
        enum AVSampleFormat src_sample_fmt;
        enum AVSampleFormat dest_sample_fmt;
        int bits;
    } sample_fmt_entries[] = {
        { AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_S16, 16 },
    };

    for (i = 0; i < FF_ARRAY_ELEMS(sample_fmt_entries); i++) {
        if (sample_fmt_entries[i].src_sample_fmt == src.sample_fmt) {
            dest.sample_fmt = sample_fmt_entries[i].dest_sample_fmt;
            dest.bits = sample_fmt_entries[i].bits;
            break;
        }
    }

    if (dest.sample_fmt == AV_SAMPLE_FMT_NONE)
        return dest;

    /* create resampler context */
    if (!*swr_ctx) {
        *swr_ctx = swr_alloc();
        if (!*swr_ctx) {
            fprintf(stderr, "Could not allocate resampler context\n");
            return dest;
        }
    }

    av_opt_set_int(*swr_ctx, "in_channel_layout", src.channel_layout, 0);
    av_opt_set_int(*swr_ctx, "in_sample_rate",       src.sample_rate, 0);
    av_opt_set_sample_fmt(*swr_ctx, "in_sample_fmt", src.sample_fmt, 0);

    av_opt_set_int(*swr_ctx, "out_channel_layout",    dest.channel_layout, 0);
    av_opt_set_int(*swr_ctx, "out_sample_rate",       dest.sample_rate, 0);
    av_opt_set_sample_fmt(*swr_ctx, "out_sample_fmt", dest.sample_fmt, 0);

    /* initialize the resampling context */
    if ((ret = swr_init(*swr_ctx)) < 0) {
        fprintf(stderr, "Failed to initialize the resampling context\n");
        return dest;
    }
    return dest;
}

int fm_player_set_song(fm_player_t *pl, fm_song_t *song)
{
    fm_player_stop(pl);

    if (!song) {
        printf("No song to play\n");
        return -1;
    }

    // set the relevant properties
    pl->info.duration = 0;
    pl->info.length = song->length;

    printf("Blocking on waiting for downloader being assigned\n");
    while (song->filepath[0] == '\0'); 
    printf("filepath is %s\n", song->filepath);
    // open the file
    /*pl->file = fopen(song->filepath, "r");*/
    /*if (!pl->file) {*/
        /*fprintf(stderr, "Failed opening the audio file %s\n", song->filepath);*/
        /*return -1;*/
    /*}                     */

    pl->format_context = NULL;
    /*// intialize the ffmpeg avio context etc.*/
    /*pl->format_context = avformat_alloc_context();*/
    /*if (!pl->format_context) {*/
        /*fprintf(stderr, "Failed allocating format context\n");*/
        /*exit(-1);*/
    /*}*/
    /*pl->format_context->pb = pl->io_context;*/

    // wait for the file to grow at least to a considerable size
    printf("Blocking on waiting for the file to have some initial size\n");
    struct stat st;
    do {
        stat(song->filepath, &st);
        sleep(3);
    } while (st.st_size < HEADERBUF_SIZE);

    int ret;
    /*int length = 0, size = sizeof(pl->headerbuf);*/
    /*printf("Attempting to read the header\n");*/
    /*// read the data into the headerbuf once*/
    /*while (length < size) {*/
        /*ret = fread(pl->headerbuf + length, 1, size - length, pl->file);*/
        /*if (!ferror(pl->file) && ret > 0) {*/
            /*printf("%d byte(s) of header read.\n", ret);*/
            /*length += ret;*/
        /*}*/
    /*} */
    /*AVProbeData avpd;*/
    /*avpd.buf = pl->headerbuf;*/
    /*avpd.buf_size = sizeof(pl->headerbuf) - 16;*/
    /*avpd.filename = "";*/
    /*printf("Attempting to probe the input\n");*/
    /*pl->input_format = av_probe_input_format(&avpd, 1);*/
    /*if (!pl->input_format) {*/
        /*fprintf(stderr, "Failed to initiate input format\n");*/
        /*return -1;*/
    /*}*/

    printf("Attempting to open the input\n");
    /*AVDictionary *options = NULL;*/
    /*format_context->probesize = sz;*/
    if (avformat_open_input(&pl->format_context, song->filepath, NULL, NULL) < 0) {
    /*if (avformat_open_input(&pl->format_context, "", pl->input_format, &options) < 0) {*/
        printf("Failure on opening the input stream\n");
        return -1;
    } else 
        printf("Opened format input\n");

    // seek to the beginning of the file to avoid problem
    /*fseek(pl->file, 0, SEEK_SET);*/
    printf("Attempting to find the stream info\n");
    ret = avformat_find_stream_info(pl->format_context, NULL);
    if (ret < 0) {
        fprintf(stderr, "Cannot find stream info\n");
        return -1;
    }

    printf("Attempting to find the best stream\n");
    printf("Number of streams available: %d\n", pl->format_context->nb_streams);
    pl->audio_stream_idx = av_find_best_stream(pl->format_context, AVMEDIA_TYPE_AUDIO, -1, -1, &pl->codec, 0);

    if (pl->audio_stream_idx < 0) {
        fprintf(stderr,"Couldn't find stream information\n");
        return -1;
    }
    // Get a pointer to the codec context for the audio stream
    AVStream *stream = pl->format_context->streams[pl->audio_stream_idx];
    // set the timebase
    pl->info.time_base = stream->time_base;
    pl->context = stream->codec;
    av_opt_set_int(pl->context, "refcounted_frames", 1, 0);

    if (avcodec_open2(pl->context, pl->codec, NULL) < 0) {
        fprintf(stderr, "Could not open codec\n");
        return -1;
    }
    
    // adjusting for resampling
    pl->src_swr_format.sample_fmt = pl->context->sample_fmt;
    pl->src_swr_format.channel_layout = pl->context->channel_layout;
    pl->src_swr_format.sample_rate = pl->context->sample_rate;
    pl->src_swr_format.bits = 0;
    pl->dest_swr_format = pl->src_swr_format;
    switch(pl->context->sample_fmt) {
        case AV_SAMPLE_FMT_U8:         ///< unsigned 8 bits 
        case AV_SAMPLE_FMT_S16:       ///< signed 16 bits  
        case AV_SAMPLE_FMT_S32:       ///< signed 32 bits  
        case AV_SAMPLE_FMT_U8P:       ///< unsigned 8 bits, planar 
        case AV_SAMPLE_FMT_S16P:     ///< signed 16 bits, planar  
        case AV_SAMPLE_FMT_S32P:     ///< signed 32 bits, planar  
            pl->resampled = 0;
            break;
        // for float and double
        default: 
            pl->resampled = 1;
            printf("Resampling needs to be done\n"); 
            pl->dest_swr_format = get_dest_sample_fmt_from_sample_fmt(&pl->swr_context, pl->src_swr_format);
            if (!pl->swr_context) {
                fprintf(stderr, "Cannot resample the data in the specified stream. Sample fmt is %d\n", pl->src_swr_format.sample_fmt);
                return -1;
            }
            break;
    }

    return 0;
}

void fm_player_set_ack(fm_player_t *pl, pthread_t tid, int sig)
{
    pl->tid_ack = tid;
    pl->sig_ack = sig;
}

int fm_player_pos(fm_player_t *pl)
{
    return pl->info.duration * pl->info.time_base.num / pl->info.time_base.den;
}

int fm_player_length(fm_player_t *pl)
{
    return pl->info.length;
}

void fm_player_play(fm_player_t *pl)
{
    printf("Player play\n");
    if (pl->status == FM_PLAYER_STOP) {
        pl->status = FM_PLAYER_PLAY;
        /*pl->info.file_size = 0;*/
        /*pl->info.samples = 0;*/
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
    if (pl->status != FM_PLAYER_STOP) {
        printf("Player stop\n");
        pthread_mutex_lock(&pl->mutex_status);
        pl->status = FM_PLAYER_STOP;
        pthread_mutex_unlock(&pl->mutex_status);
        pthread_cond_signal(&pl->cond_play);

        pthread_join(pl->tid_play, NULL);

        // close the file
        /*if (pl->file) {*/
            /*fclose(pl->file);*/
            /*pl->file = NULL;*/
        /*}*/
        if (pl->context)
            avcodec_close(pl->context);
        if (pl->format_context)
            avformat_close_input(&pl->format_context);
    }
}

void fm_player_init()
{
    ao_initialize();
    avcodec_register_all();
    av_register_all();
}

void fm_player_exit()
{
    ao_shutdown();
}
