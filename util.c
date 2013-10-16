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

char *escapesh(char *buf, char *str) 
{
    int l = strlen(str) + 1;
    // just needs to loop through and add backslash to the front
    int i = 0, pi = 0;
    while (i < l) {
        char ch = str[i++];
        if (ch == '\'') 
            buf[pi++] = '\\';
        buf[pi++] = ch;
    }
    return buf;
}
