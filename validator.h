#include <openssl/sha.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

enum validator_mode {
    vNone,
    vSHA256,
    vFileSize,
};

typedef struct {
    enum validator_mode mode;
    union {
        char sha256sum[65];
        int filesize;
    } data;
} validator_t;

void validator_init(validator_t *validator);
void validator_sha256_init(validator_t *validator, const char *sha256);
void validator_filesize_init(validator_t *validator, int size);
// return 1 if the file is valid otherwise 0
int validate(validator_t *validator, char *filepath);
