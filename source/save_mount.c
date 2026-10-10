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

static int mount_failed(save_mount_diagnostic_t *diagnostic, const char *call, int result, int error)
{
	if (diagnostic) {
		diagnostic->call=call;
		diagnostic->native_result=result;
		diagnostic->error=error;
	}
	errno=error;
	return 0;
}

static int save_mount(const save_entry_t *save, uint32_t mount_mode, char* mount_path, uint32_t empty_user, int empty,
	int *mount_uncertain, save_mount_diagnostic_t *diagnostic)
{
	if (mount_uncertain) *mount_uncertain=0;
	if (diagnostic) { diagnostic->call="none"; diagnostic->native_result=0; diagnostic->error=0; }
	if (save->title_id && save->dir_name && !strcmp(save->title_id, "PSSY00001") &&
		!strcmp(save->dir_name, "GoogleAuth")) return mount_failed(diagnostic,"reserved_save",0,0);
	char mountDir[256];
	char keyPath[256];
	char volumePath[256];

	snprintf(mountDir, sizeof(mountDir), APOLLO_SANDBOX_PATH, save->dir_name);
	if (empty) {
		struct stat st;
		int native_error=0;
		if (!restore_fs_init(&native_error)) return mount_failed(diagnostic,"restore_fs_init",native_error,errno);
		errno=0;
		int stat_result=restore_fs_lstat(mountDir,&st);
		int stat_error=errno;
		if (stat_result==0 || stat_error!=ENOENT)
			return mount_failed(diagnostic,"mount_dir_lstat",stat_result,stat_error);
	}
	int mkdir_result=mkdirs(mountDir);
	if (mkdir_result < 0)
	{
		int saved_errno = errno;
		LOG("ERROR: can't create '%s'", mountDir);
		return mount_failed(diagnostic,"mkdirs_mount_dir",mkdir_result,saved_errno);
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

	if (empty) {
		errno=0;
		int absent=orbis_SaveTargetAbsent(save,empty_user);
		int absent_error=errno;
		if (absent!=1) return mount_failed(diagnostic,"SaveTargetAbsent",absent,absent_error);
	}
	if (empty || ((mount_mode & ORBIS_SAVE_DATA_MOUNT_MODE_CREATE2) && (file_exists(keyPath) != SUCCESS)))
	{
		sqlite3 *db;
		char *query, dbpath[256];

		LOG("Creating save '%s'...", keyPath);
		mkdir_result=mkdirs(volumePath);
		if (mkdir_result < 0) return mount_failed(diagnostic,"mkdirs_volume",mkdir_result,errno);
		errno=0;
		int create_result=empty ? createSaveEmpty(volumePath, keyPath, save->blocks) :
			createSave(volumePath, keyPath, save->blocks);
		int create_error=errno;
		if (create_result < 0)
		{
			LOG("ERROR: can't create '%s'", keyPath);
			return mount_failed(diagnostic,empty?"createSaveEmpty":"createSave",create_result,create_error);
		}

		snprintf(dbpath, sizeof(dbpath), SAVES_DB_PATH, apollo_config.user_id);
		errno=0;
		if ((db = open_sqlite_db(dbpath)) == NULL)
			return mount_failed(diagnostic,"open_sqlite_db",0,errno);

		errno=0;
		query = sqlite3_mprintf("INSERT INTO savedata(title_id, dir_name, main_title, sub_title, detail, tmp_dir_name, is_broken, user_param, blocks, free_blocks, size_kib, mtime, fake_broken, account_id, user_id, faked_owner, cloud_icon_url, cloud_revision, game_title_id) "
			"VALUES (%Q, %Q, '', '', '', '', 0, 0, %d, %d, %d, strftime('%%Y-%%m-%%dT%%H:%%M:%%S.00Z', CURRENT_TIMESTAMP), 0, %ld, %d, 0, '', 0, %Q);",
			save->title_id, save->dir_name, save->blocks, save->blocks, (save->blocks*32), apollo_config.account_id, apollo_config.user_id, save->title_id);
		if (!query) { int error=errno; sqlite3_close(db); return mount_failed(diagnostic,"sqlite3_mprintf",0,error); }

		errno=0;
		int sql_result=sqlite3_exec(db, query, NULL, NULL, NULL);
		if (sql_result != SQLITE_OK)
		{
			int error=errno;
			LOG("Error inserting '%s': %s", save->title_id, sqlite3_errmsg(db));
			sqlite3_free(query);
			sqlite3_close(db);
			return mount_failed(diagnostic,"sqlite3_exec_insert",sql_result,error);
		}

		errno=0;
		int db_saved = save_sqlite_db(db, dbpath);
		int save_error=errno;
		sqlite3_free(query);
		errno=0;
		int close_result=sqlite3_close(db);
		int close_error=errno;
		if (!db_saved) return mount_failed(diagnostic,"save_sqlite_db",db_saved,save_error);
		if (close_result!=SQLITE_OK) return mount_failed(diagnostic,"sqlite3_close",close_result,close_error);
	}

	errno=0;
	int mountErrorCode = mountSave(volumePath, keyPath, mountDir);
	int mount_error=errno;
	if (mountErrorCode < 0)
	{
		LOG("ERROR (%X): can't mount '%s/%s'", mountErrorCode, save->title_id, save->dir_name);
		/* The private SDK does not establish that a failed return left no mount. */
		if (mount_uncertain) *mount_uncertain=1;
		return mount_failed(diagnostic,"mountSave",mountErrorCode,mount_error);
	}

	LOG("'%s/%s' mountPath (%s)", save->title_id, save->dir_name, mountDir);
	strlcpy(mount_path, save->dir_name, ORBIS_SAVE_DATA_DIRNAME_DATA_MAXSIZE);

	return 1;
}

int orbis_SaveMount(const save_entry_t *save, uint32_t mode, char* mount_path)
{
	return save_mount(save,mode,mount_path,0,0,NULL,NULL);
}
int orbis_SaveMountEmpty(const save_entry_t *save, uint32_t user, char* mount_path)
{
	return save_mount(save,ORBIS_SAVE_DATA_MOUNT_MODE_RDWR|ORBIS_SAVE_DATA_MOUNT_MODE_CREATE2|ORBIS_SAVE_DATA_MOUNT_MODE_COPY_ICON, mount_path,user,1,NULL,NULL);
}
int orbis_SaveMountChecked(const save_entry_t *save, uint32_t mode, char* mount_path, int *mount_uncertain)
{
	return save_mount(save,mode,mount_path,0,0,mount_uncertain,NULL);
}
int orbis_SaveMountEmptyChecked(const save_entry_t *save, uint32_t user, char* mount_path, int *mount_uncertain)
{
	return save_mount(save,ORBIS_SAVE_DATA_MOUNT_MODE_RDWR|ORBIS_SAVE_DATA_MOUNT_MODE_CREATE2|ORBIS_SAVE_DATA_MOUNT_MODE_COPY_ICON, mount_path,user,1,mount_uncertain,NULL);
}

int orbis_SaveMountEmptyCheckedDiagnostic(const save_entry_t *save, uint32_t user, char* mount_path,
	int *mount_uncertain, save_mount_diagnostic_t *diagnostic)
{
	return save_mount(save,ORBIS_SAVE_DATA_MOUNT_MODE_RDWR|ORBIS_SAVE_DATA_MOUNT_MODE_CREATE2|ORBIS_SAVE_DATA_MOUNT_MODE_COPY_ICON,
		mount_path,user,1,mount_uncertain,diagnostic);
}
