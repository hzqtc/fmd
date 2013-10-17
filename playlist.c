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

typedef struct {
    char data[8192];
    size_t length;
} buffer_t;

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

static fm_song_t* fm_song_parse_json(struct json_object *obj)
{
    fm_song_t *song = (fm_song_t*) malloc(sizeof(fm_song_t));
    song->title = strdup(json_object_get_string(json_object_object_get(obj, "title")));
    printf("parsed title\n");
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

static void fm_playlist_parse_json(fm_playlist_t *pl, struct json_object *obj)
{
    if (obj) {
        int i;
        int ret = json_object_get_int(json_object_object_get(obj, "r"));
        if (ret != 0) {
            fprintf(stderr, "API error: %s\n", json_object_get_string(json_object_object_get(obj, "err")));
            exit(ret);
        }
        printf("Playlist parsing new API response\n");
        array_list *songs = json_object_get_array(json_object_object_get(obj, "song"));
        printf("parsed song\n");
        for (i = songs->length - 1; i >= 0; i--) {
            struct json_object *o = (struct json_object*) array_list_get_idx(songs, i);
            fm_song_t *song = fm_song_parse_json(o);
            if (song) {
                fm_playlist_push_front(pl, song);
                printf("Playlist add song %d to the top\n", song->sid);
            }
        }
        json_object_put(obj);
    } else {
        printf("Unable to parse object\n");
        // if we fail to parse json we should probably clear the playlist to indicate that we are NOT good
        fm_playlist_clear(pl);
    }
}

void fm_playlist_init(fm_playlist_t *pl, fm_playlist_config_t *config)
{
    pl->history = NULL;
    pl->playlist = NULL;

    pl->api = "http://www.douban.com/j/app/radio/people";
    pl->channel_api = "http://www.douban.com/j/app/radio/channels";
    pl->app_name = "radio_desktop_win";
    pl->version = "100";

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

static struct json_object* fm_playlist_send_long_report(fm_playlist_t *pl, int sid, char act)
{
    static buffer_t curl_buffer;
    json_object* obj = NULL;
    // hack here; use the current channel to determine if the local channel is on board
    if (pl->config.channel == local_channel) {
        switch (act) {
            case 'b': case 's':
                printf("Ban / Skip stats feedback not supported for local station to optimize performance\n");
        }
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
                "\"sid\":\"%d\","
                "\"url\":\"file:{path}\""
                "}}' $(find $'%s' -type f -name '*.mp3' | shuf | head -n '%d');"
                "echo -n ']}';"
                , pl->config.uid, escapesh(bmd, pl->config.music_dir), local_channel_fetch_number);
        printf("Cmd is: %s\n", cmd);
        FILE *f = popen(cmd, "r");
        if (f) {
            int size = sizeof(curl_buffer.data), len;
            memset(curl_buffer.data, 0, size);
            while ((len = strlen(curl_buffer.data)) <= size - 1  && fgets(curl_buffer.data + len, size - len, f) != NULL);
            printf("Local music information retrieved: %s\n", curl_buffer.data);
            pclose(f);

            obj = json_tokener_parse(curl_buffer.data);
            if (!obj) 
                printf("Local station failed to generate recognizable json data. Buffer = %s\n", curl_buffer.data);
        } else
            printf("Failed to open the pipe for the command to find the local music\n");

    } else {
        char url[1024];
        printf("Playlist send long report: %d:%c\n", sid, act);
        sprintf(url, "%s?app_name=%s&version=%s&user_id=%d&expire=%d&token=%s&channel=%d&sid=%d&type=%c&h=%s&kbps=%s",
                pl->api, pl->app_name, pl->version, pl->config.uid, pl->config.expire, pl->config.token, pl->config.channel,
                sid, act, fm_playlist_history_str(pl), pl->config.kbps);
        printf("Playlist request: %s\n", url);

        memset(curl_buffer.data, 0, sizeof(curl_buffer.data));
        curl_buffer.length = 0;

        curl_easy_setopt(pl->curl, CURLOPT_URL, url);
        curl_easy_setopt(pl->curl, CURLOPT_WRITEFUNCTION, append_to_buffer);
        curl_easy_setopt(pl->curl, CURLOPT_WRITEDATA, &curl_buffer);
        curl_easy_perform(pl->curl);

        obj = json_tokener_parse(curl_buffer.data);
        if (!obj) {
            printf("Token parser couldn't parse the buffer; Maybe network is down. Switched to local mode instead. Buffer = %s\n", curl_buffer.data);
            pl->config.channel = local_channel;
            obj = fm_playlist_send_long_report(pl, sid, act);
        }
    }
    return obj;
}

static void fm_playlist_send_short_report(fm_playlist_t *pl, int sid, char act)
{
    char url[1024];
    printf("Playlist send short report: %d:%c\n", sid, act);
    sprintf(url, "%s?app_name=%s&version=%s&user_id=%d&expire=%d&token=%s&channel=%d&sid=%d&type=%c&kbps=%s",
            pl->api, pl->app_name, pl->version, pl->config.uid, pl->config.expire, pl->config.token, pl->config.channel,
            sid, act, pl->config.kbps);
    printf("Playlist request: %s\n", url);

    curl_easy_setopt(pl->curl, CURLOPT_URL, url);
    curl_easy_setopt(pl->curl, CURLOPT_WRITEFUNCTION, drop_buffer);
    curl_easy_setopt(pl->curl, CURLOPT_WRITEDATA, NULL);
    curl_easy_perform(pl->curl);
}

fm_song_t* fm_playlist_current(fm_playlist_t *pl)
{
    if (pl->playlist == NULL)
        return fm_playlist_next(pl);
    else
        return pl->playlist;
}

static void fm_playlist_next_on_link(fm_playlist_t *pl, int sid)
{
    fm_song_free(fm_playlist_pop_front(pl));
    if (pl->playlist == NULL) {
        printf("Playlist empty, request more\n");
        fm_playlist_parse_json(pl, fm_playlist_send_long_report(pl, sid, 'p'));
    }
}

/** Remove the local song */
static void fm_playlist_remove(fm_playlist_t *pl, int sid)
{
    // we should try to remove the current song from the cache
    char *url = pl->playlist->audio;
    if (strncmp(url, "file:/", 6) == 0 && strcmp(url + strlen(url) - 4, ".mp3") == 0) {
        url = pl->playlist->audio + 5;
        unlink(url);
        // attempt to remove the directory if its empty as well
        rmdir(dirname(url));
    } else 
        printf("Inconsistent audio address format: %s\n", pl->playlist->audio);
    // same action as n
    fm_playlist_next_on_link(pl, sid);
}

fm_song_t* fm_playlist_next(fm_playlist_t *pl)
{
    int sid = 0;
    printf("Playlist next song\n");
    if (pl->playlist) {
        fm_playlist_history_add(pl, pl->playlist, 'e');
        if (pl->config.channel != local_channel)
            fm_playlist_send_short_report(pl, sid, 'e');
        fm_playlist_next_on_link(pl, sid);
    }
    else {
        printf("Playlist init empty, request new\n");
        fm_playlist_parse_json(pl, fm_playlist_send_long_report(pl, sid, 'n'));
    }
    return pl->playlist;
}


fm_song_t* fm_playlist_skip(fm_playlist_t *pl, int force_refresh)
{
    int sid = 0;
    printf("Playlist skip song\n");
    if (pl->playlist) {
        sid = pl->playlist->sid;
        fm_playlist_history_add(pl, pl->playlist, 's');
        if (!force_refresh && pl->config.channel == local_channel) {
            // same action as n
            fm_playlist_next_on_link(pl, sid);
        } else {
            fm_playlist_clear(pl);
            fm_playlist_parse_json(pl, fm_playlist_send_long_report(pl, sid, 's'));
        }
        return pl->playlist;
    }
    else
        return fm_playlist_next(pl);
}

fm_song_t* fm_playlist_ban(fm_playlist_t *pl)
{
    int sid = 0;
    printf("Playlist ban song\n");
    if (pl->playlist) {
        sid = pl->playlist->sid;
        fm_playlist_history_add(pl, pl->playlist, 'b');
        if (pl->config.channel == local_channel) {
            fm_playlist_remove(pl, sid);
        } else {
            fm_playlist_clear(pl);
            fm_playlist_parse_json(pl, fm_playlist_send_long_report(pl, sid, 'b'));
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
        if (pl->config.channel != local_channel)
            fm_playlist_send_short_report(pl, pl->playlist->sid, 'r');
    }
}

void fm_playlist_unrate(fm_playlist_t *pl)
{
    printf("Playlist unrate song\n");
    if (pl->playlist) {
        pl->playlist->like = 0;
        if (pl->config.channel -= local_channel)
            fm_playlist_remove(pl, pl->playlist->sid);
        else
            fm_playlist_send_short_report(pl, pl->playlist->sid, 'u');
    }
}
