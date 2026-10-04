/* Platform adapter: this worker owns metadata and never touches selected_entry. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <orbis/SaveData.h>
#include "saves.h"
#include "common.h"
#include "google_upload.h"
#include "save_zip.h"
#include "settings.h"
static int stage_failure(google_backup *b, const char *operation, int error)
{
    snprintf(b->diagnostic, sizeof(b->diagnostic), "Preparation %s: errno=%d", operation, error);
    return 0;
}
int google_backup_stage(google_backup *b, int (*cancel)(void*), void *data)
{
    b->diagnostic[0] = 0;
    if (cancel(data)) return stage_failure(b, "cancellation", 0);
    if (b->user != apollo_config.user_id ||
        (!strcmp(b->title, "PSSY00001") && !strcmp(b->directory, "GoogleAuth"))) return stage_failure(b, "validation", 0);
    if (strlen(b->title) != 9 || !*b->directory ||
        strlen(b->directory) >= ORBIS_SAVE_DATA_DIRNAME_DATA_MAXSIZE ||
        !strcmp(b->directory,".") || !strcmp(b->directory,"..")) return stage_failure(b, "validation", 0);
    for (const char *p=b->title; *p; p++) if (!isalnum((unsigned char)*p)) return stage_failure(b, "validation", 0);
    for (const char *p=b->directory; *p; p++)
        if (*p=='/' || *p=='\\' || (unsigned char)*p<32) return stage_failure(b, "validation", 0);
    errno = 0;
    if (mkdirs(GOOGLE_BACKUP_CACHE) != SUCCESS) return stage_failure(b, "cache creation", errno);
    snprintf(b->temp_dir, sizeof(b->temp_dir), GOOGLE_BACKUP_CACHE "drive-XXXXXX");
    errno = 0;
    if (!mkdtemp(b->temp_dir)) { int error = errno; b->temp_dir[0] = 0; return stage_failure(b, "mkdtemp", error); }
    snprintf(b->archive, sizeof(b->archive), "%s/backup.zip", b->temp_dir);
    save_entry_t save = {0};
    save.name = b->game; save.title_id = b->title; save.dir_name = b->directory;
    save.flags = SAVE_FLAG_PS4 | SAVE_FLAG_HDD; save.type = FILE_TYPE_PS4;
    char mount[ORBIS_SAVE_DATA_DIRNAME_DATA_MAXSIZE], path[256], base[256];
    errno = 0;
    if (!orbis_SaveMount(&save, ORBIS_SAVE_DATA_MOUNT_MODE_RDONLY, mount)) return stage_failure(b, "mount", errno);
    snprintf(path, sizeof(path), APOLLO_SANDBOX_PATH, mount);
    snprintf(base, sizeof(base), "%s", path);
    /* Match zipSave/FTP exactly: remove trailing slash, then mount component. */
    char *slash = strrchr(base, '/'); if (slash) *slash = 0;
    slash = strrchr(base, '/'); if (slash) *slash = 0;
    int ok;
    if (!slash) ok = stage_failure(b, "validation", 0);
    else if (cancel(data)) ok = stage_failure(b, "cancellation", 0);
    else ok = zip_directory_diagnostic(base, path, b->archive, cancel, data, b->diagnostic, sizeof(b->diagnostic));
    errno = 0;
    if (!orbis_SaveUmount(mount)) {
        int error = errno;
        b->mount_blocked = 1; ok = 0;
        /* Preserve the original ZIP error as well as the mandatory restart warning. */
        char prior[sizeof(b->diagnostic)]; snprintf(prior, sizeof(prior), "%s", b->diagnostic);
        stage_failure(b, "unmount", error);
        if (*prior) {
            size_t used = strlen(b->diagnostic);
            snprintf(b->diagnostic + used, sizeof(b->diagnostic) - used, "; %s", prior);
        }
    }
    /* Hash, GoogleAuth, and all networking start only after successful unmount. */
    if (!ok) return 0;
    if (cancel(data)) return stage_failure(b, "cancellation", 0);
    return google_backup_hash(b, cancel, data);
}
