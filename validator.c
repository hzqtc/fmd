#include "validator.h"
#include <sys/stat.h>
#define FILESIZE_MARGIN 100000

void sha256_hash_string (unsigned char hash[SHA256_DIGEST_LENGTH], char outputBuffer[65])
{
    int i = 0;

    for(i = 0; i < SHA256_DIGEST_LENGTH; i++)
    {
        sprintf(outputBuffer + (i * 2), "%02x", hash[i]);
    }

    outputBuffer[64] = 0;
}

int calc_sha256 (char* path, char output[65])
{
    FILE* file = fopen(path, "rb");
    if(!file) return -1;

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    const int bufSize = 32768;
    char* buffer = malloc(bufSize);
    int bytesRead = 0;
    if(!buffer) return -1;
    while((bytesRead = fread(buffer, 1, bufSize, file)))
    {
        SHA256_Update(&sha256, buffer, bytesRead);
    }
    SHA256_Final(hash, &sha256);

    sha256_hash_string(hash, output);
    fclose(file);
    free(buffer);
    return 0;
}      

void validator_init(validator_t *validator)
{
    validator->mode = vNone;
}

void validator_sha256_init(validator_t *validator, const char *sha256)
{
    printf("SHA256 validator initiated with SHA256 string: %s\n", sha256);
    strncpy(validator->data.sha256sum, sha256, 64);
    validator->data.sha256sum[64] = '\0';
    validator->mode = vSHA256;
}

void validator_filesize_init(validator_t *validator, int size)
{
    printf("FileSize validator initiated with filesize: %d\n", size);
    validator->data.filesize = size;
    validator->mode = vFileSize;
}

int validate(validator_t *validator, char *filepath)
{
    switch(validator->mode) {
        case vSHA256: {
            char output[65];
            return calc_sha256(filepath, output) == 0 && strcmp(output, validator->data.sha256sum) == 0;
        }
        case vFileSize: {
            struct stat sts;
            stat(filepath, &sts);
            printf("Attempting to validate path %s with recorded filesize %d vs actual filesize %d\n", filepath, validator->data.filesize, (int) sts.st_size);
            return stat(filepath, &sts) == 0 && validator->data.filesize - sts.st_size < FILESIZE_MARGIN;
        }
        default:
            return 0;
    }
}

