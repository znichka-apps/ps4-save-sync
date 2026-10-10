/* Shared decrypted export layout used by ZIP export, FTP, and Google backup. */
#include <zip.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include "save_zip.h"
static int failure(char *out, size_t cap, const char *op, int saved_errno, zip_t *zip, int code)
{
    int system = 0;
    if (zip) {
        zip_error_t *error = zip_get_error(zip);
        code = zip_error_code_zip(error);
        system = zip_error_code_system(error);
    }
    if (out && cap && !*out)
        snprintf(out, cap, "ZIP %s: errno=%d zip=%d system=%d", op, saved_errno, code, system);
    return 0;
}
static int entry_stat(const char *path, const struct dirent *entry, struct stat *info)
{
#ifdef __PS4__
    /* OpenOrbis musl lstat -> fstatat is ENOSYS on PS4. getdents supplies
     * types without following links. Unknown types still fail via lstat;
     * never fall back to stat, which would follow a symlink out of the save. */
    if (entry->d_type == DT_DIR || entry->d_type == DT_REG) {
        memset(info, 0, sizeof(*info));
        info->st_mode = entry->d_type == DT_DIR ? S_IFDIR : S_IFREG;
        return 0;
    }
    if (entry->d_type != DT_UNKNOWN) {
        memset(info, 0, sizeof(*info)); /* Rejected as unsupported below. */
        return 0;
    }
#else
    (void)entry;
#endif
    return lstat(path, info);
}
static int walk(const char *base, const char *input, zip_t *zip,
                int (*cancel)(void*), void *data, char *out, size_t cap)
{
    size_t prefix = strlen(base) + 1;
    errno = 0;
    DIR *dir = opendir(input);
    if (!dir) return failure(out, cap, "opendir", errno, NULL, 0);
    int ok = 1;
    errno = 0;
    if (strlen(input) > prefix && zip_add_dir(zip, input + prefix) < 0)
        ok = failure(out, cap, "zip_add_dir", errno, zip, 0);
    struct dirent *entry;
    while (ok) {
        errno = 0; entry = readdir(dir);
        if (!entry) { if (errno) ok = failure(out, cap, "readdir", errno, NULL, 0); break; }
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        if (cancel && cancel(data)) { ok = failure(out, cap, "cancelled", 0, NULL, ZIP_ER_CANCELLED); break; }
        char path[1024]; struct stat info;
        int n = snprintf(path, sizeof(path), "%s%s", input, entry->d_name);
        if (n < 0 || (size_t)n + 1 >= sizeof(path)) { ok = failure(out, cap, "path validation", ENAMETOOLONG, NULL, 0); break; }
        errno = 0;
        if (entry_stat(path, entry, &info) != 0) { ok = failure(out, cap, "lstat", errno, NULL, 0); break; }
        if (S_ISDIR(info.st_mode)) {
            strcat(path, "/"); ok = walk(base, path, zip, cancel, data, out, cap);
        } else if (S_ISREG(info.st_mode)) {
            errno = 0;
            zip_source_t *source = zip_source_file(zip, path, 0, 0);
            if (!source) { ok = failure(out, cap, "zip_source_file", errno, zip, 0); break; }
            errno = 0;
            zip_int64_t index = zip_add(zip, path + prefix, source);
            if (index < 0) { ok = failure(out, cap, "zip_add", errno, zip, 0); zip_source_free(source); break; }
            errno = 0;
            if (zip_file_set_external_attributes(zip, index, 0, ZIP_OPSYS_UNIX, (zip_uint32_t)0100644 << 16) < 0)
                ok = failure(out, cap, "external attributes", errno, zip, 0);
        } else ok = failure(out, cap, "unsupported file type", 0, NULL, 0); /* Never follow symlinks. */
    }
    errno = 0;
    if (closedir(dir) != 0) ok = failure(out, cap, "closedir", errno, NULL, 0);
    return ok;
}
typedef struct { int (*cancel)(void*); void *data; } zip_cancel;
static int cancel_zip(zip_t *zip, void *opaque)
{
    (void)zip; zip_cancel *ctx = opaque;
    return ctx->cancel && ctx->cancel(ctx->data);
}
int zip_directory_diagnostic(const char *base, const char *input, const char *output, int (*cancel)(void*), void *data, char *out, size_t cap)
{
    if (out && cap) *out = 0;
    if (!base || !input || !output || !*output || !*base || strlen(input) <= strlen(base) ||
        strncmp(base,input,strlen(base)) || input[strlen(base)] != '/' ||
        input[strlen(input)-1] != '/') return failure(out, cap, "path validation", 0, NULL, ZIP_ER_INVAL);
    int code = 0;
    errno = 0;
    zip_t *zip = zip_open(output, ZIP_CREATE | ZIP_TRUNCATE, &code);
    if (!zip) return failure(out, cap, "zip_open", errno, NULL, code);
    zip_cancel ctx = {cancel, data};
    errno = 0;
    int ok = 1;
    if (zip_register_cancel_callback_with_state(zip, cancel_zip, NULL, &ctx) < 0)
        ok = failure(out, cap, "cancellation callback registration", errno, zip, 0);
    if (!ok || !walk(base, input, zip, cancel, data, out, cap)) {
        zip_discard(zip); unlink(output); return 0;
    }
    errno = 0;
    if (zip_close(zip) != 0) { failure(out, cap, "zip_close", errno, zip, 0); zip_discard(zip); unlink(output); return 0; }
    return 1;
}
int zip_directory_cancel(const char *base, const char *input, const char *output, int (*cancel)(void*), void *data)
{
    return zip_directory_diagnostic(base, input, output, cancel, data, NULL, 0);
}
int zip_directory(const char *base, const char *input, const char *output)
{
    return zip_directory_cancel(base, input, output, NULL, NULL);
}
