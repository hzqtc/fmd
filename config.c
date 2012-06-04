#include "config.h"
#include "util.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

int fm_config_parse(const char *file, fm_config_t *confs, int length)
{
    const int max_len = 64;
    char section[max_len];
    char key[max_len];
    char val[max_len];
    char line[max_len * 2];
    FILE* f;
    int i;
    
    if ((f = fopen(file, "r")) == NULL) {
        perror("open config file");
        return -1;
    }

    while (fgets(line, sizeof(line), f) != NULL) {
        trim(line);
        if (line[0] == '[') {
            strcpy(section, line + 1);
            section[strlen(section) - 1] = '\0';
        }
        else if (strlen(line) > 0) {
            char *p = split(line, '=');
            strcpy(key, line);
            trim(key);
            strcpy(val, p);
            trim(val);
            for (i = 0; i < length; i++) {
                if (strcmp(confs[i].section, section) == 0 && strcmp(confs[i].key, key) == 0) {
                    switch (confs[i].type) {
                        case FM_CONFIG_INT:
                            *confs[i].val.i = atoi(val);
                            break;
                        case FM_CONFIG_STR:
                            strcpy(confs[i].val.s, val);
                            break;
                        default:
                            break;
                    }
                    break;
                }
            }
        }
    }

    return 0;
}
