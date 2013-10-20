#include "playlist.h"
#include "util.h"

#include <json/json.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>


static char *douban_music_website = "http://music.douban.com";
static int local_channel_fetch_number = 25;

static struct json_object* fm_playlist_send_report(fm_playlist_t *pl, char act, int get_response, int fallback, void *data);

typedef struct {
    char data[8192];
    size_t length;
} buffer_t;

static void fm_buffer_init(buffer_t *buf) 
{
    buf->length = 0;
    memset(buf->data, 0, sizeof(buf->data));
}

static void fm_song_free(fm_song_t *song)
{
    free(song->title);
    free(song->artist);
    free(song->album);
    free(song->cover);
    free(song->url);
    free(song->audio);
    free(song);
}

static fm_song_t* fm_song_douban_parse_json(struct json_object *obj)
{
    fm_song_t *song = (fm_song_t*) malloc(sizeof(fm_song_t));
    song->title = strdup(json_object_get_string(json_object_object_get(obj, "title")));
    song->artist = strdup(json_object_get_string(json_object_object_get(obj, "artist")));
    song->kbps = strdup(json_object_get_string(json_object_object_get(obj, "kbps")));
    song->album = strdup(json_object_get_string(json_object_object_get(obj, "albumtitle")));
    song->pubdate = json_object_get_int(json_object_object_get(obj, "public_time"));
    song->cover = strdup(json_object_get_string(json_object_object_get(obj, "picture")));
    // the url form is NOT helpful. We should automatically add the music.douban.com prefix
    char *url = strdup(json_object_get_string(json_object_object_get(obj, "album")));
    if (strncmp(url, douban_music_website, strlen(douban_music_website)) != 0) {
        char *nu = (char *) malloc(128);
        sprintf(nu, "%s%s", douban_music_website, url);
        url = nu;
    }
    song->url = url;
    song->audio = strdup(json_object_get_string(json_object_object_get(obj, "url")));
    song->sid = json_object_get_int(json_object_object_get(obj, "sid"));
    song->like = json_object_get_int(json_object_object_get(obj, "like"));
    if (song->sid == 0) {
        fm_song_free(song);
        song = NULL;
    }
    return song;
}

// return the result object if possible
static json_object *fm_jing_parse_json_result(json_object *obj)
{
    // here we are only going to parse the fetch_pls (conceivably)
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
    song->audio = strdup(json_object_get_string(json_object_object_get(obj, "mid")));
    // save the mid first
    /*printf("Jing song parser: song url is %s\n", song->audio);*/
    song->title = strdup(json_object_get_string(json_object_object_get(obj, "n")));
    /*printf("Jing song parser: title is %s\n", song->title);*/
    song->artist = strdup(json_object_get_string(json_object_object_get(obj, "atn")));
    /*printf("Jing song parser: artist is %s\n", song->artist);*/
    song->kbps = "192";
    song->album = strdup(json_object_get_string(json_object_object_get(obj, "an")));
    /*printf("Jing song parser: album is %s\n", song->album);*/
    // jing does not provide release date either
    song->pubdate = 0;
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
        char *c_url = (char *) malloc(128);
        sprintf(c_url, "http://img.jing.fm/album/AM/%s", buf);
        song->cover = c_url;
        /*printf("Jing song parser: song cover image is %s\n", song->cover);*/
    } else {
        song->cover = "";
    }
    // jing does not provide a url link...
    song->url = "";
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
    song->next = pl->playlist;
    pl->playlist = song;
}

static fm_song_t* fm_playlist_pop_front(fm_playlist_t *pl)
{
    fm_song_t *ret = NULL;
    if (pl->playlist) {
        ret = pl->playlist;
        pl->playlist = pl->playlist->next;
    }
    return ret;
}

static void fm_playlist_clear(fm_playlist_t *pl)
{
    fm_song_t *s = pl->playlist;
    fm_song_t *next;
    while (s) {
        next = s->next;
        fm_song_free(s);
        s = next;
    }
    pl->playlist = NULL;
}

void fm_playlist_init(fm_playlist_t *pl, fm_playlist_config_t *config)
{
    pl->history = NULL;
    pl->playlist = NULL;

    pl->douban_api = "http://www.douban.com/j/app/radio/people";
    pl->douban_channel_api = "http://www.douban.com/j/app/radio/channels";
    pl->app_name = "radio_desktop_win";
    pl->version = "100";

    pl->jing_api = "http://jing.fm/api/v1";

    pl->config = *config;

    pl->curl = curl_easy_init();
}

void fm_playlist_cleanup(fm_playlist_t *pl)
{
    curl_easy_cleanup(pl->curl);
    fm_playlist_hisotry_clear(pl);
    fm_playlist_clear(pl);
}

static size_t append_to_buffer(char *ptr, size_t size, size_t nmemb, void *userp)
{
    printf("Entered buffer appending block\n");
    buffer_t *buffer = (buffer_t*) userp;
    size_t bytes = size * nmemb;
    memcpy(buffer->data + buffer->length, ptr, bytes);
    buffer->length += bytes;
    return bytes;
}

static size_t drop_buffer(char *ptr, size_t size, size_t nmemb, void *userp)
{
    size_t bytes = size * nmemb;
    return bytes;
}

static void fm_playlist_douban_parse_json(fm_playlist_t *pl, struct json_object *obj)
{
    int i;
    int ret = json_object_get_int(json_object_object_get(obj, "r"));
    if (ret != 0) {
        fprintf(stderr, "API error: %s\n", json_object_get_string(json_object_object_get(obj, "err")));
        exit(ret);
    }
    printf("Douban playlist parsing new API response\n");
    array_list *songs = json_object_get_array(json_object_object_get(obj, "song"));
    printf("parsed song\n");
    for (i = songs->length - 1; i >= 0; i--) {
        struct json_object *o = (struct json_object*) array_list_get_idx(songs, i);
        fm_song_t *song = fm_song_douban_parse_json(o);
        if (song) {
            fm_playlist_push_front(pl, song);
            printf("Playlist add song %d to the top\n", song->sid);
        }
    }
    json_object_put(obj); 
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

static void fm_playlist_curl_buffer_config(CURL *curl, buffer_t *buf, int get_response)
{
    // initialize buffer
    if (get_response) {
        fm_buffer_init(buf);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, append_to_buffer);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, buf);
    } else {
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, drop_buffer);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, NULL);
    }
}

static void fm_playlist_curl_jing_config(fm_playlist_t *pl, CURL *curl, char act, buffer_t *curl_buffer, int get_response, struct curl_slist *slist, void *data)
{
    // initialize the buffer
    fm_playlist_curl_buffer_config(curl, curl_buffer, get_response);
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
            sprintf(buf, "uid=%d&tid=%d", pl->config.jing_uid, pl->playlist->sid);
            break;
        case 'u':
            format = "%s/music/post_hate_song";
            sprintf(buf, "uid=%d&tid=%d", pl->config.jing_uid, pl->playlist->sid);
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

static void fm_playlist_jing_parse_json(fm_playlist_t *pl, struct json_object *obj)
{
    // here we are only going to parse the fetch_pls (conceivably)
    if ((obj = fm_jing_parse_json_result(obj)))  {
        printf("Jing playlist parsing new API response\n");
        array_list *song_objs = json_object_get_array(json_object_object_get(obj, "items"));
        printf("parsed song\n");
        // we should make use of a multihandle that accelerates the pulling process
        int len = song_objs->length;
        if (len > 0) {
            int i, j;
            buffer_t buf[len][2];
            fm_song_t *songs[len];
            CURL *handles[len][2];

            CURL *multi_handle = curl_multi_init();

            struct curl_slist *slist;
            fm_playlist_curl_jing_headers_init(pl, &slist);
            for (i = len -1; i >= 0; i--) {
                struct json_object *o = (struct json_object*) array_list_get_idx(song_objs, i);
                fm_song_t *song = fm_song_jing_parse_json(pl, o);
                songs[i] = song;
                if (song) {
                    // initialize the CURL easy handle
                    handles[i][0] = curl_easy_init();
                    fm_playlist_curl_jing_config(pl, handles[i][0], 'm', &buf[i][0], 1, slist, song->audio);
                    curl_multi_add_handle(multi_handle, handles[i][0]);

                    handles[i][1] = curl_easy_init();
                    fm_playlist_curl_jing_config(pl, handles[i][1], 'i', &buf[i][1], 1, slist, &song->sid);
                    curl_multi_add_handle(multi_handle, handles[i][1]);
                }
            }
            int still_running;
            /* we start some action by calling perform right away */
            while ( curl_multi_perform(multi_handle, &still_running) == CURLM_CALL_MULTI_PERFORM );

            do {
                struct timeval timeout;
                int rc; /* select() return code */

                fd_set fdread;
                fd_set fdwrite;
                fd_set fdexcep;
                int maxfd = -1;

                long curl_timeo = -1;

                FD_ZERO(&fdread);
                FD_ZERO(&fdwrite);
                FD_ZERO(&fdexcep);

                /* set a suitable timeout to play around with */
                timeout.tv_sec = 1;
                timeout.tv_usec = 0;

                curl_multi_timeout(multi_handle, &curl_timeo);
                if(curl_timeo >= 0) {
                    timeout.tv_sec = curl_timeo / 1000;
                    if(timeout.tv_sec > 1)
                        timeout.tv_sec = 1;
                    else
                        timeout.tv_usec = (curl_timeo % 1000) * 1000;
                }

                curl_multi_fdset(multi_handle, &fdread, &fdwrite, &fdexcep, &maxfd);

                rc = select(maxfd+1, &fdread, &fdwrite, &fdexcep, &timeout);

                switch(rc) {
                    case -1:
                        /* select error */
                        break;
                    case 0: /* timeout */
                    default: /* action */
                        curl_multi_perform(multi_handle, &still_running);
                        break;
                }
            } while(still_running);

            printf("Multi curl perform finished\n");
            /* See how the transfers went */
            CURLMsg *msg;
            int msgs_left;
            while ((msg = curl_multi_info_read(multi_handle, &msgs_left))) {
                if (msg->msg == CURLMSG_DONE) {

                    /* Find out which handle this message is about */
                    for (i=0; i<len; i++) {
                        // first check for the first field
                        if (msg->easy_handle == handles[i][0]) {
                            // music url retrieval complete; parse the result
                            json_object *o = json_tokener_parse(buf[i][0].data);
                            if (o && (o = fm_jing_parse_json_result(o))) {
                                songs[i]->audio = strdup(json_object_get_string(o));
                                printf("Successfully retrieved the audio url %s for song title: %s\n", songs[i]->audio, songs[i]->title);
                            }
                        } else if (msg->easy_handle == handles[i][1]) {
                            // track info retrieval complete
                            json_object *o = json_tokener_parse(buf[i][1].data);
                            if (o && (o = fm_jing_parse_json_result(o))) {
                                songs[i]->like = *json_object_get_string(json_object_object_get(o, "lvd")) == 'l' ? 1 : 0;
                                printf("Song title %s is liked? %d\n", songs[i]->title, songs[i]->like);
                            }
                        }
                    }
                }
            }

            curl_multi_cleanup(multi_handle);

            /* Free the CURL handles and add the songs*/
            for (i=0; i<len; i++) {
                for (j=0; j<2; j++)
                    curl_easy_cleanup(handles[i][j]);
                if (songs[i]) {
                    printf("Playlist add song %d to the top\n", songs[i]->sid);
                    fm_playlist_push_front(pl, songs[i]);
                }
            }
            curl_slist_free_all(slist);
        }
    } else
        printf("Jing song parser: failed to retrieve the like information about the song\n");

    json_object_put(obj); 
}

static void fm_playlist_parse_json(fm_playlist_t *pl, struct json_object *obj)
{
    if (obj) {
        switch (pl->config.mode) {
            case plLocal: case plDouban:
                fm_playlist_douban_parse_json(pl, obj);
                break;
            case plJing:
                fm_playlist_jing_parse_json(pl, obj);
                break;
        }
    } else {
        printf("Unable to parse object\n");
        // if we fail to parse json we should probably clear the playlist to indicate that we are NOT good
        fm_playlist_clear(pl);
    }       
}

static void fm_playlist_local_dump_report(fm_playlist_t *pl, buffer_t *curl_buffer)
{
    char cmd[1024], bmd[128];
    sprintf(cmd,
            "IFS=$'\\n';"
            "echo -n '{\"r\":0,\"song\":[';"
            "eyeD3f -s, -e'\"' '{{"
            "\"title\":\"{title}\","
            "\"artist\":\"{artist}\","
            "\"album\":\"{internet_radio_url}\","
            "\"albumtitle\":\"{album}\","
            "\"public_time\":\"{release_date}\","
            "\"picture\":\"\","
            "\"kbps\":\"{kbps}\","
            "\"like\":1,"
            "\"sid\":%d,"
            "\"url\":\"file:{path}\""
            "}}' $(find $'%s' -type f -name '*.mp3' | shuf | head -n '%d');"
            "echo -n ']}';"
            , 1, escapesh(bmd, pl->config.music_dir), local_channel_fetch_number);
    printf("Cmd is: %s\n", cmd);
    FILE *f = popen(cmd, "r");
    if (f) {
        int size = sizeof(curl_buffer->data), len;
        memset(curl_buffer->data, 0, size);
        while ((len = strlen(curl_buffer->data)) <= size - 1  && fgets(curl_buffer->data + len, size - len, f) != NULL);
        /*printf("Local music information retrieved: %s\n", curl_buffer.data);*/
        pclose(f);
    } else
        printf("Failed to open the pipe for the command to find the local music\n");
}

static void fm_playlist_curl_douban_config(fm_playlist_t *pl, CURL *curl, char act, buffer_t *curl_buffer, int get_response)
{
    // initialize the buffer
    fm_playlist_curl_buffer_config(curl, curl_buffer, get_response);
    char url[1024];
    printf("Playlist send report: %d:%c\n", pl->config.douban_uid, act);
    sprintf(url, "%s?app_name=%s&version=%s&user_id=%d&expire=%d&token=%s&channel=%s&sid=%d&type=%c&h=%s&kbps=%s",
            pl->douban_api, pl->app_name, pl->version, pl->config.douban_uid, pl->config.expire, pl->config.douban_token, pl->config.channel,
            pl->playlist ? pl->playlist->sid : 0, act, fm_playlist_history_str(pl), pl->config.kbps);
    printf("Playlist request: %s\n", url);

    curl_easy_setopt(curl, CURLOPT_HTTPGET, 1);
    curl_easy_setopt(curl, CURLOPT_URL, url);
}

static struct json_object* fm_playlist_send_report(fm_playlist_t *pl, char act, int get_response, int fallback, void *data)
{
    static buffer_t curl_buffer;

    json_object* obj = NULL;
    // try to parse the channel to make sense
    switch (pl->config.mode) {
        case plLocal:
            fm_playlist_local_dump_report(pl, &curl_buffer);
            break;
        case plDouban:
            fm_playlist_curl_douban_config(pl, pl->curl, act, &curl_buffer, get_response);
            curl_easy_perform(pl->curl);
            break;
        case plJing: {
            struct curl_slist *slist;
            fm_playlist_curl_jing_headers_init(pl, &slist);
            fm_playlist_curl_jing_config(pl, pl->curl, act, &curl_buffer, get_response, slist, data);
            curl_easy_perform(pl->curl);
            curl_slist_free_all(slist);
            printf("output from Jing is %s\n", curl_buffer.data);
            break;
        }
    }
    // lastly retrieve the reponse if necessary
    if (get_response) {
        obj = json_tokener_parse(curl_buffer.data);
        if (!obj) {
            switch (pl->config.mode) {
                case plLocal:
                    printf("Local station failed to generate recognizable json data. Buffer = %s\n", curl_buffer.data);
                    break;
                case plDouban: case plJing:
                    printf("Token parser couldn't parse the buffer; Maybe network is down. Buffer = %s\n", curl_buffer.data);
                    if (fallback) {
                        printf("Switched to local mode instead.\n");
                        strcpy(pl->config.channel, local_channel);
                        pl->config.mode = plLocal;
                        obj = fm_playlist_send_report(pl, act, get_response, 0, data);
                    }
                    break;
            }
        }
    }
    return obj;
}

fm_song_t* fm_playlist_current(fm_playlist_t *pl)
{
    if (pl->playlist == NULL)
        return fm_playlist_next(pl);
    else
        return pl->playlist;
}

static void fm_playlist_next_on_link(fm_playlist_t *pl)
{
    fm_song_free(fm_playlist_pop_front(pl));
    if (pl->playlist == NULL) {
        printf("Playlist empty, request more\n");
        fm_playlist_parse_json(pl, fm_playlist_send_report(pl, 'p', 1, 1, NULL));
    }
}

/** Remove the local song */
static void fm_playlist_local_remove(fm_playlist_t *pl)
{
    // we should try to remove the current song from the cache
    char *url = pl->playlist->audio;
    if (strncmp(url, "file:/", 6) == 0) {
        url = pl->playlist->audio + 5;
        unlink(url);
        // attempt to remove the directory if its empty as well
        rmdir(dirname(url));
    } else 
        printf("Inconsistent audio address format: %s\n", pl->playlist->audio);
}

fm_song_t* fm_playlist_next(fm_playlist_t *pl)
{
    printf("Playlist next song\n");
    if (pl->playlist) {
        switch (pl->config.mode) {
            case plLocal:case plJing:
                break;
            case plDouban: 
                fm_playlist_history_add(pl, pl->playlist, 'e');
                fm_playlist_send_report(pl, 'e', 0, 1, NULL);
                break;
        }
        fm_playlist_next_on_link(pl);
    }
    else {
        printf("Playlist init empty, request new\n");
        fm_playlist_parse_json(pl, fm_playlist_send_report(pl, 'n', 1, 1, NULL));
    }
    return pl->playlist;
}


fm_song_t* fm_playlist_skip(fm_playlist_t *pl, int force_refresh)
{
    printf("Playlist skip song\n");
    if (pl->playlist) {
        switch (pl->config.mode) {
            case plLocal:case plJing:
                if (force_refresh) {
                    fm_playlist_clear(pl);
                    fm_playlist_parse_json(pl, fm_playlist_send_report(pl, 's', 1, 1, NULL));
                } else 
                    fm_playlist_next_on_link(pl);
                break;
            case plDouban: 
                fm_playlist_history_add(pl, pl->playlist, 's');
                fm_playlist_clear(pl);
                fm_playlist_parse_json(pl, fm_playlist_send_report(pl, 's', 1, 1, NULL));
                break;
        }
        return pl->playlist;
    }
    else
        return fm_playlist_next(pl);
}

fm_song_t* fm_playlist_ban(fm_playlist_t *pl)
{
    printf("Playlist ban song\n");
    if (pl->playlist) {
        switch (pl->config.mode) {
            case plLocal:
                fm_playlist_local_remove(pl);
                // same action as n
                fm_playlist_next_on_link(pl);
                break;
            case plDouban: 
                fm_playlist_history_add(pl, pl->playlist, 'b');
                fm_playlist_clear(pl);
                fm_playlist_parse_json(pl, fm_playlist_send_report(pl, 'b', 1, 1, NULL));
                break;
            case plJing:
                fm_playlist_parse_json(pl, fm_playlist_send_report(pl, 'b', 0, 1, NULL));
                fm_playlist_next_on_link(pl);
                break;
        }
        return pl->playlist;
    }
    else
        return fm_playlist_next(pl);
}

void fm_playlist_rate(fm_playlist_t *pl)
{
    printf("Playlist rate song\n");
    if (pl->playlist) {
        pl->playlist->like = 1;
        switch (pl->config.mode) {
            case plLocal:
                break;
            case plDouban: case plJing:
                fm_playlist_send_report(pl, 'r', 0, 0, NULL);
                break;
        }
    }
}

void fm_playlist_unrate(fm_playlist_t *pl)
{
    printf("Playlist unrate song\n");
    if (pl->playlist) {
        pl->playlist->like = 0;
        switch (pl->config.mode) {
            case plLocal:
                fm_playlist_local_remove(pl);
                // same action as n
                fm_playlist_next_on_link(pl);
                break;
            case plDouban: case plJing:
                fm_playlist_send_report(pl, 'u', 0, 0, NULL);
                break;
        }
    }
}
