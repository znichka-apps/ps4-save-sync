/* Google restore adapter: stage a validated decrypted save, then use Apollo's
   normal USB save scanner and HDD copy/resign implementation. */
#include <stdio.h>
#include <string.h>
#include "saves.h"
#include "settings.h"
#include "google_restore.h"

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
    int ok=!cancel(p) && orbis_ImportStagedSave(stage,b->title,b->directory,c->user,cancel,p,&mount_blocked);
    if (mount_blocked) {
        ((google_backup*)b)->mount_blocked=1;
        snprintf(((google_backup*)b)->diagnostic,sizeof(b->diagnostic),
            "Save unmount failed after staged import. Restart the app; downloaded ZIP retained.");
    }
    return ok;
}

int google_restore_local(google_backup *b,int (*cancelled)(void*),int (*completed)(void*),void *data)
{
    restore_context c={.user=b->user,.cancel=cancelled,.finish=completed,.data=data};
    google_restore_io io={&c,cancel,absent,import_staged,finish};
    return google_restore_run(b,&io);
}
