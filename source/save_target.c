#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <sqlite3.h>
#include "saves.h"
#include "settings.h"
#include "restore_fs.h"

/* Fail closed on orphan volumes, keys, database rows, and lookup errors.
   These paths are built only from previously validated local restore metadata. */
int orbis_SaveTargetAbsent(const save_entry_t *save, uint32_t user)
{
    if (user != apollo_config.user_id) return -1;
    int native_error;
    if (!restore_fs_init(&native_error)) return -1;
    char path[256]; struct stat st;
    snprintf(path,sizeof(path),SAVES_PATH_HDD "%s",user,save->title_id);
    if (!restore_fs_lstat(path,&st)) { if (!S_ISDIR(st.st_mode)) return -1; }
    else if (errno!=ENOENT) return -1;
    snprintf(path,sizeof(path),SAVES_PATH_HDD "%s/%s.bin",user,save->title_id,save->dir_name);
    if (!restore_fs_lstat(path,&st)) return 0;
    if (errno!=ENOENT) return -1;
    snprintf(path,sizeof(path),SAVES_PATH_HDD "%s/sdimg_%s",user,save->title_id,save->dir_name);
    if (!restore_fs_lstat(path,&st)) return 0;
    if (errno!=ENOENT) return -1;
    snprintf(path,sizeof(path),SAVES_DB_PATH,user);
    sqlite3 *db=open_sqlite_db(path); if (!db) return -1;
    sqlite3_stmt *stmt=NULL; int result=-1;
    if (sqlite3_prepare_v2(db,"SELECT 1 FROM savedata WHERE title_id=? AND dir_name=? LIMIT 1",-1,&stmt,NULL)==SQLITE_OK &&
        sqlite3_bind_text(stmt,1,save->title_id,-1,SQLITE_TRANSIENT)==SQLITE_OK &&
        sqlite3_bind_text(stmt,2,save->dir_name,-1,SQLITE_TRANSIENT)==SQLITE_OK) {
        int code=sqlite3_step(stmt); result=code==SQLITE_DONE?1:code==SQLITE_ROW?0:-1;
    }
    sqlite3_finalize(stmt); if (sqlite3_close(db)!=SQLITE_OK) result=-1;
    return result;
}
