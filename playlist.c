#include "playlist.h"
#include "util.h"

#include <json/json.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>
#include <pthread.h>
#include <sys/stat.h>
#include <errno.h>

static char *douban_music_website = "http://music.douban.com";

static void song_downloader_stop(fm_playlist_t *pl, downloader_t *dl)
{
    stack_downloader_stop(pl->stack, dl);
    // clean up the state
    pthread_mutex_lock(&pl->mutex_song_downloader);
    fm_song_t *song = (fm_song_t *)dl->data;
    if (song) {
        song->downloader = NULL;
        dl->data = NULL;
    }
    pthread_mutex_unlock(&pl->mutex_song_downloader);
    pthread_cond_signal(&dl->cond_new_content);
}

void fm_playlist_update_mode(fm_playlist_t *pl)
{
    char *address;
    strtol(pl->config.channel, &address, 10);
    if (*address == '\0') {
        // this is valid number
        if (strcmp(pl->config.channel, LOCAL_CHANNEL) == 0)
            pl->mode = plLocal;
        else
            pl->mode = plDouban;
    } else
        pl->mode = plJing;
}

static void replace(char *str, char to_rep, char rep)
{
    while (*str != '\0') {
        if (*str == to_rep) {
            *str = rep;
        }
        str++;
    }
}

static int get_file_path(char *buf, char *directory, char *artist, char *title, char *ext)
{
    // if the artist or the title is unknown, we should report an error
    if (title[0] == '\0' || artist[0] == '\0')
        return -1;
    replace(artist, '/', '|');
    replace(title, '/', '|');
    sprintf(buf, "%s/%s/%s.%s", directory, artist, title, ext);
    /*printf("The obtained file path is %s\n", buf);*/
    return 0;
}

static void fm_song_free(fm_playlist_t *pl, fm_song_t *song)
{
    // first notify the downloader to stop
    pthread_mutex_lock(&pl->mutex_song_downloader);
    if (song->downloader) {
        stack_downloader_stop(pl->stack, song->downloader);
        song->downloader->data = NULL;
        song->downloader = NULL;
    }
    pthread_mutex_unlock(&pl->mutex_song_downloader);

    int to_remove = 1;
    // now check: if we should cache this song / if we should delete the tmp buffer associated with this song
    // we can run a length check to check if that matches the size given in the song
    if (song->like) {
        // run a simple check on whether there are any file present in the file system
        if (song->filepath[0] != '\0') {
            struct stat sts;
            // must make sure that the tmp file exists and the dest file does not exist
            if (stat(song->filepath, &sts) == 0) {
                char lp[128];
                if (get_file_path(lp, pl->config.music_dir, song->artist, song->title, song->ext) == 0) {
                    if (strcmp(song->filepath, lp) == 0)
                        to_remove = 0;
                    else if (validate(&song->validator, song->filepath) && stat(lp, &sts) == -1 && errno == ENOENT) {
                        to_remove = 0;
                        printf("Attemping to cache the song for path %s\n", lp);
                        // first move the file to a secure location to avoid it being truncated later
                        char cmd[2048], btp[128], bart[128], btitle[128], balb[128], blp[128], bcover[128], burl[128]; 
                        sprintf(cmd, 
                                "src=$'%s'; dest=\"$src.%s\";"
                                "tmpimg=\"$src.jpg\";"
                                "mv \"$src\" \"$dest\";"
                                "src=\"$dest\";"
                                /*"l=\"$(mutagen -f '{len}' \"$src\")\";"*/
                                /*"ld=$((l - %d));"*/
                                /*"(((ld < -2)) || ((ld > 2))) && rm -f \"$src\" && exit 0;"*/
                                "artist=$'%s'; title=$'%s'; album=$'%s'; date='%d';"
                                "[[ \"$date\" =~ [0-9]{4} ]] && datearg=\"-Y $date\" || datearg=;"
                                "dest=$'%s';"
                                "mkdir -p \"$(dirname \"$dest\")\";"
                                "mv -f \"$src\" \"$dest\";" 
                                "cover=$'%s';"
                                "(curl --connect-timeout 15 -m 60 -o \"$tmpimg\" \"$cover\";"
                                "([ -f \"$tmpimg\" ] && identify \"$tmpimg\") && coverarg=\"-c $tmpimg\" || coverarg=;"
                                "page_url=$'%s';"
                                "mutagen -a \"$artist\" -A \"$album\" -t \"$title\" -r \"$page_url\" $datearg $coverarg \"$dest\";"
                                "rm -f \"$tmpimg\") &", 
                                escapesh(btp, song->filepath), 
                                song->ext,
                                /*song->length,*/
                                escapesh(bart, song->artist), 
                                escapesh(btitle, song->title), 
                                escapesh(balb, song->album), 
                                song->pubdate,
                                escapesh(blp, lp),
                                escapesh(bcover, song->cover),
                                escapesh(burl, song->url));
                        printf("Move and tag command: %s\n", cmd);
                        system(cmd);
                    }                                                                   
                }
            }
        }
    } 
    if (to_remove) {
        // remove the song
        unlink(song->filepath);
        rmdir(dirname(song->filepath));
    }

    free(song);
}

static int valid_song_url(char *url)
{
    return strncmp(url, "http://", 7) == 0;
}

static fm_song_t *song_init(fm_playlist_t *pl)
{
    fm_song_t *song = (fm_song_t*) malloc(sizeof(fm_song_t));
    song->title[0] = song->artist[0] = song->kbps[0] = song->album[0] = song->cover[0] = song->url[0] = song->audio[0] = song->ext[0] = song->filepath[0] = '\0';
    song->pubdate = song->sid = song->like = song->length = 0;
    song->next = NULL;
    song->downloader = NULL;
    validator_init(&song->validator);
    song->mutex_downloader = &pl->mutex_song_downloader;
    return song;
}

static fm_song_t *fm_song_douban_parse_json(fm_playlist_t *pl, struct json_object *obj)
{
    fm_song_t *song = song_init(pl);
    song->sid = json_object_get_int(json_object_object_get(obj, "sid"));
    strcpy(song->title, json_object_get_string(json_object_object_get(obj, "title")));
    strcpy(song->artist, json_object_get_string(json_object_object_get(obj, "artist")));
    strcpy(song->album, json_object_get_string(json_object_object_get(obj, "albumtitle")));
    song->pubdate = json_object_get_int(json_object_object_get(obj, "public_time"));
    strcpy(song->cover, json_object_get_string(json_object_object_get(obj, "picture")));
    sprintf(song->url, "%s%s", douban_music_website, json_object_get_string(json_object_object_get(obj, "album")));
    song->like = json_object_get_int(json_object_object_get(obj, "like"));
    strcpy(song->ext, "mp3");
    strcpy(song->kbps, json_object_get_string(json_object_object_get(obj, "kbps")));
    song->length = json_object_get_int(json_object_object_get(obj, "length"));
    validator_sha256_init(&song->validator, json_object_get_string(json_object_object_get(obj, "sha256")));
    // check if we can substitute the audio field with a local path
    if (get_file_path(song->filepath, pl->config.music_dir, song->artist, song->title, song->ext) == 0 && validate(&song->validator, song->filepath)) {
        printf("Detected local audio file for song %s/%s. Using the file directly instead of downloading.\n", song->artist, song->title);
    } else {
        song->filepath[0] = '\0';
        strcpy(song->audio, json_object_get_string(json_object_object_get(obj, "url")));
        if (!valid_song_url(song->audio)) {
            fm_song_free(pl, song);
            song= NULL;
        }
    }
    
    if (song->sid == 0) {
        fm_song_free(pl, song);
        song = NULL;
    }
    return song;
}

// return the result object if possible
static json_object *fm_jing_parse_json_result(json_object *obj)
{
    // here we are only going to parse the fetch_pls (conceivably)
    if (!obj) {
        printf("Attempting to parse null object\n");
        return NULL;
    }
    if (json_object_get_boolean(json_object_object_get(obj, "success")) == FALSE ) {
        printf("API error: %s\n", json_object_get_string(json_object_object_get(obj, "msg")));
        return NULL;
    }
    return json_object_object_get(obj, "result");
}

static fm_song_t* fm_song_jing_parse_json(fm_playlist_t *pl, struct json_object *obj)
{
    fm_song_t *song = song_init(pl);
    // for the audio link we have to perform a retrieve
    song->sid = json_object_get_int(json_object_object_get(obj, "tid")); 
    strcpy(song->title, json_object_get_string(json_object_object_get(obj, "n")));
    strcpy(song->artist, json_object_get_string(json_object_object_get(obj, "atn")));
    strcpy(song->album, json_object_get_string(json_object_object_get(obj, "an")));
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
    } 
    strcpy(song->ext, "m4a");
    strcpy(song->kbps, "192");
    song->length = json_object_get_int(json_object_object_get(obj, "d"));
    validator_filesize_init(&song->validator, json_object_get_int(json_object_object_get(obj, "fs")));
    // check if we can substitute the audio field with a local path
    if (get_file_path(song->filepath, pl->config.music_dir, song->artist, song->title, song->ext) == 0 && validate(&song->validator, song->filepath)) {
        printf("Detected local audio file for song %s/%s. Using the file directly instead of downloading.\n", song->artist, song->title);
    } else {
        song->filepath[0] = '\0';
        strcpy(song->audio, json_object_get_string(json_object_object_get(obj, "mid")));
        if (song->audio[0] == '\0') {
            fm_song_free(pl, song);
            song = NULL;
        }
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

static void fm_playlist_push_front(fm_song_t **base, fm_song_t *song)
{
    if (song) {
        song->next = *base;
        *base = song;
        printf("Playlist add song %d before %p\n", song->sid, *base);
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

static void fm_playlist_clear(fm_playlist_t *pl)
{
    printf("Clearing old songs\n");
    pl->fm_player_stop();
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

    // set up the mode
    fm_playlist_update_mode(pl);
    // set up the downloader stack
    pl->stack = stack_init();
    // wire up the player
    pl->fm_player_stop = fm_player_stop;
    // set up the downloader stuff
    pl->song_download_stop = 0;
    pl->tid_download = 0;
    pl->current_download = NULL;
    pthread_mutex_init(&pl->mutex_song_download_stop, NULL);
    pthread_mutex_init(&pl->mutex_current_download, NULL);
    pthread_mutex_init(&pl->mutex_song_downloader, NULL);
    pthread_cond_init(&pl->cond_song_download_restart, NULL);
}

void fm_playlist_cleanup(fm_playlist_t *pl)
{
    fm_playlist_hisotry_clear(pl);
    fm_playlist_clear(pl);
    stack_free(pl->stack);
    pthread_mutex_destroy(&pl->mutex_song_download_stop);
    pthread_mutex_destroy(&pl->mutex_current_download);
    pthread_mutex_destroy(&pl->mutex_song_downloader);
    pthread_cond_destroy(&pl->cond_song_download_restart);
}

static int fm_playlist_douban_parse_json(fm_playlist_t *pl, struct json_object *obj, fm_song_t **base)
{
    if (!obj)
        return -1;
    int i;
    int ret = json_object_get_int(json_object_object_get(obj, "r"));
    if (ret != 0) {
        printf("API error: %s\n", json_object_get_string(json_object_object_get(obj, "err")));
    } else {
        printf("Douban playlist parsing new API response\n");
        array_list *songs = json_object_get_array(json_object_object_get(obj, "song"));
        printf("parsed song\n");
        for (i = songs->length - 1; i >= 0; i--) {
            struct json_object *o = (struct json_object*) array_list_get_idx(songs, i);
            fm_song_t *song = fm_song_douban_parse_json(pl, o);
            fm_playlist_push_front(base, song);
        }
    }
    json_object_put(obj); 
    return ret;
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
        case 'r': 
            format = "%s/music/post_love_song";
            printf("Rating song for jing: tid = %d\n", pl->current->sid);
            sprintf(buf, "uid=%d&tid=%d", pl->config.jing_uid, pl->current->sid);
            break;
        case 'b':
            format = "%s/music/post_hate_song";
            sprintf(buf, "uid=%d&tid=%d", pl->config.jing_uid, pl->current->sid);
            break;
        case 'i':
            format = "%s/music/fetch_track_infos";
            // leveraging the data field; make sure the data and act are set accordingly to avoid problems
            sprintf(buf, "uid=%d&tid=%d", pl->config.jing_uid, *(int *)data);
            break;
    }

    curl_easy_setopt(curl, CURLOPT_COPYPOSTFIELDS, buf);

    sprintf(buf,format, pl->jing_api);
    curl_easy_setopt(curl, CURLOPT_URL, buf);
    // set up the headers; should call init headers before this function
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, slist);
}

static int fm_playlist_jing_parse_json(fm_playlist_t *pl, struct json_object *obj, fm_song_t **base)
{
    // here we are only going to parse the fetch_pls (conceivably)
    int ret = 0;
    if ((obj = fm_jing_parse_json_result(obj)))  {
        printf("Jing playlist parsing new API response\n");
        array_list *song_objs = json_object_get_array(json_object_object_get(obj, "items"));
        printf("parsed song\n");
        // we should make use of a multihandle that accelerates the pulling process
        int len = song_objs->length;
        printf("Number of songs returned is %d\n", len);
        if (len > 0) {
            int i;
            fm_song_t *songs[len];
            // get the len number of downloaders
            downloader_t *dls[len * 2];
            stack_get_idle_downloaders(pl->stack, dls, len * 2, dMem);
            int used = 0;

            struct curl_slist *slist;
            fm_playlist_curl_jing_headers_init(pl, &slist);
            for (i=0; i<len; i++) {
                struct json_object *o = (struct json_object*) array_list_get_idx(song_objs, i);
                songs[i] = fm_song_jing_parse_json(pl, o);
            }
            // add the m downloaders
            for (i=0; i<len; i++) {
                if (songs[i] && songs[i]->audio[0] != '\0') {
                    fm_playlist_curl_jing_config(pl, dls[used]->curl, 'm', slist, songs[i]->audio);
                    dls[used++]->data = songs[i];
                } 
            }
            int nmdls = used;
            // add the i downloaders
            for (i=0; i<len; i++) {
                if (songs[i]) {
                    fm_playlist_curl_jing_config(pl, dls[used]->curl, 'i', slist, &songs[i]->sid);
                    dls[used++]->data = songs[i];
                }
            }

            printf("Jing song parser: %d of m downloaders used, %d of i downloaders used\n", nmdls, used-nmdls);
            stack_perform_until_all_done(pl->stack, dls, used);
            printf("Jing song parsing finished\n");
            fm_song_t *song;
            for (i=0; i<nmdls; i++) {
                song = (fm_song_t *)dls[i]->data;
                json_object *o = json_tokener_parse(dls[i]->content.mbuf->data);
                if ((o = fm_jing_parse_json_result(o))) {
                    strcpy(song->audio, json_object_get_string(o));
                    printf("Successfully retrieved the audio url %s for song title: %s\n", song->audio, song->title);
                }
            }
            for (i=nmdls; i<used; i++) {
                song = (fm_song_t *)dls[i]->data;
                json_object *o = json_tokener_parse(dls[i]->content.mbuf->data);
                if ((o = fm_jing_parse_json_result(json_tokener_parse(dls[i]->content.mbuf->data)))) {
                    song->like = *json_object_get_string(json_object_object_get(o, "lvd")) == 'l' ? 1 : 0;
                    printf("Song %s is liked? %d\n", song->title, song->like);
                }
                if (!valid_song_url(song->audio) && song->filepath[0] == '\0') {
                    // this song is not useful anymore
                    fm_song_free(pl, song);
                    song = NULL;
                }
                fm_playlist_push_front(base, song);
            }
            curl_slist_free_all(slist);
        } else {
            printf("Jing song parser: no song available for the given channel\n");
            ret = -1;
        }
    } else {
        printf("Jing song parser: no result returned from Jing.fm\n");
        ret = -1;
    }

    json_object_put(obj); 
    return ret;
}

static int fm_playlist_local_dump_parse_report(fm_playlist_t *pl, fm_song_t **base)
{
    char buf[512];
    sprintf(buf,
            "IFS='\n';"
            "args=($(find $'%s' -type f -mmin +2 -print0 | xargs -0 file -iF'\t' | fgrep audio | cut -d'\t' -f1 | shuf | head -n '%d'));"
            "mutagen -f '{path}\n{title}\n{artist}\n{wors}\n{album}\n{year}\n{kbps}\n{len}' \"${args[@]}\";"
            , pl->config.music_dir, N_LOCAL_CHANNEL_FETCH);
    printf("Local channel refilling command is: %s\n", buf);
    // the field reference counter
    FILE *fp = popen(buf, "r");
    if (!fp) {
        printf("Failed to open the pipe for the command to find the local music\n");
        return -1;
    }
    fm_song_t *song = NULL;
    int fl = 8, fn = fl - 1;
    char ch = '\n', *lastf = NULL;
    int len = 0, size = 0;
    while (1) {
        // get the next character
        if (ch == '\n' || ch == EOF) {
            if (lastf) {
                // append the 0 at the end
                lastf[len] = '\0';
                switch (fn) {
                    case 0: {
                        // if path is nil then we should break out
                        if (lastf[0] == '\0') {
                            if (song)
                                fm_song_free(pl, song);
                            printf("Empty path field for song. Stopped parsing\n");
                            return -1;
                        }
                        // ext is the ext given in the filepath
                        char *p = lastf;
                        int exti = -1;
                        size = sizeof(song->ext);
                        song->ext[0] = '\0';
                        while (*p != '\0') {
                            if (*p == '.') {
                                exti = 0;
                                // clear all fields for the ext
                                memset(song->ext, 0, size);
                            } else if (exti >= 0 && exti < size-1) {
                                song->ext[exti++] = *p;
                            } 
                            p++;
                        }
                        printf("Obtained extension: %s\n", song->ext);
                        break;
                    }
                    case 5: song->pubdate = atoi(lastf); break;
                    case 7: song->length = atoi(lastf); break;
                    default: break;
                }
            }
            printf("Obtained song field: %s for song %p\n", lastf, song);
            if (fn == fl - 1) {
                // push the last song
                fm_playlist_push_front(base, song);
                if (ch == EOF)
                    break; 
                song = song_init(pl);
                song->like = 1;
            }
            fn = (fn+1) % fl;
            // set up the last field
            switch(fn) {
                case 0: lastf = song->filepath; size = sizeof(song->filepath); break;
                case 1: lastf = song->title; size = sizeof(song->title); break;
                case 2: lastf = song->artist; size = sizeof(song->artist); break;
                case 3: lastf = song->url; size = sizeof(song->url); break;
                case 4: lastf = song->album; size = sizeof(song->album); break;
                case 5: lastf = buf; size = sizeof(buf); break;
                case 6: lastf = song->kbps; size = sizeof(song->kbps); break;
                case 7: lastf = buf; size = sizeof(buf); break;
            }
            len = 0;
        } else {
            if (len < size - 1)
                lastf[len++] = ch;
            else {
                printf("Buffer overflow for song fields. Last recorded field = %s\n", lastf);
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
    char url[1024], h_arg[512] = "";
    switch(act) {
        case 'r': case 'u': case 'e':
            break;
        default:
            sprintf(h_arg, "&h=%s", fm_playlist_history_str(pl));
    }
    printf("Playlist send report: %d:%c\n", pl->config.douban_uid, act);
    sprintf(url, "%s?app_name=%s&version=%s&user_id=%d&expire=%d&token=%s&channel=%s&sid=%d&type=%c%s&kbps=%s",
            pl->douban_api, pl->app_name, pl->version, pl->config.douban_uid, pl->config.expire, pl->config.douban_token, pl->config.channel,
            pl->current ? pl->current->sid : 0, act, h_arg, pl->config.kbps);
    printf("Playlist request: %s\n", url);

    curl_easy_setopt(curl, CURLOPT_URL, url);
}

// the recycle flag tells the function to reinit the states beforing proceeding
static int song_downloader_init(fm_playlist_t *pl, downloader_t *dl, int recycle) {
    int ret = -1;
    pthread_mutex_lock(&pl->mutex_current_download);
    if (pl->current_download) {
        fm_song_t *s = *pl->current_download;
        while (s && !valid_song_url(s->audio)) {
            printf("Skipped song %s with audio field %s\n", s->title, s->audio);
            s = s->next;
        }
        if (s) {
            ret = 0;
            if (recycle) {
                fdownloader_config(dl);
            }
            // set the url
            // checking for the validity of the url
            printf("Setting the url %s(%s) for the song downloader %p\n", s->audio, s->title, dl);
            curl_easy_setopt(dl->curl, CURLOPT_URL, s->audio);
            printf("File path %s is copied to the song\n", dl->content.fbuf->filepath);
            strcpy(s->filepath, dl->content.fbuf->filepath);
            s->downloader = dl;
            dl->data = s;
            pl->current_download = &s->next;
        }
    } 
    pthread_mutex_unlock(&pl->mutex_current_download);
    return ret;
}

static downloader_t *process_download(downloader_stack_t *stack, downloader_t **start, int length, void *data)
{
    /*printf("Download process conditon begun\n");*/
    int i;
    fm_playlist_t *pl = (fm_playlist_t *)data;
    // we check for the existing state 

    pthread_mutex_lock(&pl->mutex_song_download_stop);
    if (pl->song_download_stop) {
        pl->song_download_stop = 0;
        printf("Download process waiting for signal of restarting\n");
        pthread_cond_wait(&pl->cond_song_download_restart, &pl->mutex_song_download_stop);
    } 
    // perform any download if we can
    int all_finished = 1;
    for (i=0; i<length; i++) {
        /*printf("Looping through the downloaders\n");*/
        if (start[i]->idle) {
            /*printf("Obtained idle song downloader %p\n", start[i]);*/
            song_downloader_stop(pl, start[i]);
            // reinitialize the downloaders and configure them
            if (song_downloader_init(pl, start[i], 1) == 0) {
                all_finished = 0;
                // readd the downloaders to the stack
                stack_downloader_init(stack, start[i]);
            }
        } else {
            all_finished = 0;
        }
    }
    pthread_mutex_unlock(&pl->mutex_song_download_stop);
    if (all_finished) {
        // simply return
        printf("All downloads finished\n");
        return start[0];
    }
    return NULL;
}

static void* download_thread(void *data)
{
    printf("Download thread started\n");
    fm_playlist_t *pl = (fm_playlist_t *)data;
    // first get the downloaders
    downloader_t *song_downloaders[N_SONG_DOWNLOADERS];
    int i;
    // initialize and lock the downloaders; doing this in the main thread to guarantee no race condition
    printf("Getting idle song downloaders\n");
    stack_get_idle_downloaders(pl->stack, song_downloaders, N_SONG_DOWNLOADERS, dFile);
    for (i=0; i<N_SONG_DOWNLOADERS; i++) {
        printf("Intializing the song downloader %p\n", song_downloaders[i]);
        song_downloader_init(pl, song_downloaders[i], 0); 
        // no need to call stack downloader init since when they are added first time that will be automatically called
    }
    printf("Start performing\n");
    stack_perform_until_condition_met(pl->stack, song_downloaders, N_SONG_DOWNLOADERS, pl, process_download);
    pl->tid_download = 0;
    return data;
}

static void song_downloader_all_start(fm_playlist_t *pl)
{
    if (!pl->tid_download) {
        printf("Creating the download thread\n");
        // need to synchronize
        // do not proceed to play the music unless you are sure that the first song has been assigned a downloader
        pthread_create(&pl->tid_download, NULL, download_thread, pl);
    }
}

// base: the base to append the result in front of (NULL if result should be discarded)
// clear_old, whether the old songs should be cleared; only used when base is not NULL
// fallback: whether fallback should be used (use local station when network unavailable)
static int fm_playlist_send_report(fm_playlist_t *pl, char act, fm_song_t **base, int clear_old, int fallback)
{
    if (pl->mode == plLocal) {
        pthread_mutex_lock(&pl->mutex_current_download);
        if (clear_old)
            fm_playlist_clear(pl);
        int ret = fm_playlist_local_dump_parse_report(pl, base);
        pthread_mutex_unlock(&pl->mutex_current_download);
        return ret;
    }

    int (*parse_fun) (fm_playlist_t *pl, json_object *obj, fm_song_t **base);
    downloader_t *dl = stack_get_idle_downloader(pl->stack, base ? dMem : dDrop);
    printf("### Downloader obtained for playlist retrieval is %p\n", dl);
    switch (pl->mode) {
        case plDouban: // we should first request the downloader; obtain the curl handle and then 
            fm_playlist_curl_douban_config(pl, dl->curl, act);
            stack_perform_until_done(pl->stack, dl);
            printf("### Downloader finished is %p; idle %d\n", dl, dl->idle);
            parse_fun = fm_playlist_douban_parse_json;
            break;
        case plJing: {
            struct curl_slist *slist;
            fm_playlist_curl_jing_headers_init(pl, &slist);
            // the jing config shouldn't involve any data for the playlist. Otherwise there's some error
            fm_playlist_curl_jing_config(pl, dl->curl, act, slist, NULL);
            stack_perform_until_done(pl->stack, dl);
            printf("### Downloader finished is %p; idle %d\n", dl, dl->idle);
            curl_slist_free_all(slist);
            parse_fun = fm_playlist_jing_parse_json;
            break;
        }
        default: return -1;
    }
    int reset_current = clear_old || base == &pl->current;
    if (!base)
        return 0;
    if (reset_current) {
        // stop the player first
        pl->fm_player_stop();
        printf("Trying to stop all downloaders\n");
        pthread_mutex_lock(&pl->mutex_song_download_stop);
        pl->song_download_stop = 1;
        pthread_mutex_unlock(&pl->mutex_song_download_stop);
    }
    // changing the song structure
    pthread_mutex_lock(&pl->mutex_current_download);
    if (clear_old) {
        fm_playlist_clear(pl);
    }
    printf("Attempting to parse the output\n");
    int ret = parse_fun(pl, json_tokener_parse(dl->content.mbuf->data), base);
    if (ret == 0 && reset_current) {
        pl->current_download = &pl->current;
        printf("Resetting current download to %s / %s with url %s\n", (*pl->current_download)->artist, (*pl->current_download)->title, (*pl->current_download)->audio);
    }
    pthread_mutex_unlock(&pl->mutex_current_download);

    if (ret == 0) {
        if (reset_current) {
            // signal the condition
            pthread_cond_signal(&pl->cond_song_download_restart);
            // we should reset the stop flag to 0; because at this stage the download thread should definitely go on
            pthread_mutex_lock(&pl->mutex_song_download_stop);
            pl->song_download_stop = 0;
            pthread_mutex_unlock(&pl->mutex_song_download_stop);
        }  
        printf("Starting song downloaders\n");
        song_downloader_all_start(pl);
    } else {
        printf("Some error occurred during the process; Maybe network is down. Output is %s\n", dl->content.mbuf->data);
        if (fallback) {
            printf("Trying again with local channel.\n");
            strcpy(pl->config.channel, LOCAL_CHANNEL);
            pl->mode = plLocal;
            return fm_playlist_send_report(pl, act, base, clear_old, 0);
        }
        return -1;
    }
    return 0;
}

fm_song_t* fm_playlist_current(fm_playlist_t *pl)
{
    if (!pl->current)
        return fm_playlist_next(pl);
    else
        return pl->current;
}

static fm_song_t **playlist_end_in_number(fm_song_t **start, int n)
{
    while (n>0) {
        if (!*start)
            return start;
        start = &(*start)->next;
        n--;
    }
    return NULL;
}

static void fm_playlist_next_on_link(fm_playlist_t *pl)
{
    // stop the player first
    pl->fm_player_stop();
    fm_song_free(pl, fm_playlist_pop_front(pl));
    // starts caching when there are two songs left
    fm_song_t **ef = playlist_end_in_number(&pl->current, PLAYLIST_REFILL_THRESHOLD);
    if (ef) {
        printf("Playlist going to terminate in %d hops, request more\n", PLAYLIST_REFILL_THRESHOLD);
        fm_playlist_send_report(pl, 'p', ef, 0, 1);
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
                fm_playlist_send_report(pl, 'e', NULL, 0, 1);
                break;
        }
        fm_playlist_next_on_link(pl);
    }
    else {
        printf("Playlist init empty, request new\n");
        fm_playlist_send_report(pl, 'n', &pl->current, 0, 1);
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
                    fm_playlist_send_report(pl, 's', &pl->current, 1, 1);
                } else 
                    fm_playlist_next_on_link(pl);
                break;
            case plDouban: 
                fm_playlist_history_add(pl, pl->current, 's');
                fm_playlist_send_report(pl, 's', &pl->current, 1, 1);
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
                fm_playlist_send_report(pl, 'b', &pl->current, 1, 1);
                break;
            case plJing:
                fm_playlist_send_report(pl, 'b', NULL, 0, 0);
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
                fm_playlist_send_report(pl, 'r', NULL, 0, 0);
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
                fm_playlist_send_report(pl, 'u', NULL, 0, 0);
                break;
        }
    }
}
