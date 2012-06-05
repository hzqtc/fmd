#include "util.h"

#include <ctype.h>
#include <string.h>

char* trim(char *str)
{
    char *left = str;
    char *right = str + (strlen(str) - 1);

    while (isspace(*left)) {
        left++;
    }
    while (isspace(*right)) {
        right--;
    }

    if (left == str) {
        *++right = '\0';
    }
    else {
        char *p = str;
        while (left <= right) {
            *p++ = *left++;
        }
        *p = '\0';
    }

    return str;
}

char* split(char *str, char delimiter)
{
    char *p = strchr(str, delimiter);
    if (p != NULL) {
        *p = '\0';
        return p + 1;
    }
    return NULL;
}
