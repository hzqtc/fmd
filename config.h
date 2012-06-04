#ifndef _FM_CONFIG_H_
#define _FM_CONIFG_H_

enum fm_config_type {
    FM_CONFIG_INT, FM_CONFIG_STR
};

typedef struct {
    enum fm_config_type type;
    char *section;
    char *key;
    union {
        int *i;
        char *s;
    } val;
} fm_config_t;

int fm_config_parse(const char *file, fm_config_t *items, int length);

#endif
