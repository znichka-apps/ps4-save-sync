#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <orbis/SaveData.h>
#include <sqlite3.h>

#include "saves.h"
#include "common.h"
#include "settings.h"
#include "sd.h"
#include "restore_fs.h"

static int save_mount(const save_entry_t *save, uint32_t mount_mode, char* mount_path, uint32_t empty_user, int empty)
{
	if (save->title_id && save->dir_name && !strcmp(save->title_id, "PSSY00001") &&
		!strcmp(save->dir_name, "GoogleAuth")) return 0;
	char mountDir[256];
	char keyPath[256];
	char volumePath[256];

	snprintf(mountDir, sizeof(mountDir), APOLLO_SANDBOX_PATH, save->dir_name);
	if (empty) {
		struct stat st;
		int native_error;
		if (!restore_fs_init(&native_error) || !restore_fs_lstat(mountDir,&st) || errno!=ENOENT) return 0;
	}
	if (mkdirs(mountDir) < 0)
	{
		int saved_errno = errno;
		LOG("ERROR: can't create '%s'", mountDir);
		errno = saved_errno;
		return 0;
	}

	if (mount_mode & SAVE_FLAG_TROPHY)
	{
		snprintf(keyPath, sizeof(keyPath), TROPHY_PATH_HDD "%s/sealedkey", apollo_config.user_id, save->title_id);
		snprintf(volumePath, sizeof(volumePath), TROPHY_PATH_HDD "%s/trophy.img", apollo_config.user_id, save->title_id);
	}
	else if (mount_mode & SAVE_FLAG_LOCKED)
	{
		snprintf(keyPath, sizeof(keyPath), "%s%s.bin", save->path, save->dir_name);
		snprintf(volumePath, sizeof(volumePath), "%s%s", save->path, save->dir_name);
	}
	else
	{
		snprintf(keyPath, sizeof(keyPath), SAVES_PATH_HDD "%s/%s.bin", apollo_config.user_id, save->title_id, save->dir_name);
		snprintf(volumePath, sizeof(volumePath), SAVES_PATH_HDD "%s/sdimg_%s", apollo_config.user_id, save->title_id, save->dir_name);
	}

	if (empty && orbis_SaveTargetAbsent(save,empty_user)!=1) return 0;
	if (empty || ((mount_mode & ORBIS_SAVE_DATA_MOUNT_MODE_CREATE2) && (file_exists(keyPath) != SUCCESS)))
	{
		sqlite3 *db;
		char *query, dbpath[256];

		LOG("Creating save '%s'...", keyPath);
		if (mkdirs(volumePath) < 0) return 0;
		if ((empty ? createSaveEmpty(volumePath, keyPath, save->blocks) : createSave(volumePath, keyPath, save->blocks)) < 0)
		{
			LOG("ERROR: can't create '%s'", keyPath);
			return 0;
		}

		snprintf(dbpath, sizeof(dbpath), SAVES_DB_PATH, apollo_config.user_id);
		if ((db = open_sqlite_db(dbpath)) == NULL)
			return 0;

		query = sqlite3_mprintf("INSERT INTO savedata(title_id, dir_name, main_title, sub_title, detail, tmp_dir_name, is_broken, user_param, blocks, free_blocks, size_kib, mtime, fake_broken, account_id, user_id, faked_owner, cloud_icon_url, cloud_revision, game_title_id) "
			"VALUES (%Q, %Q, '', '', '', '', 0, 0, %d, %d, %d, strftime('%%Y-%%m-%%dT%%H:%%M:%%S.00Z', CURRENT_TIMESTAMP), 0, %ld, %d, 0, '', 0, %Q);",
			save->title_id, save->dir_name, save->blocks, save->blocks, (save->blocks*32), apollo_config.account_id, apollo_config.user_id, save->title_id);

		if (sqlite3_exec(db, query, NULL, NULL, NULL) != SQLITE_OK)
		{
			LOG("Error inserting '%s': %s", save->title_id, sqlite3_errmsg(db));
			sqlite3_free(query);
			sqlite3_close(db);
			return 0;
		}

		int db_saved = save_sqlite_db(db, dbpath);
		sqlite3_free(query);
		if (sqlite3_close(db)!=SQLITE_OK) db_saved=0;
		if (!db_saved) return 0;
	}

	int mountErrorCode = mountSave(volumePath, keyPath, mountDir);
	if (mountErrorCode < 0)
	{
		int saved_errno = errno;
		LOG("ERROR (%X): can't mount '%s/%s'", mountErrorCode, save->title_id, save->dir_name);
		rmdir(mountDir);
		errno = saved_errno;
		return 0;
	}

	LOG("'%s/%s' mountPath (%s)", save->title_id, save->dir_name, mountDir);
	strlcpy(mount_path, save->dir_name, ORBIS_SAVE_DATA_DIRNAME_DATA_MAXSIZE);

	return 1;
}

int orbis_SaveMount(const save_entry_t *save, uint32_t mode, char* mount_path)
{
    return save_mount(save,mode,mount_path,0,0);
}
int orbis_SaveMountEmpty(const save_entry_t *save, uint32_t user, char* mount_path)
{
    return save_mount(save,ORBIS_SAVE_DATA_MOUNT_MODE_RDWR|ORBIS_SAVE_DATA_MOUNT_MODE_CREATE2|ORBIS_SAVE_DATA_MOUNT_MODE_COPY_ICON, mount_path,user,1);
}
