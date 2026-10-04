#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <mbedtls/md.h>
#include "google_upload.h"
int google_backup_hash(google_backup *b, int (*cancel)(void*), void *data)
{
    FILE *fp = fopen(b->archive, "rb");
    if (!fp) return 0;
    unsigned char buffer[16384], digest[16]; size_t n;
    mbedtls_md_context_t md; mbedtls_md_init(&md);
    int ok = mbedtls_md_setup(&md, mbedtls_md_info_from_type(MBEDTLS_MD_MD5), 0) == 0 && mbedtls_md_starts(&md) == 0;
    b->size = 0;
    while (ok && (n = fread(buffer, 1, sizeof(buffer), fp))) {
        if (cancel(data) || UINT64_MAX - b->size < n) { ok = 0; break; }
        b->size += n; ok = mbedtls_md_update(&md, buffer, n) == 0;
    }
    if (ferror(fp) || !b->size) ok = 0;
    if (fclose(fp) != 0) ok = 0;
    if (ok) ok = mbedtls_md_finish(&md, digest) == 0;
    mbedtls_md_free(&md);
    if (ok) for (size_t i = 0; i < sizeof(digest); i++) snprintf(b->md5 + i * 2, 3, "%02x", digest[i]);
    time_t timestamp = time(NULL); struct tm utc;
    if (timestamp < 0 || !gmtime_r(&timestamp, &utc) || !strftime(b->utc, sizeof(b->utc), "%Y-%m-%dT%H:%M:%SZ", &utc)) ok = 0;
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
