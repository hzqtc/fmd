#include "playlist.h"

#include <json/json.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    song->artist = strdup(json_object_get_string(json_object_object_get(obj, "artist")));
    song->album = strdup(json_object_get_string(json_object_object_get(obj, "albumtitle")));
    song->pubdate = json_object_get_int(json_object_object_get(obj, "public_time"));
    song->cover = strdup(json_object_get_string(json_object_object_get(obj, "picture")));
    song->url = strdup(json_object_get_string(json_object_object_get(obj, "album")));
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
    
    if (len < max_hist) {     // append new history item
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
    int i;
    array_list *songs = json_object_get_array(json_object_object_get(obj, "song"));
    for (i = songs->length - 1; i >= 0; i--) {
        struct json_object *o = (struct json_object*) array_list_get_idx(songs, i);
        fm_song_t *song = fm_song_parse_json(o);
        if (song)
            fm_playlist_push_front(pl, song);
    }
    json_object_put(obj);
}

void fm_playlist_init(fm_playlist_t *pl)
{
    pl->history = NULL;
    pl->playlist = NULL;

    pl->api = "http://www.douban.com/j/app/radio/people";
    pl->channel_api = "http://www.douban.com/j/app/radio/channels";
    pl->app_name = "radio_desktop_win";
    pl->version = "100";
    pl->channel = 1;
    pl->uid = 0;
    memset(pl->uname, 0, sizeof(pl->uname));
    memset(pl->token, 0, sizeof(pl->token));
    pl->expire = 0;
    
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
    char url[1024];
    sprintf(url, "%s?app_name=%s&version=%s&user_id=%d&expire=%d&token=%s&channel=%d&sid=%d&type=%c&h=%s",
            pl->api, pl->app_name, pl->version, pl->uid, pl->expire, pl->token, pl->channel,
            sid, act, fm_playlist_history_str(pl));

    memset(curl_buffer.data, 0, sizeof(curl_buffer.data));
    curl_buffer.length = 0;

    curl_easy_setopt(pl->curl, CURLOPT_URL, url);
    curl_easy_setopt(pl->curl, CURLOPT_WRITEFUNCTION, append_to_buffer);
    curl_easy_setopt(pl->curl, CURLOPT_WRITEDATA, &curl_buffer);
    curl_easy_perform(pl->curl);

    return json_tokener_parse(curl_buffer.data);
}

static void fm_playlist_send_short_report(fm_playlist_t *pl, int sid, char act)
{
    char url[1024];
    sprintf(url, "%s?app_name=%s&version=%s&user_id=%d&expire=%d&token=%s&channel=%d&sid=%d&type=%c",
            pl->api, pl->app_name, pl->version, pl->uid, pl->expire, pl->token, pl->channel,
            sid, act);

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

fm_song_t* fm_playlist_next(fm_playlist_t *pl)
{
    int sid = 0;
    if (pl->playlist) {
        sid = pl->playlist->sid;
        fm_playlist_history_add(pl, pl->playlist, 'e');
        fm_playlist_send_short_report(pl, sid, 'e');

        fm_song_free(fm_playlist_pop_front(pl));
        if (pl->playlist == NULL)
            fm_playlist_parse_json(pl, fm_playlist_send_long_report(pl, sid, 'p'));
    }
    else
        fm_playlist_parse_json(pl, fm_playlist_send_long_report(pl, sid, 'n'));
    return pl->playlist;
}

fm_song_t* fm_playlist_skip(fm_playlist_t *pl)
{
    int sid = 0;
    if (pl->playlist) {
        sid = pl->playlist->sid;
        fm_playlist_history_add(pl, pl->playlist, 's');
        fm_playlist_clear(pl);
        fm_playlist_parse_json(pl, fm_playlist_send_long_report(pl, sid, 's'));
        return pl->playlist;
    }
    else
        return fm_playlist_next(pl);
}

fm_song_t* fm_playlist_ban(fm_playlist_t *pl)
{
    int sid = 0;
    if (pl->playlist) {
        sid = pl->playlist->sid;
        fm_playlist_history_add(pl, pl->playlist, 'b');
        fm_playlist_clear(pl);
        fm_playlist_parse_json(pl, fm_playlist_send_long_report(pl, sid, 'b'));
        return pl->playlist;
    }
    else
        return fm_playlist_next(pl);
}

void fm_playlist_rate(fm_playlist_t *pl)
{
    if (pl->playlist) {
        pl->playlist->like = 1;
        fm_playlist_send_short_report(pl, pl->playlist->sid, 'r');
    }
}

void fm_playlist_unrate(fm_playlist_t *pl)
{
    if (pl->playlist) {
        pl->playlist->like = 0;
        fm_playlist_send_short_report(pl, pl->playlist->sid, 'u');
    }
}
