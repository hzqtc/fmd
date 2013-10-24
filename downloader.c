#include "downloader.h"
#include <stdlib.h>
#include <string.h>

static int tmp_count = 0;

static void get_tmp_filepath(char *filepath)
{
    sprintf(filepath, "/tmp/fmctmp%d", tmp_count++);
}

// init the downloader by setting all the relevant fields
downloader_t *downloader_init()
{
    downloader_t *dl = (downloader_t *) malloc(sizeof(downloader_t));
    dl->curl = curl_easy_init();
    dl->btype = bNone;
    dl->mode = dNone;
    dl->idle = 1;
    dl->stop = 0;
    dl->locked = 0;
    dl->data = NULL;
    dl->content.mbuf = NULL;
    // set the handle's private field to point to the downloader itself so that later it can be easily retrieved
    curl_easy_setopt(dl->curl, CURLOPT_PRIVATE, dl);
    curl_easy_setopt(dl->curl, CURLOPT_WRITEDATA, dl);
    return dl;
}

static void fdownloader_close(downloader_t *dl)
{
    fclose(dl->content.fbuf->file);
    dl->content.fbuf->file = NULL;
}

// free the respective fields given that the downloader would be used for mode m
// remove all fields if m is dlNone
void downloader_free_buf(downloader_t *dl)
{
    switch (dl->btype) {
        case bMem:
            free(dl->content.mbuf);
            break;
        case bFile:
            // remove this part of the memory
            if (dl->content.fbuf->file)
                fdownloader_close(dl);
            free(dl->content.fbuf);
            break;
        default:
            break;
    }
    dl->content.mbuf = NULL;
    dl->btype = bNone;
}

static size_t append_to_buffer(char *ptr, size_t size, size_t nmemb, void *userp)
{
    downloader_t *dl = (downloader_t *) userp;
    if (dl->stop) {
        return 0;
    }
    dl->idle = 0;
    printf("Entered buffer appending block\n");
    mbuffer_t *buffer = dl->content.mbuf;
    size_t bytes = size * nmemb;
    if (buffer->length + bytes <= sizeof(buffer->data)) {
        memcpy(buffer->data + buffer->length, ptr, bytes);
        buffer->length += bytes;
    } else {
        fprintf(stderr, "Unable to append more data. Buffer is full.");
    }
    return bytes;
}

static size_t drop_buffer(char *ptr, size_t size, size_t nmemb, void *userp)
{
    downloader_t *dl = (downloader_t *) userp;
    if (dl->stop) {
        return 0;
    }
    dl->idle = 0;
    return size * nmemb;
}

void downloader_free_curl(downloader_t *dl)
{
    curl_easy_cleanup(dl->curl);
}

void downloader_free(downloader_t *dl)
{
    downloader_free_buf(dl);
    downloader_free_curl(dl);
    free(dl);
}

void mdownloader_config(downloader_t *dl)
{
    if (dl->btype != bMem) {
        downloader_free_buf(dl);
        dl->btype = bMem;
        // reseting the drop bit
        dl->content.mbuf = (mbuffer_t *) malloc(sizeof(mbuffer_t));
    } 
    dl->mode = dMem;
    // set up the curl options
    curl_easy_setopt(dl->curl, CURLOPT_WRITEFUNCTION, append_to_buffer);
    // reset the mem related fields
    memset(dl->content.mbuf->data, 0, sizeof(dl->content.mbuf->data));
    dl->content.mbuf->length = 0;
}

static size_t append_to_file(char *ptr, size_t size, size_t nmemb, void *userp)
{
    downloader_t *dl = (downloader_t *) userp;
    if (dl->stop) {
        // close the file
        fdownloader_close(dl);
        return 0;
    }
    dl->idle = 0;
    printf("Entered file appending block\n");
    fbuffer_t *buffer = dl->content.fbuf;
    size_t s = fwrite(ptr, size, nmemb, buffer->file);
    return s * size;
}

void fdownloader_config(downloader_t *dl)
{
    if (dl->btype != bFile) {
        downloader_free_buf(dl);
        dl->btype = bFile;
        dl->content.fbuf = (fbuffer_t *) malloc(sizeof(fbuffer_t));
    } else if (dl->content.fbuf->file) {
        fdownloader_close(dl);
    }
    // requesting a new tmp file to be opened
    get_tmp_filepath(dl->content.fbuf->filepath);
    dl->mode = dFile;
    // set up the curl options
    curl_easy_setopt(dl->curl, CURLOPT_WRITEFUNCTION, append_to_file);
    dl->content.fbuf->file = fopen(dl->content.fbuf->filepath, "w");
}

void ddownloader_config(downloader_t *dl)
{
    dl->mode = dDrop;
    curl_easy_setopt(dl->curl, CURLOPT_WRITEFUNCTION, drop_buffer);
}

void downloader_config_mode(downloader_t *dl, enum downloader_mode m)
{
    switch (m) {
        case dNone:
            downloader_free_buf(dl); break;
        case dMem:
            mdownloader_config(dl); break;
        case dFile:
            fdownloader_config(dl); break;
        case dDrop:
            ddownloader_config(dl); break;
        default: break;
    }
}

void downloader_stop(downloader_t *dl)
{
    if (!dl->idle) {
        printf("Downloader forced to stop\n");
        dl->stop = 1;
        dl->idle = 1;
    }
}

void stack_add_downloader(downloader_stack_t *stack, downloader_t *d)
{
    if (stack->total_size == stack->size) {
        // resize by giving double storage
        stack->total_size *= 2;
        downloader_t **na = (downloader_t **) malloc(stack->total_size * sizeof(downloader_t *));
        int i;
        for (i=0; i<stack->size; i++) {
            na[i] = stack->downloaders[i];
        }
        free(stack->downloaders);
        stack->downloaders = na;
    }
    stack->downloaders[stack->size++] = d;
}

void stack_downloader_lock(downloader_stack_t *stack, downloader_t *d)
{
    d->locked = 1;
}

void stack_downloader_unlock(downloader_stack_t *stack, downloader_t *d)
{
    d->locked = 0;
}

downloader_stack_t *stack_init()
{
    downloader_stack_t *stack = (downloader_stack_t *) malloc(sizeof(downloader_stack_t));
    stack->total_size = DEFAULT_N_DOWNLOADERS;
    stack->size = 0;
    stack->downloaders = (downloader_t **) malloc(DEFAULT_N_DOWNLOADERS * sizeof(downloader_t *));
    stack->multi_handle = curl_multi_init();
    int i;
    for (i=0; i<DEFAULT_N_DOWNLOADERS; i++) {
        stack_add_downloader(stack, downloader_init());
    }
    return stack;
}

void stack_mark_idle_downloaders(downloader_stack_t *stack)
{
    CURLMsg *msg;
    int msgs_left;
    downloader_t *d;
    while ((msg = curl_multi_info_read(stack->multi_handle, &msgs_left))) {
        if (msg->msg == CURLMSG_DONE) {
            curl_easy_getinfo(msg->easy_handle, CURLOPT_PRIVATE, &d);
            d->idle = 1;
            // any close action
            if (d->btype == bFile) {
                fdownloader_close(d);
            }
            // remove the handle from the multi_handle
            curl_multi_remove_handle(stack->multi_handle, d->curl);
        }
    }
}

int stack_downloader_is_idle(downloader_stack_t *stack, downloader_t *dl)
{
    stack_mark_idle_downloaders(stack);
    return dl->idle;
}

void stack_downloader_stop(downloader_stack_t *stack, downloader_t *dl)
{
    curl_multi_remove_handle(stack->multi_handle, dl->curl);
    dl->idle = 1;
}

void stack_downloader_init(downloader_stack_t *stack, downloader_t *dl) 
{
    curl_multi_add_handle(stack->multi_handle, dl->curl);
}

downloader_t *stack_perform_until_condition_met(downloader_stack_t *stack, downloader_t **start, int length, void *data, downloader_t *(*condition)(downloader_stack_t *stack, downloader_t **start, int length, void *data))
{
    int still_running, i;
    void *ret;
    // first add all these handles to the mix
    for (i=0; i<length; i++) {
        stack_downloader_init(stack, start[i]);
    }

    /* we start some action by calling perform right away */
    while ( curl_multi_perform(stack->multi_handle, &still_running) == CURLM_CALL_MULTI_PERFORM );

    do {
        stack_mark_idle_downloaders(stack);

        if ((ret = condition(stack, start, length, data)))
            return ret;

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

        curl_multi_timeout(stack->multi_handle, &curl_timeo);
        if(curl_timeo >= 0) {
            timeout.tv_sec = curl_timeo / 1000;
            if(timeout.tv_sec > 1)
                timeout.tv_sec = 1;
            else
                timeout.tv_usec = (curl_timeo % 1000) * 1000;
        }

        curl_multi_fdset(stack->multi_handle, &fdread, &fdwrite, &fdexcep, &maxfd);

        rc = select(maxfd+1, &fdread, &fdwrite, &fdexcep, &timeout);

        switch(rc) {
            case -1:
                /* select error */
                break;
            case 0: /* timeout */
            default: /* action */
                curl_multi_perform(stack->multi_handle, &still_running);
                break;
        }
    } while (still_running);
    // still running turns false; so we should just mark and check again
    stack_mark_idle_downloaders(stack);
    return condition(stack, start, length, data);
}

downloader_t *stack_downloader_any_done(downloader_stack_t *stack, downloader_t **start, int length, void *data)
{
    int i;
    for (i=0; i<length; i++) {
        if (start[i]->idle)
            return start[i];
    }
    return NULL;
}

downloader_t *stack_downloader_all_done(downloader_stack_t *stack, downloader_t **start, int length, void *data)
{
    int i;
    for (i=0; i<length; i++) {
        if (!start[i]->idle)
            return NULL;
    }
    return start[0];
}

downloader_t *stack_perform_until_any_done(downloader_stack_t *stack, downloader_t **start, int length)
{
    return stack_perform_until_condition_met(stack, start, length, NULL, stack_downloader_any_done);
}

int stack_perform_until_all_done(downloader_stack_t *stack, downloader_t **start, int length)
{
    if (stack_perform_until_condition_met(stack, start, length, NULL, stack_downloader_all_done))
        return 0;
    return -1;
}

int stack_perform_until_done(downloader_stack_t *stack, downloader_t *downloader)
{
    return stack_perform_until_all_done(stack, &downloader, 1);
}


// this will attempt to get as many downloaders as specified and write all the addresses to the
// specified address as the starting address
// if there are no more downloaders then we will try to allocate a new one
// the order
// 1. idle downloader with the preferred mode
// 2. idle downloader
// 3. newly allocated downloader
void stack_get_idle_downloaders(downloader_stack_t *stack, downloader_t **start, int length, enum downloader_mode mode)
{
    stack_mark_idle_downloaders(stack);
    int i, n = length;
    enum downloader_buffer_type preferred_btype = bNone;
    switch (mode) {
        case dMem: preferred_btype = bMem; break;
        case dFile: preferred_btype = bFile; break;
        default: break;
    }
    // loop through the multi_handle's message queue to get all those 'done' downloaders
    downloader_t *idlers[stack->size], *dl;
    int idlex = 0;
    for (i=0; n > 0 && i<stack->size; i++) {
        dl = stack->downloaders[i];
        if (!dl->locked && dl->idle) {
            if (dl->mode == mode || mode == dAny || mode == dDrop || dl->btype == preferred_btype)
                start[n--] = dl;
            else
                idlers[idlex++] = dl;
        }

    }
    for (i=0; n > 0 && i < idlex; i++) {
        start[n--] = idlers[i];
    }
    while (n > 0) {
        dl = downloader_init();
        start[n--] = dl;
        stack_add_downloader(stack, dl);
    }
    // configure all the downloaders to be returned
    for (i=0; i < length; i++) {
        downloader_config_mode(start[i], mode);
    }
}

downloader_t *stack_get_idle_downloader(downloader_stack_t *stack, enum downloader_mode mode)
{
    downloader_t *dl;
    stack_get_idle_downloaders(stack, &dl, 1, mode);
    return dl;
}

void stack_free(downloader_stack_t *stack)
{
    int i;
    for (i=0; i<stack->size; i++) {
        downloader_free(stack->downloaders[i]);
    }
    free(stack->downloaders);
    curl_multi_cleanup(stack->multi_handle);
    free(stack);
}
