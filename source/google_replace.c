/* Best-effort replacement journal. All paths are fixed names under a private
   directory; a journal is synced before any save mutation. Never erase a
   transaction's ZIPs, including after success or failed recovery. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>
#include <errno.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include "google_replace.h"
#include "restore_fs.h"

#define JOURNAL_MAGIC 0x52505347u
#define JOURNAL_VERSION 1u
enum { PREPARED=1, UPLOADED, DELETING, IMPORTING, RECOVERING, COMPLETE, RECOVERED, RECOVERY_FAILED };
typedef struct {
    uint32_t magic, version, phase, user;
    uint64_t source_size, rollback_size;
    char title[32], directory[64], game[1024];
    char source_md5[33], rollback_md5[33], source_utc[32], rollback_utc[32];
    uint32_t checksum;
} journal;

static void diagnostic(google_backup *b,const char *reason)
{
    if (b) snprintf(b->diagnostic,sizeof(b->diagnostic),"Replace %s. Both ZIPs and journal retained where available.",reason);
}
static uint32_t checksum(const journal *j)
{
    const unsigned char *p=(const void*)j; uint32_t h=2166136261u;
    for (size_t i=0;i<offsetof(journal,checksum);i++) h=(h^p[i])*16777619u;
    return h;
}
static int full_write(int fd,const void *data,size_t size)
{
    const unsigned char *p=data;
    while (size) {
        ssize_t n=write(fd,p,size);
        if (n<0 && errno==EINTR) continue;
        if (n<=0) return 0;
        p+=n; size-=(size_t)n;
    }
    return 1;
}
static int sync_directory(const char *path)
{
    int fd=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
    if (fd<0) return 0;
    int ok=fsync(fd)==0;
    if (close(fd)) ok=0;
    return ok;
}
static int valid_md5(const char *s)
{
    if (strnlen(s,33)!=32) return 0;
    for (unsigned i=0;i<32;i++) if (!isxdigit((unsigned char)s[i])) return 0;
    return 1;
}
static int valid_record(const journal *j)
{
    if (j->magic!=JOURNAL_MAGIC || j->version!=JOURNAL_VERSION ||
        j->phase<PREPARED || j->phase>RECOVERY_FAILED || !j->user ||
        !j->source_size || !j->rollback_size || checksum(j)!=j->checksum ||
        !memchr(j->title,0,sizeof(j->title)) || !memchr(j->directory,0,sizeof(j->directory)) ||
        !memchr(j->game,0,sizeof(j->game)) || !memchr(j->source_utc,0,sizeof(j->source_utc)) ||
        !memchr(j->rollback_utc,0,sizeof(j->rollback_utc)) ||
        strlen(j->title)!=9 || strncmp(j->title,"CUSA",4) ||
        !*j->directory || strlen(j->directory)>=32 || !valid_md5(j->source_md5) ||
        !valid_md5(j->rollback_md5)) return 0;
    for (unsigned i=4;i<9;i++) if (!isdigit((unsigned char)j->title[i])) return 0;
    for (const unsigned char *p=(const unsigned char*)j->directory;*p;p++)
        if (*p<32 || *p==127 || *p=='/' || *p=='\\' || *p==':') return 0;
    return 1;
}
static int private_root(int create)
{
    struct stat st; int native;
    char root[sizeof(GOOGLE_REPLACE_ROOT)];
    memcpy(root,GOOGLE_REPLACE_ROOT,sizeof(root));
    size_t length=strlen(root);
    if (!length || root[length-1]!='/') return 0;
    root[length-1]=0; /* lstat must not follow a symlink via trailing slash. */
    if (!restore_fs_init(&native)) return 0;
    int created=0;
    if (create) {
        if (!mkdir(root,0700)) created=1;
        else if (errno!=EEXIST) return 0;
    }
    if (restore_fs_lstat(root,&st)) return 0;
    if (!S_ISDIR(st.st_mode) || (st.st_mode&077) || st.st_uid!=geteuid()) return 0;
    if (created) {
        char *slash=strrchr(root,'/');
        if (!slash) return 0;
        *slash=0;
        if (!sync_directory(root)) return 0;
        *slash='/';
    }
    return 1;
}
static int transaction_path(char *out,size_t cap,const char *dir,const char *leaf)
{
    int n=snprintf(out,cap,"%s/%s",dir,leaf);
    return n>0 && (size_t)n<cap;
}
static int read_journal(const char *dir,journal *j)
{
    char path[288]; struct stat st;
    if (!transaction_path(path,sizeof(path),dir,"journal") ||
        restore_fs_lstat(dir,&st) || !S_ISDIR(st.st_mode) || (st.st_mode&077) || st.st_uid!=geteuid()) return -1;
    int fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    if (fd<0) return errno==ENOENT?0:-1;
    int ok=fstat(fd,&st)==0 && S_ISREG(st.st_mode) && st.st_nlink==1 && st.st_uid==geteuid() && st.st_size==(off_t)sizeof(*j);
    if (ok) ok=read(fd,j,sizeof(*j))==(ssize_t)sizeof(*j) && valid_record(j);
    if (close(fd)) ok=0;
    return ok?1:-1;
}
static int write_journal(const char *dir,journal *j)
{
    char temp[288],dest[288];
    if (!transaction_path(temp,sizeof(temp),dir,"journal.tmp") ||
        !transaction_path(dest,sizeof(dest),dir,"journal")) return 0;
    j->checksum=checksum(j);
    int fd=open(temp,O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW,0600);
    if (fd<0) return 0;
    struct stat st; int ok=fstat(fd,&st)==0 && S_ISREG(st.st_mode) && st.st_nlink==1 &&
        st.st_uid==geteuid() && !(st.st_mode&077) && full_write(fd,j,sizeof(*j)) && fsync(fd)==0;
    if (close(fd)) ok=0;
    if (!ok || rename(temp,dest)) return 0;
    return sync_directory(dir);
}
static void as_backup(const journal *j,const char *dir,int rollback,google_backup *b)
{
    memset(b,0,sizeof(*b));
    snprintf(b->game,sizeof(b->game),"%s",j->game);
    snprintf(b->title,sizeof(b->title),"%s",j->title);
    snprintf(b->directory,sizeof(b->directory),"%s",j->directory);
    snprintf(b->temp_dir,sizeof(b->temp_dir),"%s",dir);
    transaction_path(b->archive,sizeof(b->archive),dir,rollback?"rollback.zip":"source.zip");
    snprintf(b->md5,sizeof(b->md5),"%s",rollback?j->rollback_md5:j->source_md5);
    snprintf(b->utc,sizeof(b->utc),"%s",rollback?j->rollback_utc:j->source_utc);
    b->size=rollback?j->rollback_size:j->source_size;
    b->user=j->user;
}
static int copy_archive(const char *from,const char *dir,const char *leaf,uint64_t expected)
{
    char to[288]; struct stat st;
    if (!transaction_path(to,sizeof(to),dir,leaf)) return 0;
    int in=open(from,O_RDONLY|O_NOFOLLOW|O_NONBLOCK); if (in<0) return 0;
    int ok=fstat(in,&st)==0 && S_ISREG(st.st_mode) && st.st_nlink==1 && st.st_size>=0 && (uint64_t)st.st_size==expected;
    int out=-1;
    if (ok) { out=open(to,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600); ok=out>=0; }
    unsigned char buf[16384]; uint64_t count=0;
    while (ok) { ssize_t n=read(in,buf,sizeof(buf)); if (n<0 && errno==EINTR) continue;
        if (n<0) { ok=0; break; } if (!n) break;
        if ((uint64_t)n>expected-count || !full_write(out,buf,(size_t)n)) { ok=0; break; }
        count+=(uint64_t)n;
    }
    if (ok) ok=count==expected && fsync(out)==0;
    if (out>=0 && close(out)) ok=0;
    if (close(in)) ok=0;
    if (!ok) return 0;
    return sync_directory(dir);
}
static int find_pending(uint32_t user,char *path,size_t cap,journal *found)
{
    int native;
    if (!restore_fs_init(&native)) return -1;
    struct stat root_stat;
    char root[sizeof(GOOGLE_REPLACE_ROOT)];
    memcpy(root,GOOGLE_REPLACE_ROOT,sizeof(root));
    size_t length=strlen(root);
    if (!length || root[length-1]!='/') return -1;
    root[length-1]=0;
    if (restore_fs_lstat(root,&root_stat) && errno==ENOENT) return 0;
    if (!private_root(0)) return -1;
    DIR *d=opendir(GOOGLE_REPLACE_ROOT); if (!d) return -1;
    struct dirent *e; int count=0;
    while ((e=readdir(d))) {
        if (strncmp(e->d_name,"tx-",3)) continue;
        if (strchr(e->d_name,'/') || strlen(e->d_name)>32) { count=-1; break; }
        char candidate[256]; int n=snprintf(candidate,sizeof(candidate),"%s%s",GOOGLE_REPLACE_ROOT,e->d_name);
        if (n<0 || (size_t)n>=sizeof(candidate)) { count=-1; break; }
        journal j; int r=read_journal(candidate,&j);
        if (r<0) { count=-1; break; }
        if (r==1 && j.user==user && j.phase!=COMPLETE && j.phase!=RECOVERED) {
            if (++count>1) { count=-1; break; }
            snprintf(path,cap,"%s",candidate); *found=j;
        }
    }
    if (closedir(d)) count=-1;
    return count;
}
int google_replace_pending(uint32_t user,google_backup *source)
{
    char path[256]={0}; journal j;
    int result=find_pending(user,path,sizeof(path),&j);
    if (result==1 && source) as_backup(&j,path,0,source);
    return result;
}
static int never_cancel(void *unused) { (void)unused; return 0; }
static int recover_record(const char *dir,journal *j,const google_replace_io *io)
{
    google_backup rollback;
    as_backup(j,dir,1,&rollback);
    if (google_restore_validate(&rollback,never_cancel,NULL)!=GOOGLE_UPLOAD_SUCCESS) return GOOGLE_REPLACE_NEEDS_RECOVERY;
    if (j->phase==PREPARED || j->phase==UPLOADED) {
        int present=io->present(io->data,&rollback);
        if (present==1) {
            j->phase=RECOVERED;
            return write_journal(dir,j)?GOOGLE_REPLACE_ROLLED_BACK:GOOGLE_REPLACE_NEEDS_RECOVERY;
        }
        if (present<0) return GOOGLE_REPLACE_NEEDS_RECOVERY;
    }
    j->phase=RECOVERING;
    if (!write_journal(dir,j)) return GOOGLE_REPLACE_NEEDS_RECOVERY;
    int present=io->present(io->data,&rollback);
    if (present<0 || (present==1 && !io->delete_target(io->data,&rollback))) goto failed;
    if (!io->import_archive(io->data,&rollback,1)) goto failed;
    j->phase=RECOVERED;
    return write_journal(dir,j)?GOOGLE_REPLACE_ROLLED_BACK:GOOGLE_REPLACE_NEEDS_RECOVERY;
failed:
    j->phase=RECOVERY_FAILED;
    (void)write_journal(dir,j);
    return GOOGLE_REPLACE_NEEDS_RECOVERY;
}
int google_replace_recover(uint32_t user,const google_replace_io *io)
{
    char dir[256]={0}; journal j;
    if (!io || !io->present || !io->delete_target || !io->import_archive ||
        find_pending(user,dir,sizeof(dir),&j)!=1) return GOOGLE_REPLACE_FAILED;
    return recover_record(dir,&j,io);
}
int google_replace_start(google_backup *source,const google_replace_io *io)
{
    if (!source || !io || !io->cancelled || !io->present || !io->stage_backup ||
        !io->upload_backup || !io->commit || !io->delete_target || !io->import_archive) return GOOGLE_REPLACE_FAILED;
    source->diagnostic[0]=0;
    int pending=google_replace_pending(source->user,NULL);
    if (pending!=0) { diagnostic(source,pending<0?"journal is unsafe":"recovery is pending"); return GOOGLE_REPLACE_FAILED; }
    if (google_restore_validate(source,io->cancelled,io->data)!=GOOGLE_UPLOAD_SUCCESS) return
        io->cancelled(io->data)?GOOGLE_REPLACE_CANCELLED:GOOGLE_REPLACE_FAILED;
    if (io->present(io->data,source)!=1) { diagnostic(source,"target is absent or cannot be confirmed"); return GOOGLE_REPLACE_FAILED; }
    if (io->cancelled(io->data)) return GOOGLE_REPLACE_CANCELLED;
    google_backup rollback={0};
    snprintf(rollback.game,sizeof(rollback.game),"%s",source->game);
    snprintf(rollback.title,sizeof(rollback.title),"%s",source->title);
    snprintf(rollback.directory,sizeof(rollback.directory),"%s",source->directory);
    rollback.user=source->user;
    if (!io->stage_backup(io->data,&rollback)) {
        diagnostic(source,rollback.mount_blocked?"backup unmount failed; restart app":"rollback backup could not be staged");
        source->mount_blocked=rollback.mount_blocked;
        return io->cancelled(io->data)?GOOGLE_REPLACE_CANCELLED:GOOGLE_REPLACE_FAILED;
    }
    if (google_restore_validate(&rollback,io->cancelled,io->data)!=GOOGLE_UPLOAD_SUCCESS) {
        diagnostic(source,"rollback ZIP did not pass validation");
        return io->cancelled(io->data)?GOOGLE_REPLACE_CANCELLED:GOOGLE_REPLACE_FAILED;
    }
    if (io->cancelled(io->data)) return GOOGLE_REPLACE_CANCELLED;
    if (!private_root(1)) { diagnostic(source,"private journal root unavailable"); return GOOGLE_REPLACE_FAILED; }
    char dir[256]; snprintf(dir,sizeof(dir),GOOGLE_REPLACE_ROOT "tx-XXXXXX");
    if (!mkdtemp(dir)) { diagnostic(source,"transaction directory unavailable"); return GOOGLE_REPLACE_FAILED; }
    if (!sync_directory(GOOGLE_REPLACE_ROOT)) { diagnostic(source,"transaction directory could not be made durable"); return GOOGLE_REPLACE_FAILED; }
    if (!copy_archive(source->archive,dir,"source.zip",source->size) ||
        !copy_archive(rollback.archive,dir,"rollback.zip",rollback.size)) {
        diagnostic(source,"durable ZIP copy failed"); return GOOGLE_REPLACE_FAILED;
    }
    journal j={0}; j.magic=JOURNAL_MAGIC; j.version=JOURNAL_VERSION; j.phase=PREPARED; j.user=source->user;
    j.source_size=source->size; j.rollback_size=rollback.size;
    snprintf(j.title,sizeof(j.title),"%s",source->title);
    snprintf(j.directory,sizeof(j.directory),"%s",source->directory);
    snprintf(j.game,sizeof(j.game),"%s",source->game);
    snprintf(j.source_md5,sizeof(j.source_md5),"%s",source->md5);
    snprintf(j.rollback_md5,sizeof(j.rollback_md5),"%s",rollback.md5);
    snprintf(j.source_utc,sizeof(j.source_utc),"%s",source->utc);
    snprintf(j.rollback_utc,sizeof(j.rollback_utc),"%s",rollback.utc);
    google_backup source_copy,rollback_copy;
    as_backup(&j,dir,0,&source_copy); as_backup(&j,dir,1,&rollback_copy);
    if (google_restore_validate(&source_copy,io->cancelled,io->data)!=GOOGLE_UPLOAD_SUCCESS ||
        google_restore_validate(&rollback_copy,io->cancelled,io->data)!=GOOGLE_UPLOAD_SUCCESS ||
        !write_journal(dir,&j)) { diagnostic(source,"durable ZIP or journal verification failed"); return GOOGLE_REPLACE_FAILED; }
    if (io->cancelled(io->data)) return GOOGLE_REPLACE_CANCELLED;
    if (io->upload_backup(io->data,&rollback_copy)!=GOOGLE_UPLOAD_SUCCESS) {
        diagnostic(source,"rollback upload was not verified; target unchanged"); return GOOGLE_REPLACE_FAILED;
    }
    j.phase=UPLOADED;
    if (!write_journal(dir,&j) || io->present(io->data,source)!=1) {
        diagnostic(source,"commit preflight failed; target unchanged"); return GOOGLE_REPLACE_FAILED;
    }
    if (!io->commit(io->data)) {
        diagnostic(source,"cancelled or commit refused before target change");
        return io->cancelled(io->data)?GOOGLE_REPLACE_CANCELLED:GOOGLE_REPLACE_FAILED;
    }
    j.phase=DELETING;
    if (!write_journal(dir,&j)) { diagnostic(source,"journal sync failed; target unchanged"); return GOOGLE_REPLACE_FAILED; }
    if (!io->delete_target(io->data,source)) goto rollback_attempt;
    j.phase=IMPORTING;
    if (!write_journal(dir,&j) || !io->import_archive(io->data,&source_copy,0)) goto rollback_attempt;
    j.phase=COMPLETE;
    if (!write_journal(dir,&j)) { diagnostic(source,"save imported but completion journal failed; recovery pending"); return GOOGLE_REPLACE_NEEDS_RECOVERY; }
    return GOOGLE_REPLACE_SUCCESS;
rollback_attempt:
    if (source_copy.mount_blocked) {
        source->mount_blocked=1;
        diagnostic(source,"import unmount failed; restart before recovery");
        return GOOGLE_REPLACE_NEEDS_RECOVERY;
    }
    {
        int recovered=recover_record(dir,&j,io);
        diagnostic(source,recovered==GOOGLE_REPLACE_ROLLED_BACK?"import failed; prior save restored":"import failed; recovery pending");
        return recovered;
    }
}
