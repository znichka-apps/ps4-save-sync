/* Shared decrypted export layout used by ZIP export, FTP, and Google backup. */
#include <zip.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include "save_zip.h"
static int walk(const char *base, const char *input, zip_t *zip,
                int (*cancel)(void*), void *data)
{
    size_t prefix = strlen(base) + 1;
    DIR *dir = opendir(input);
    if (!dir) return 0;
    int ok = 1;
    if (strlen(input) > prefix && zip_add_dir(zip, input + prefix) < 0) ok = 0;
    struct dirent *entry;
    while (ok) {
        errno = 0; entry = readdir(dir);
        if (!entry) { if (errno) ok = 0; break; }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (cancel && cancel(data)) { ok = 0; break; }
        char path[1024]; struct stat info;
        int n = snprintf(path, sizeof(path), "%s%s", input, entry->d_name);
        if (n < 0 || (size_t)n + 1 >= sizeof(path) || lstat(path, &info) != 0) { ok = 0; break; }
        if (S_ISDIR(info.st_mode)) {
            strcat(path, "/"); ok = walk(base, path, zip, cancel, data);
        } else if (S_ISREG(info.st_mode)) {
            zip_source_t *source = zip_source_file(zip, path, 0, 0);
            if (!source) { ok = 0; break; }
            zip_int64_t index = zip_add(zip, path + prefix, source);
            if (index < 0) { zip_source_free(source); ok = 0; break; }
            if (zip_file_set_external_attributes(zip, index, 0, ZIP_OPSYS_UNIX, (zip_uint32_t)0100644 << 16) < 0) ok = 0;
        } else ok = 0; /* Never follow symlinks outside the mounted save. */
    }
    if (closedir(dir) != 0) ok = 0;
    return ok;
}
typedef struct { int (*cancel)(void*); void *data; } zip_cancel;
static int cancel_zip(zip_t *zip, void *opaque)
{
    (void)zip; zip_cancel *ctx = opaque;
    return ctx->cancel && ctx->cancel(ctx->data);
}
int zip_directory_cancel(const char *base, const char *input, const char *output, int (*cancel)(void*), void *data)
{
    if (!base || !input || !*base || strlen(input) <= strlen(base) ||
        strncmp(base,input,strlen(base)) || input[strlen(base)] != '/' ||
        input[strlen(input)-1] != '/') return 0;
    zip_t *zip = zip_open(output, ZIP_CREATE | ZIP_TRUNCATE, NULL);
    if (!zip) return 0;
    zip_cancel ctx = {cancel, data};
    if (zip_register_cancel_callback_with_state(zip, cancel_zip, NULL, &ctx) < 0 || !walk(base, input, zip, cancel, data)) {
        zip_discard(zip); unlink(output); return 0;
    }
    if (zip_close(zip) != 0) { zip_discard(zip); unlink(output); return 0; }
    return 1;
}
int zip_directory(const char *base, const char *input, const char *output)
{
    return zip_directory_cancel(base, input, output, NULL, NULL);
}
