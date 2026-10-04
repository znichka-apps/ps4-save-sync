#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <mbedtls/md.h>
#include "google_upload.h"
static int hash_failure(google_backup *b, const char *operation, int error, int md_error)
{
    if (!b->diagnostic[0]) snprintf(b->diagnostic, sizeof(b->diagnostic),
        "Preparation %s: errno=%d hash=%d", operation, error, md_error);
    return 0;
}
int google_backup_hash(google_backup *b, int (*cancel)(void*), void *data)
{
    errno = 0;
    FILE *fp = fopen(b->archive, "rb");
    if (!fp) return hash_failure(b, "archive reading (fopen)", errno, 0);
    unsigned char buffer[16384], digest[16]; size_t n;
    mbedtls_md_context_t md; mbedtls_md_init(&md);
    int code = mbedtls_md_setup(&md, mbedtls_md_info_from_type(MBEDTLS_MD_MD5), 0);
    if (!code) code = mbedtls_md_starts(&md);
    int ok = code == 0;
    if (!ok) hash_failure(b, "archive hash initialization", 0, code);
    b->size = 0;
    while (ok) {
        errno = 0;
        n = fread(buffer, 1, sizeof(buffer), fp);
        if (ferror(fp)) { ok = hash_failure(b, "archive reading (fread)", errno, 0); break; }
        if (!n) break;
        if (cancel(data)) { ok = hash_failure(b, "archive hash cancellation", 0, 0); break; }
        if (UINT64_MAX - b->size < n) { ok = hash_failure(b, "archive hash size overflow", 0, 0); break; }
        b->size += n; code = mbedtls_md_update(&md, buffer, n);
        if (code) ok = hash_failure(b, "archive hash update", 0, code);
    }
    if (ok && !b->size) ok = hash_failure(b, "archive reading (empty)", 0, 0);
    errno = 0;
    if (fclose(fp) != 0) ok = hash_failure(b, "archive reading (fclose)", errno, 0);
    if (ok && (code = mbedtls_md_finish(&md, digest)) != 0) ok = hash_failure(b, "archive hash finish", 0, code);
    mbedtls_md_free(&md);
    if (ok) for (size_t i = 0; i < sizeof(digest); i++) snprintf(b->md5 + i * 2, 3, "%02x", digest[i]);
    if (!ok) return 0;
    errno = 0;
    time_t timestamp = time(NULL); struct tm utc;
    if (timestamp < 0) return hash_failure(b, "UTC timestamp (time)", errno, 0);
    errno = 0;
    if (!gmtime_r(&timestamp, &utc)) return hash_failure(b, "UTC timestamp (gmtime_r)", errno, 0);
    errno = 0;
    if (!strftime(b->utc, sizeof(b->utc), "%Y-%m-%dT%H:%M:%SZ", &utc)) return hash_failure(b, "UTC timestamp (strftime)", errno, 0);
    return ok;
}
int google_backup_cleanup(google_backup *b)
{
    /* Only this job's exclusively created directory and fixed archive basename. */
    if (!b->temp_dir[0]) return 1;
    if (strncmp(b->temp_dir, GOOGLE_BACKUP_CACHE "drive-", strlen(GOOGLE_BACKUP_CACHE "drive-")) ||
        strchr(b->temp_dir + strlen(GOOGLE_BACKUP_CACHE), '/')) return 0;
    char expected[288]; snprintf(expected, sizeof(expected), "%s/backup.zip", b->temp_dir);
    if (strcmp(expected, b->archive)) return 0;
    int ok = unlink(b->archive) == 0 || errno == ENOENT;
    if (rmdir(b->temp_dir) != 0) ok = 0;
    if (ok) b->temp_dir[0] = b->archive[0] = 0;
    return ok;
}
