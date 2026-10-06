#include <string.h>
#include "saves.h"
#include "settings.h"
#include "google_replace.h"

typedef struct {
    uint32_t user;
    const google_upload_io *network;
    int (*cancel)(void *), (*commit)(void *);
    void *data;
    int *mount_blocked;
} replace_context;
static int cancelled(void *p)
{
    replace_context *c=p;
    return c->user!=apollo_config.user_id || (c->cancel && c->cancel(c->data));
}
static int never_cancel(void *p) { (void)p; return 0; }
static int completed(void *p) { (void)p; return 1; }
static int commit(void *p)
{
    replace_context *c=p;
    return !cancelled(p) && c->commit && c->commit(c->data);
}
static save_entry_t entry(const google_backup *b)
{
    save_entry_t s={0};
    s.title_id=(char*)b->title; s.dir_name=(char*)b->directory;
    s.flags=SAVE_FLAG_PS4|SAVE_FLAG_HDD; s.type=FILE_TYPE_PS4;
    return s;
}
static int present(void *p,const google_backup *b)
{
    replace_context *c=p;
    if (c->user!=apollo_config.user_id || b->user!=c->user) return -1;
    save_entry_t s=entry(b);
    int absent=orbis_SaveTargetAbsent(&s,c->user);
    return absent==0?1:absent==1?0:-1;
}
static int stage_backup(void *p,google_backup *b)
{
    replace_context *c=p;
    b->replace_trace=1;
    int ok=!cancelled(p) && google_backup_stage(b,cancelled,p);
    b->replace_trace=0;
    if (b->mount_blocked && c->mount_blocked) *c->mount_blocked=1;
    return ok;
}
static int upload_backup(void *p,const google_backup *b)
{
    replace_context *c=p;
    return c->network?google_upload_run(b,c->network):GOOGLE_UPLOAD_FAILED;
}
static int delete_target(void *p,const google_backup *b)
{
    replace_context *c=p;
    if (c->user!=apollo_config.user_id || b->user!=c->user) return 0;
    save_entry_t s=entry(b);
    return orbis_SaveDelete(&s) && orbis_SaveTargetAbsent(&s,c->user)==1;
}
static int import_archive(void *p,google_backup *b,int recovery)
{
    replace_context *c=p;
    if (c->user!=apollo_config.user_id || b->user!=c->user) return 0;
    b->replace_trace=1;
    int result=google_restore_local(b,recovery?never_cancel:cancelled,completed,p);
    b->replace_trace=0;
    if (b->mount_blocked && c->mount_blocked) *c->mount_blocked=1;
    return result==GOOGLE_UPLOAD_SUCCESS;
}
int google_replace_local(google_backup *b,const google_upload_io *network,
                         int (*cancel)(void *),int (*begin)(void *),void *data)
{
    int blocked=0;
    replace_context c={b->user,network,cancel,begin,data,&blocked};
    google_replace_io io={&c,cancelled,present,stage_backup,upload_backup,commit,delete_target,import_archive};
    int result=google_replace_start(b,&io);
    if (blocked) b->mount_blocked=1;
    return result;
}
int google_replace_recover_local(uint32_t user,int *mount_blocked)
{
    replace_context c={.user=user,.mount_blocked=mount_blocked};
    google_replace_io io={.data=&c,.present=present,.delete_target=delete_target,.import_archive=import_archive};
    if (mount_blocked) *mount_blocked=0;
    return google_replace_recover(user,&io);
}
