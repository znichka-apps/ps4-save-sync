#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <orbis/SaveData.h>
#include "google_store.h"

/* No title override: mount this application's own save, for the captured user. */
int google_store(uint32_t user, int operation, char *token, size_t capacity)
{
    OrbisSaveDataMount2 mount = {0};
    OrbisSaveDataMountResult result = {0};
    OrbisSaveDataDirName name = {0};
    char path[256], temp[272];
    int ok = 0;
    FILE *fp = NULL;
    if (!token || capacity < 2 || operation < GOOGLE_STORE_READ || operation > GOOGLE_STORE_CLEAR) return 0;
    memcpy(name.data, "GoogleAuth", sizeof("GoogleAuth"));
    mount.userId = user;
    mount.dirName = &name;
    mount.blocks = ORBIS_SAVE_DATA_BLOCKS_MIN2;
    mount.mountMode = ORBIS_SAVE_DATA_MOUNT_MODE_CREATE2 | ORBIS_SAVE_DATA_MOUNT_MODE_RDWR;
    if (sceSaveDataMount2(&mount, &result) < 0) return 0;
    snprintf(path, sizeof(path), "/mnt/sandbox/PSSY00001_000%s/refresh_token", result.mountPathName);
    snprintf(temp, sizeof(temp), "%s.tmp", path);
    if (operation == GOOGLE_STORE_READ) {
        token[0] = 0;
        fp = fopen(path, "rb");
        if (!fp && errno == ENOENT) ok = 1;
        if (fp) {
            size_t n = fread(token, 1, capacity - 1, fp);
            int end = fgetc(fp);
            ok = end == EOF && !ferror(fp) && n > 0 && !memchr(token, 0, n);
            for (size_t i = 0; ok && i < n; i++)
                if ((unsigned char)token[i] < 33 || (unsigned char)token[i] > 126) ok = 0;
            token[n] = 0;
        }
    } else if (operation == GOOGLE_STORE_CLEAR) {
        ok = (unlink(path) == 0 || errno == ENOENT);
        if (unlink(temp) != 0 && errno != ENOENT) ok = 0;
    } else {
        fp = fopen(temp, "wb");
        if (fp) {
            size_t n = strnlen(token, capacity);
            ok = n > 0 && n < capacity && fwrite(token, 1, n, fp) == n;
            if (fflush(fp) != 0 || fsync(fileno(fp)) != 0) ok = 0;
            if (fclose(fp) != 0) ok = 0;
            fp = NULL;
            if (ok) ok = rename(temp, path) == 0;
            if (!ok) unlink(temp);
        }
    }
    if (fp && fclose(fp) != 0) ok = 0;
    if (sceSaveDataUmount((void*)&result.mountPathName) < 0) ok = 0;
    if (!ok && operation == GOOGLE_STORE_READ) token[0] = 0;
    return ok;
}
