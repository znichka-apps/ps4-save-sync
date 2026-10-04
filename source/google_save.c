/* Platform adapter: this worker owns metadata and never touches selected_entry. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <ctype.h>
#include <orbis/SaveData.h>
#include "saves.h"
#include "common.h"
#include "google_upload.h"
#include "save_zip.h"
int google_backup_stage(google_backup *b, int (*cancel)(void*), void *data)
{
    if (cancel(data) || b->user != apollo_config.user_id ||
        (!strcmp(b->title, "PSSY00001") && !strcmp(b->directory, "GoogleAuth"))) return 0;
    if (strlen(b->title) != 9 || !*b->directory ||
        strlen(b->directory) >= ORBIS_SAVE_DATA_DIRNAME_DATA_MAXSIZE ||
        !strcmp(b->directory,".") || !strcmp(b->directory,"..")) return 0;
    for (const char *p=b->title; *p; p++) if (!isalnum((unsigned char)*p)) return 0;
    for (const char *p=b->directory; *p; p++)
        if (*p=='/' || *p=='\\' || (unsigned char)*p<32) return 0;
    if (mkdirs(GOOGLE_BACKUP_CACHE) != SUCCESS) return 0;
    snprintf(b->temp_dir, sizeof(b->temp_dir), GOOGLE_BACKUP_CACHE "drive-XXXXXX");
    if (!mkdtemp(b->temp_dir)) { b->temp_dir[0] = 0; return 0; }
    snprintf(b->archive, sizeof(b->archive), "%s/backup.zip", b->temp_dir);
    save_entry_t save = {0};
    save.name = b->game; save.title_id = b->title; save.dir_name = b->directory;
    save.flags = SAVE_FLAG_PS4 | SAVE_FLAG_HDD; save.type = FILE_TYPE_PS4;
    char mount[ORBIS_SAVE_DATA_DIRNAME_DATA_MAXSIZE], path[256], base[256];
    if (!orbis_SaveMount(&save, ORBIS_SAVE_DATA_MOUNT_MODE_RDONLY, mount)) return 0;
    snprintf(path, sizeof(path), APOLLO_SANDBOX_PATH, mount);
    snprintf(base, sizeof(base), "%s", path);
    /* Match zipSave/FTP exactly: remove trailing slash, then mount component. */
    char *slash = strrchr(base, '/'); if (slash) *slash = 0;
    slash = strrchr(base, '/'); if (slash) *slash = 0;
    int ok = slash && !cancel(data) && zip_directory_cancel(base, path, b->archive, cancel, data);
    if (!orbis_SaveUmount(mount)) { b->mount_blocked = 1; ok = 0; }
    /* Hash, GoogleAuth, and all networking start only after successful unmount. */
    return ok && !cancel(data) && google_backup_hash(b, cancel, data);
}
