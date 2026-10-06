/* Google restore adapter: stage a validated decrypted save, then use Apollo's
   normal USB save scanner and HDD copy/resign implementation. */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "saves.h"
#include "settings.h"
#include "google_restore.h"
#include "google_replace.h"

typedef struct {
    uint32_t user;
    int (*cancel)(void*), (*finish)(void*);
    void *data;
} restore_context;

static int cancel(void *p)
{
    restore_context *c=p;
    return c->user!=apollo_config.user_id || c->cancel(c->data);
}

static int finish(void *p)
{
    restore_context *c=p;
    return !cancel(p) && c->finish(c->data);
}

static int absent(void *p,const google_backup *b)
{
    restore_context *c=p;
    save_entry_t save={0};
    save.title_id=(char*)b->title;
    save.dir_name=(char*)b->directory;
    return cancel(p) ? -1 : orbis_SaveTargetAbsent(&save,c->user);
}

static int import_staged(void *p,const google_backup *b,const char *stage)
{
    restore_context *c=p;
    int mount_blocked=0;
    /* IDs in the decrypted SFO are used only for title/directory matching.
       Apollo's HDD copy path resigns the destination to this local user. */
    int allowed=!cancel(p);
    int32_t account_status=0;
    errno=0;
    int account_ok=allowed && account_id_refresh_live(c->user,&apollo_config.account_id,&account_status);
    int account_error=errno;
    if (b->replace_trace && allowed) {
        char detail[80];
        snprintf(detail,sizeof(detail),"result=%d native=%d errno=%d call=sceUserServiceGetNpAccountId",
            account_ok,account_status,account_ok?0:account_error);
        google_replace_phase(b->user,"import account lookup",detail);
    }
    if (allowed && !account_ok)
        snprintf(((google_backup*)b)->diagnostic,sizeof(b->diagnostic),
            "Local PS4 account ID unavailable [sceUserServiceGetNpAccountId=%d]. ZIP retained.",account_status);
    errno=0;
    int ok=allowed && account_ok && orbis_ImportStagedSave(stage,b->title,b->directory,c->user,cancel,p,&mount_blocked,
        b->replace_trace?b->user:0);
    int import_error=errno;
    if (b->replace_trace) {
        char detail[64];
        snprintf(detail,sizeof(detail),"result=%d native=%d errno=%d call=%s",ok,ok,ok?0:import_error,
            !allowed?"cancel":!account_ok?"account_id":"ImportStagedSave");
        google_replace_phase(b->user,"import adapter",detail);
    }
    if (mount_blocked) {
        ((google_backup*)b)->mount_blocked=1;
        snprintf(((google_backup*)b)->diagnostic,sizeof(b->diagnostic),
            "Save mount state uncertain after staged import. Stop save operations; ZIP retained.");
    }
    errno=import_error;
    return ok && !mount_blocked;
}

int google_restore_local(google_backup *b,int (*cancelled)(void*),int (*completed)(void*),void *data)
{
    restore_context c={.user=b->user,.cancel=cancelled,.finish=completed,.data=data};
    google_restore_io io={&c,cancel,absent,import_staged,finish};
    return google_restore_run(b,&io);
}
