#include "playlist.h"
#include "util.h"

#include <json/json.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <pthread.h>

// Adding the new downloader interface
// All newly retrieved songs are downloaded IMMEDIATELY

#define N_LOCAL_CHANNEL_FETCH 25

static char *douban_music_website = "http://music.douban.com";

static void song_downloader_stop(fm_playlist_t *pl, downloader_t *dl)
{
    stack_downloader_stop(pl->stack, dl);
    // clean up the state
    fm_song_t *prev = (fm_song_t *)dl->data;
    if (prev) {
        prev->downloader = NULL;
        dl->data = NULL;
    }
}

static void fm_song_free(fm_playlist_t *pl, fm_song_t *song)
{
    // first notify the downloader to stop
    if (song->downloader) {
        song_downloader_stop(pl, song->downloader);
    }

    // now check: if we should cache this song / if we should delete the tmp buffer associated with this song
    // we can run a length check to check if that matches the size given in the song
    if (song->like) {
        if (pl->mode == plDouban || pl->mode == plJing) {
            printf("Attemping to cache the song for path %s\n", song->filepath);
            // first move the file to a secure location to avoid it being truncated later
            char cmd[2048], btp[128], bart[128], btitle[128], balb[128], bmd[128], bcover[128], burl[128]; 
            sprintf(cmd, 
                    "src=$'%s';"
                    "[ \"$(eyeD3f '{len}' \"$src\")\" != '%d' ] && exit 0;"
                    "artist=$'%s'; title=$'%s'; album=$'%s'; date='%d';"
                    "[[ \"$date\" =~ [0-9]{4} ]] && datearg=\"--release-year $date\" || datearg=;"
                    "dest=$'%s'\"/${artist//\\//|}/${title//\\//|}.%s\";"
                    "[ -f \"$dest\" ] && exit 0;"
                    "mkdir -p \"$(dirname \"$dest\")\";"
                    "mv -f \"$src\" \"$dest\";" 
                    "tmpimg='/tmp/fmdcover.jpg'; cover=$'%s';"
                    "(curl --connect-timeout 15 -m 60 -o \"$tmpimg\" \"$cover\";"
                    "([ -f \"$tmpimg\" ] && identify \"$tmpimg\") && cover=\"$tmpimg\" || cover=\"${cover//:/\\:}\";"
                    "page_url=$'%s';"
                    "[ -n \"$page_url\" ] && page_url_opt=\"--url-frame WORS:${page_url//:/\\:}\";"
                    "eyeD3 --artist \"$artist\" --album \"$album\" --title \"$title\" $datearg --add-image \"$cover:FRONT_COVER\" $page_url_opt \"$dest\";"
                    "rm -f \"$tmpimg\") &", 
                    escapesh(btp, song->filepath), 
                    song->length,
                    escapesh(bart, song->artist), 
                    escapesh(btitle, song->title), 
                    escapesh(balb, song->album), 
                    song->pubdate,
                    escapesh(bmd, pl->config.music_dir),
                    pl->mode == plDouban ? "mp3" : "m4a",
                    escapesh(bcover, song->cover),
                    escapesh(burl, song->url));
            printf("Move and tag command: %s\n", cmd);
            system(cmd);
        }
    } else {
        // remove the song
        unlink(song->filepath);
        rmdir(dirname(song->filepath));
    }

    free(song);
}

static fm_song_t* fm_song_douban_parse_json(fm_playlist_t *pl, struct json_object *obj)
{
    fm_song_t *song = (fm_song_t*) malloc(sizeof(fm_song_t));
    strcpy(song->title, json_object_get_string(json_object_object_get(obj, "title")));
    strcpy(song->artist, json_object_get_string(json_object_object_get(obj, "artist")));
    strcpy(song->kbps, json_object_get_string(json_object_object_get(obj, "kbps")));
    strcpy(song->album, json_object_get_string(json_object_object_get(obj, "albumtitle")));
    song->pubdate = json_object_get_int(json_object_object_get(obj, "public_time"));
    strcpy(song->cover, json_object_get_string(json_object_object_get(obj, "picture")));
    // the url form is NOT helpful. We should automatically add the music.douban.com prefix
    // add the music website suffix (we know that this parser is only used for douban network
    sprintf(song->url, "%s%s", douban_music_website, json_object_get_string(json_object_object_get(obj, "album")));
    strcpy(song->audio, json_object_get_string(json_object_object_get(obj, "url")));
    song->sid = json_object_get_int(json_object_object_get(obj, "sid"));
    song->like = json_object_get_int(json_object_object_get(obj, "like"));
    song->length = json_object_get_int(json_object_object_get(obj, "length"));
    if (song->sid == 0) {
        fm_song_free(pl, song);
        song = NULL;
    }
    song->filepath[0] = '\0';
    return song;
}

// return the result object if possible
static json_object *fm_jing_parse_json_result(json_object *obj)
{
    // here we are only going to parse the fetch_pls (conceivably)
    if (!obj) {
        fprintf(stderr, "Attempting to parse null object\n");
        return NULL;
    }
    if (json_object_get_boolean(json_object_object_get(obj, "success")) == FALSE ) {
        fprintf(stderr, "API error: %s\n", json_object_get_string(json_object_object_get(obj, "msg")));
        return NULL;
    }
    return json_object_object_get(obj, "result");
}

static fm_song_t* fm_song_jing_parse_json(fm_playlist_t *pl, struct json_object *obj)
{
    fm_song_t *song = (fm_song_t*) malloc(sizeof(fm_song_t));
    // for the audio link we have to perform a retrieve
    song->sid = json_object_get_int(json_object_object_get(obj, "tid")); 
    /*printf("Jing song parser: sid is %d\n", song->sid);*/
    strcpy(song->audio, json_object_get_string(json_object_object_get(obj, "mid")));
    // save the mid first
    /*printf("Jing song parser: song url is %s\n", song->audio);*/
    strcpy(song->title, json_object_get_string(json_object_object_get(obj, "n")));
    /*printf("Jing song parser: title is %s\n", song->title);*/
    strcpy(song->artist, json_object_get_string(json_object_object_get(obj, "atn")));
    /*printf("Jing song parser: artist is %s\n", song->artist);*/
    strcpy(song->kbps, "192");
    strcpy(song->album, json_object_get_string(json_object_object_get(obj, "an")));
    /*printf("Jing song parser: album is %s\n", song->album);*/
    // jing does not provide release date either
    song->pubdate = 0;
    song->length = json_object_get_int(json_object_object_get(obj, "d"));
    const char *cover = json_object_get_string(json_object_object_get(obj, "fid"));
    if (strlen(cover) >= 13) {
        char buf[50] = "";
        // first copy 4 characters
        strncat(buf, cover, 4);
        strcat(buf, "/");
        strncat(buf, cover+4, 4);
        strcat(buf, "/");
        strncat(buf, cover+8, 2);
        strcat(buf, "/");
        strncat(buf, cover+10, 2);
        strcat(buf, "/AM");
        strcat(buf, cover);
        sprintf(song->cover, "http://img.jing.fm/album/AM/%s", buf);
        /*printf("Jing song parser: song cover image is %s\n", song->cover);*/
    } else {
        song->cover[0] = '\0';
    }
    // jing does not provide a url link...
    song->url[0] = '\0';
    song->filepath[0] = '\0';
    // for like we need to retrieve again...
    song->like = 0;
    return song;
}

static void fm_playlist_history_add(fm_playlist_t *pl, fm_song_t *song, char state)
{
    const static int max_hist = 10;
    int len = 0;
    fm_history_t *h = pl->history;
    fm_history_t *last = NULL;
    fm_history_t *penult = NULL;
    while (h) {
        len++;
        penult = last;
        last = h;
        h = h->next;
    }

    if (len < max_hist) {   // append new history item
        h = (fm_history_t*) malloc(sizeof(fm_history_t));
        h->sid = song->sid;
        h->state = state;
        h->next = pl->history;
        pl->history = h;
    }
    else {                  // reuse the last history item and move it to the head
        last->sid = song->sid;
        last->state = state;
        penult->next = NULL;// make penult to new last item
        last->next = pl->history;
        pl->history = last;
    }
}

static void fm_playlist_hisotry_clear(fm_playlist_t *pl)
{
    fm_history_t *h = pl->history;
    fm_history_t *next;
    while (h) {
        next = h->next;
        free(h);
        h = next;
    }
}

static const char* fm_playlist_history_str(fm_playlist_t *pl)
{
    static char buffer[1024];
    memset(buffer, 0, sizeof(buffer));
    char* p = buffer;
    fm_history_t *hist = pl->history;
    while (hist) {
        sprintf(p, "|%d:%c", hist->sid, hist->state);
        p += strlen(p);
        hist = hist->next;
    }
    return buffer;
}

static void fm_playlist_push_front(fm_playlist_t *pl, fm_song_t *song)
{
    if (song) {
        song->next = pl->current;
        pl->current = song;
        printf("Playlist add song %d to the top\n", song->sid);
    }
}

static fm_song_t* fm_playlist_pop_front(fm_playlist_t *pl)
{
    fm_song_t *ret = NULL;
    if (pl->current) {
        ret = pl->current;
        pl->current = pl->current->next;
    }
    return ret;
}

static void song_downloaders_stop(fm_playlist_t *pl, int new_stop)
{
    pthread_mutex_lock(&pl->mutex_song_download_stop);
    pl->song_download_stop = new_stop;
    pthread_mutex_unlock(&pl->mutex_song_download_stop);
}

static void fm_playlist_clear(fm_playlist_t *pl)
{
    // stop all the downloaders first
    song_downloaders_stop(pl, 1);
    fm_song_t *s = pl->current;
    fm_song_t *next;
    while (s) {
        next = s->next;
        fm_song_free(pl, s);
        s = next;
    }
    pl->current = NULL;
}

void fm_playlist_init(fm_playlist_t *pl, fm_playlist_config_t *config, void (*fm_player_stop)())
{
    pl->history = NULL;
    pl->current = NULL;

    pl->douban_api = "http://www.douban.com/j/app/radio/people";
    pl->douban_channel_api = "http://www.douban.com/j/app/radio/channels";
    pl->app_name = "radio_desktop_win";
    pl->version = "100";

    pl->jing_api = "http://jing.fm/api/v1";

    pl->config = *config;

    // wire up the player
    pl->fm_player_stop = fm_player_stop;
    // set up the downloader stack
    pl->stack = stack_init();
    // set up the downloader stuff
    pl->song_download_stop = 0;
    pl->song_downloaders[0] = NULL;
    pl->tid_download = 0;
    pthread_mutex_init(&pl->mutex_song_download_stop, NULL);
    pthread_cond_init(&pl->cond_song_download_restart, NULL);
}

void fm_playlist_cleanup(fm_playlist_t *pl)
{
    stack_free(pl->stack);
    fm_playlist_hisotry_clear(pl);
    fm_playlist_clear(pl);
}

static int fm_playlist_douban_parse_json(fm_playlist_t *pl, struct json_object *obj)
{
    if (!obj)
        return -1;
    int i;
    int ret = json_object_get_int(json_object_object_get(obj, "r"));
    if (ret != 0) {
        fprintf(stderr, "API error: %s\n", json_object_get_string(json_object_object_get(obj, "err")));
        return -1;
    }
    printf("Douban playlist parsing new API response\n");
    array_list *songs = json_object_get_array(json_object_object_get(obj, "song"));
    printf("parsed song\n");
    for (i = songs->length - 1; i >= 0; i--) {
        struct json_object *o = (struct json_object*) array_list_get_idx(songs, i);
        fm_song_t *song = fm_song_douban_parse_json(pl, o);
        fm_playlist_push_front(pl, song);
    }
    json_object_put(obj); 
    return 0;
}

static void fm_playlist_curl_jing_headers_init(fm_playlist_t *pl, struct curl_slist **slist)
{
    char buf[128];
    *slist = NULL;
    sprintf(buf, "Jing-A-Token-Header:%s", pl->config.jing_atoken);
    *slist = curl_slist_append(*slist, buf);
    sprintf(buf, "Jing-R-Token-Header:%s", pl->config.jing_rtoken);
    *slist = curl_slist_append(*slist, buf);
}

static void fm_playlist_curl_jing_config(fm_playlist_t *pl, CURL *curl, char act, struct curl_slist *slist, void *data)
{
    // initialize the buffer
    char buf[128], *format;
    switch(act) {
        case 'm':
            // get the music url links 
            format = "%s/media/song/surl";
            // leveraging the data field; make sure the data and act are set accordingly to avoid problems
            sprintf(buf, "mid=%s", (char *)data);
            break;
        case 'n': case 'p': case 's': default:
            format = "%s/search/jing/fetch_pls";
            // escape the query
            char *arg = curl_easy_escape(curl, pl->config.channel, 0);
            sprintf(buf, "u=%d&q=%s&ps=5&tid=0&mt=&ss=true", pl->config.jing_uid, arg);
            curl_free(arg);
            break;
        case 'b': 
            format = "%s/music/post_love_song";
            sprintf(buf, "uid=%d&tid=%d", pl->config.jing_uid, pl->current->sid);
            break;
        case 'u':
            format = "%s/music/post_hate_song";
            sprintf(buf, "uid=%d&tid=%d", pl->config.jing_uid, pl->current->sid);
            break;
        case 'i':
            format = "%s/music/fetch_track_infos";
            // leveraging the data field; make sure the data and act are set accordingly to avoid problems
            sprintf(buf, "uid=%d&tid=%d", pl->config.jing_uid, *(int *)data);
            break;
    }

    /*curl_easy_setopt(curl, CURLOPT_VERBOSE, 1);*/
    curl_easy_setopt(curl, CURLOPT_COPYPOSTFIELDS, buf);

    sprintf(buf,format, pl->jing_api);
    curl_easy_setopt(curl, CURLOPT_URL, buf);
    // set up the headers; should call init headers before this function
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist);
}

static int fm_playlist_jing_parse_json(fm_playlist_t *pl, struct json_object *obj)
{
    // here we are only going to parse the fetch_pls (conceivably)
    if ((obj = fm_jing_parse_json_result(obj)))  {
        printf("Jing playlist parsing new API response\n");
        array_list *song_objs = json_object_get_array(json_object_object_get(obj, "items"));
        printf("parsed song\n");
        // we should make use of a multihandle that accelerates the pulling process
        int len = song_objs->length;
        if (len > 0) {
            int i;
            fm_song_t *songs[len];
            // get the len number of downloaders
            downloader_t *dls[len][2];
            stack_get_idle_downloaders(pl->stack, *dls, len * 2, dMem);

            struct curl_slist *slist;
            fm_playlist_curl_jing_headers_init(pl, &slist);
            for (i = len -1; i >= 0; i--) {
                struct json_object *o = (struct json_object*) array_list_get_idx(song_objs, i);
                fm_song_t *song = fm_song_jing_parse_json(pl, o);
                songs[i] = song;
                if (song) {
                    fm_playlist_curl_jing_config(pl, dls[i][0]->curl, 'm', slist, song->audio);
                    fm_playlist_curl_jing_config(pl, dls[i][1]->curl, 'i', slist, &song->sid);
                }
            }
            stack_perform_until_all_done(pl->stack, *dls, len * 2);
            printf("Multi curl perform finished\n");
            /* See how the transfers went */
            for (i=0; i<len; i++) {
                json_object *o = json_tokener_parse(dls[i][0]->content.mbuf->data);
                if ((o = fm_jing_parse_json_result(o))) {
                    strcpy(songs[i]->audio, json_object_get_string(o));
                    printf("Successfully retrieved the audio url %s for song title: %s\n", songs[i]->audio, songs[i]->title);
                }
                if ((o = fm_jing_parse_json_result(json_tokener_parse(dls[i][1]->content.mbuf->data)))) {
                    songs[i]->like = *json_object_get_string(json_object_object_get(o, "lvd")) == 'l' ? 1 : 0;
                    printf("Song title %s is liked? %d\n", songs[i]->title, songs[i]->like);
                }
                fm_playlist_push_front(pl, songs[i]);
            }
            curl_slist_free_all(slist);
        }
    } else {
        printf("Jing song parser: failed to retrieve the like information about the song\n");
        return -1;
    }

    json_object_put(obj); 
    return 0;
}

static int fm_playlist_local_dump_parse_report(fm_playlist_t *pl)
{
    char buf[512];
    sprintf(buf,
            "IFS=$'\\n';"
            "eyeD3f '{title}\n{artist}\n{internet_radio_url}\n{album}\n{release_date}\n{picture}\n{kbps}\n{path}\n{len}' "
            "$(find $'%s' -type f -name '*.mp3' | shuf | head -n '%d')"
            , pl->config.music_dir, N_LOCAL_CHANNEL_FETCH);
    printf("Local channel refilling command is: %s\n", buf);
    // the field reference counter
    FILE *fp = popen(buf, "r");
    if (!fp) {
        printf("Failed to open the pipe for the command to find the local music\n");
        return -1;
    }
    fm_song_t *song;
    int fn = 0, fl = 9;
    char ch = '\n', *lastf = NULL;
    int len = 0, size = 0;
    while (1) {
        if (fn == 0) {
            // push the last song
            fm_playlist_push_front(pl, song);
            song = (fm_song_t*) malloc(sizeof(fm_song_t));
            song->sid = song->like = 1;
        }
        // get the next character
        if (ch == EOF)
            break;
        if (ch == '\n') {
            if (lastf)
                // append the 0 at the end
                lastf[len] = '\0';
            if (fn == 4)
                song->pubdate = atoi(lastf);
            else if (fn == 8)
                song->length = atoi(lastf);
            printf("Obtained song field: %s for song %p\n", lastf, song);
            fn = (fn+1) % fl;
            // set up the last field
            switch(fn) {
                case 0: lastf = song->title; size = sizeof(song->title); break;
                case 1: lastf = song->artist; size = sizeof(song->artist); break;
                case 2: lastf = song->url; size = sizeof(song->url); break;
                case 3: lastf = song->album; size = sizeof(song->album); break;
                case 4: lastf = buf; size = sizeof(buf); break;
                case 5: lastf = song->cover; size = sizeof(song->cover); break;
                case 6: lastf = song->kbps; size = sizeof(song->kbps); break;
                case 7: lastf = song->filepath; size = sizeof(song->filepath); break;
                case 8: lastf = buf; size = sizeof(buf); break;
            }
            len = 0;
        } else {
            if (len < size - 1)
                lastf[len++] = ch;
            else {
                fprintf(stderr, "Buffer overflow for song fields. Last recorded field = %s\n", lastf);
                return -1;
            }
        }
        ch = fgetc(fp);
    }
    pclose(fp);
    return 0;
}

static void fm_playlist_curl_douban_config(fm_playlist_t *pl, CURL *curl, char act)
{
    char url[1024];
    printf("Playlist send report: %d:%c\n", pl->config.douban_uid, act);
    sprintf(url, "%s?app_name=%s&version=%s&user_id=%d&expire=%d&token=%s&channel=%s&sid=%d&type=%c&h=%s&kbps=%s",
            pl->douban_api, pl->app_name, pl->version, pl->config.douban_uid, pl->config.expire, pl->config.douban_token, pl->config.channel,
            pl->current ? pl->current->sid : 0, act, fm_playlist_history_str(pl), pl->config.kbps);
    printf("Playlist request: %s\n", url);

    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);
    curl_easy_setopt(curl, CURLOPT_URL, url);
}

static int song_downloader_init(fm_playlist_t *pl, downloader_t *dl) {
    song_downloader_stop(pl, dl);
    if (pl->current_download) {
        fdownloader_config(dl);
        // set the url
        curl_easy_setopt(dl->curl, CURLOPT_URL, pl->current_download->audio);
        strcpy(pl->current_download->filepath, dl->content.fbuf->filepath);
        pl->current_download->downloader = dl;
        dl->data = pl->current_download;
        pl->current_download = pl->current_download->next;
        return 0;
    }
    return -1;
}

static downloader_t *process_download(downloader_stack_t *stack, downloader_t **start, int length, void *data)
{
    int i;
    fm_playlist_t *pl = (fm_playlist_t *)data;
    // we check for the existing state 

    pthread_mutex_lock(&pl->mutex_song_download_stop);
    if (pl->song_download_stop) {
        pl->song_download_stop = 0;
        pthread_cond_wait(&pl->cond_song_download_restart, &pl->mutex_song_download_stop);
    }
    pthread_mutex_unlock(&pl->mutex_song_download_stop);
    // perform any download if we can
    for (i=0; i<length; i++) {
        if (start[i]->idle) {
            // reinitialize the downloaders and configure them
            if (song_downloader_init(pl, start[i]) != 0) {
                // no more download
                int all_idle = 1;
                for (i=0; i<length; i++) {
                    // check that all are idle now
                    if (!start[i]->idle) {
                        all_idle = 0;
                        break;
                    }
                }
                if (all_idle) {
                    // simply return
                    return start[0];
                }
            } else {
                // readd the downloaders to the stack
                stack_downloader_init(stack, start[i]);
            }
        }
    }
    return NULL;
}

static void* download_thread(void *data)
{
    // first get the downloaders
    fm_playlist_t *pl = (fm_playlist_t *)data;
    stack_perform_until_condition_met(pl->stack, pl->song_downloaders, sizeof(pl->song_downloaders), pl, process_download);
    // unlock all the downloaders
    int i;
    for (i=0; i<N_SONG_DOWNLOADERS; i++) {
        stack_downloader_unlock(pl->stack, pl->song_downloaders[i]);
    }
    return data;
}

static void restart_song_downloaders(fm_playlist_t *pl)
{
    // set the flag to indicate the 
    pl->current_download = pl->current;
    // signal the condition
    pthread_cond_signal(&pl->cond_song_download_restart);
    // we must ensure that the downloaders are present
    if (!pl->song_downloaders[0]) {
        int i, len = sizeof(pl->song_downloaders);
        // initialize and lock the downloaders; doing this in the main thread to guarantee no race condition
        stack_get_idle_downloaders(pl->stack, pl->song_downloaders, len, dFile);
        for (i=0; i<len; i++) {
            stack_downloader_lock(pl->stack, pl->song_downloaders[i]);
        }
    }
    if (!pl->tid_download) {
        pl->tid_download = pthread_create(&pl->tid_download, NULL, download_thread, pl);
    }
}

static int fm_playlist_send_report(fm_playlist_t *pl, char act, int parse_result, int fallback)
{
    switch (pl->mode) {
        case plLocal: {
            return fm_playlist_local_dump_parse_report(pl);
        }
        case plDouban: {
            // we should first request the downloader; obtain the curl handle and then 
            downloader_t *dl = stack_get_idle_downloader(pl->stack, parse_result ? dMem : dDrop);
            fm_playlist_curl_douban_config(pl, dl->curl, act);
            stack_perform_until_done(pl->stack, dl);
            if (!parse_result)
                return 0;
            if (fm_playlist_douban_parse_json(pl, json_tokener_parse(dl->content.mbuf->data)) == 0) {
                // before restarting the song downloader we should really make sure that the player is stopped
                // so that it does not lead to a trailing file handler on the file
                pl->fm_player_stop();
                restart_song_downloaders(pl);
                return 0;
            }
            break;
        }
        case plJing: {
            struct curl_slist *slist;
            fm_playlist_curl_jing_headers_init(pl, &slist);
            downloader_t *dl = stack_get_idle_downloader(pl->stack, parse_result ? dMem : dDrop);
            // the jing config shouldn't involve any data for the playlist. Otherwise there's some error
            fm_playlist_curl_jing_config(pl, dl->curl, act, slist, NULL);
            stack_perform_until_done(pl->stack, dl);
            curl_slist_free_all(slist);
            if (!parse_result)
                return 0;
            if (fm_playlist_jing_parse_json(pl, json_tokener_parse(dl->content.mbuf->data)) == 0) {
                pl->fm_player_stop();
                restart_song_downloaders(pl);
                return 0;
            }
            break;
        }
    }
    fprintf(stderr, "Some error occurred during the process; Maybe network is down. \n");
    if (fallback) {
        fprintf(stderr, "Trying again with local channel.\n");
        strcpy(pl->config.channel, local_channel);
        pl->mode = plLocal;
        return fm_playlist_send_report(pl, act, parse_result, 0);
    }
    return -1;
}

fm_song_t* fm_playlist_current(fm_playlist_t *pl)
{
    if (!pl->current)
        return fm_playlist_next(pl);
    else
        return pl->current;
}

static void fm_playlist_next_on_link(fm_playlist_t *pl)
{
    fm_song_free(pl, fm_playlist_pop_front(pl));
    // TODO: probably starts caching when there are a few songs left?
    if (!pl->current) {
        printf("Playlist empty, request more\n");
        fm_playlist_send_report(pl, 'p', 1, 1);
    }
}

fm_song_t* fm_playlist_next(fm_playlist_t *pl)
{
    printf("Playlist next song\n");
    if (pl->current) {
        switch (pl->mode) {
            case plLocal:case plJing:
                break;
            case plDouban: 
                fm_playlist_history_add(pl, pl->current, 'e');
                fm_playlist_send_report(pl, 'e', 0, 1);
                break;
        }
        fm_playlist_next_on_link(pl);
    }
    else {
        printf("Playlist init empty, request new\n");
        fm_playlist_send_report(pl, 'n', 1, 1);
    }
    return pl->current;
}


fm_song_t* fm_playlist_skip(fm_playlist_t *pl, int force_refresh)
{
    printf("Playlist skip song\n");
    if (pl->current) {
        switch (pl->mode) {
            case plLocal:case plJing:
                if (force_refresh) {
                    fm_playlist_clear(pl);
                    fm_playlist_send_report(pl, 's', 1, 1);
                } else 
                    fm_playlist_next_on_link(pl);
                break;
            case plDouban: 
                fm_playlist_history_add(pl, pl->current, 's');
                fm_playlist_clear(pl);
                fm_playlist_send_report(pl, 's', 1, 1);
                break;
        }
        return pl->current;
    }
    else
        return fm_playlist_next(pl);
}

fm_song_t* fm_playlist_ban(fm_playlist_t *pl)
{
    printf("Playlist ban song\n");
    if (pl->current) {
        switch (pl->mode) {
            case plLocal:
                pl->current->like = 0;
                // same action as n
                fm_playlist_next_on_link(pl);
                break;
            case plDouban: 
                fm_playlist_history_add(pl, pl->current, 'b');
                fm_playlist_clear(pl);
                fm_playlist_send_report(pl, 'b', 1, 1);
                break;
            case plJing:
                fm_playlist_send_report(pl, 'b', 0, 1);
                fm_playlist_next_on_link(pl);
                break;
        }
        return pl->current;
    }
    else
        return fm_playlist_next(pl);
}

void fm_playlist_rate(fm_playlist_t *pl)
{
    printf("Playlist rate song\n");
    if (pl->current) {
        pl->current->like = 1;
        switch (pl->mode) {
            case plLocal:
                break;
            case plDouban: case plJing:
                fm_playlist_send_report(pl, 'r', 0, 0);
                break;
        }
    }
}

void fm_playlist_unrate(fm_playlist_t *pl)
{
    printf("Playlist unrate song\n");
    if (pl->current) {
        pl->current->like = 0;
        switch (pl->mode) {
            case plLocal:
                pl->current->like = 0;
                // same action as n
                fm_playlist_next_on_link(pl);
                break;
            case plDouban: case plJing:
                fm_playlist_send_report(pl, 'u', 0, 0);
                break;
        }
    }
}
