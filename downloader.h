#include <curl/curl.h>
#define DEFAULT_N_DOWNLOADERS 5

typedef struct {
    char data[8192];
    size_t length;
} mbuffer_t;

typedef struct {
    char filepath[64];
    FILE *file;
} fbuffer_t;

enum downloader_buffer_type {
    bNone,
    bMem,
    bFile,
};

enum downloader_mode {
    dNone,
    dMem,
    dFile,
    dDrop,
    dAny,
};

typedef struct {
    enum downloader_mode mode;
    enum downloader_buffer_type btype;
    // these two states are managed by the downloaders themselves so don't touch them
    int idle;
    int locked;
    // a data variable for recording custom data
    void *data;
    // the easy curl handle responsible for the actual downloading
    CURL *curl;
    // the condition that clients can use to monitor if new content arrived
    pthread_cond_t cond_new_content;
    // content specifies the type of the buffer used
    union {
        mbuffer_t *mbuf;
        fbuffer_t *fbuf;
    } content;
} downloader_t;

downloader_t *downloader_init();
void downloader_free(downloader_t *dl);
void mdownloader_config(downloader_t *dl);
void fdownloader_config(downloader_t *dl);
void ddownloader_config(downloader_t *dl);
void downloader_config_mode(downloader_t *dl, enum downloader_mode m);

// a downloader stack manages a set of downloaders and a multi_handle for all of them
//normally one such stack is usually enough
typedef struct {
    int size;
    int total_size;
    downloader_t **downloaders;
    CURL *multi_handle;
    pthread_mutex_t mutex_op;
    pthread_mutex_t mutex_elem;
} downloader_stack_t;

downloader_stack_t *stack_init();
void stack_add_downloader(downloader_stack_t *stack, downloader_t *d);
// note that you shouldn't pass a downloader here that has not been added to the stack before
// that will cause infinite loops
// this will return any of the downloader in the given list that has become idle
downloader_t *stack_perform_until_any_done(downloader_stack_t *stack, downloader_t **start, int length);
downloader_t *stack_perform_until_condition_met(downloader_stack_t *stack, downloader_t **start, int length, void *data, downloader_t *(*condition)(downloader_stack_t *stack, downloader_t **start, int length, void *data));
void stack_downloader_stop(downloader_stack_t *stack, downloader_t *dl);
// this function will be automatically called in any perform function; however if you are manipulating the handles inside the 
// perform function, this should be used to reinitialize the downloader
void stack_downloader_init(downloader_stack_t *stack, downloader_t *dl);
int stack_perform_until_all_done(downloader_stack_t *stack, downloader_t **start, int length);
int stack_perform_until_done(downloader_stack_t *stack, downloader_t *downloader);
void stack_get_idle_downloaders(downloader_stack_t *stack, downloader_t **start, int length, enum downloader_mode mode);;
downloader_t *stack_get_idle_downloader(downloader_stack_t *stack, enum downloader_mode mode);
void stack_free(downloader_stack_t *stack);
