/* Local-only restore: no HTTP downloader, no archive-controlled destinations. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <sys/stat.h>
#include <zip.h>
#include "google_restore.h"

static unsigned le16(const unsigned char *p) { return p[0] | (unsigned)p[1]<<8; }
static uint32_t le32(const unsigned char *p) { return le16(p) | (uint32_t)le16(p+2)<<16; }
/* Bound every index before Apollo's SFO parser/patcher sees these bytes. */
static int sfo_check(const unsigned char *p, size_t n, const google_backup *b, uint32_t *blocks)
{
    if (n<20 || le32(p)!=0x46535000 || le32(p+4)!=0x101) return 0;
    unsigned keys=le32(p+8), values=le32(p+12), count=le32(p+16), found=0, allocated=0;
    if (!count || count>256 || keys<20+count*16 || keys>=values || values>n) return 0;
    for (unsigned i=0;i<count;i++) {
        const unsigned char *e=p+20+i*16;
        unsigned k=le16(e), format=le16(e+2), len=le32(e+4), max=le32(e+8), off=le32(e+12);
        if (k>=values-keys || !memchr(p+keys+k,0,values-keys-k) || !max || len>max ||
            off>n-values || max>n-values-off) return 0;
        const char *key=(const char*)p+keys+k; const unsigned char *v=p+values+off;
        if (strlen(key)>63 || max>n-values-allocated) return 0;
        allocated+=max;
        for (unsigned j=0;j<i;j++) {
            const unsigned char *prev=p+20+j*16;
            if (!strcmp(key,(const char*)p+keys+le16(prev))) return 0;
        }
        if (format==0x204 && (!len || !memchr(v,0,len))) return 0;
        if (!strcmp(key,"TITLE_ID") || !strcmp(key,"SAVEDATA_DIRECTORY")) {
            const char *expected=!strcmp(key,"TITLE_ID")?b->title:b->directory;
            if (format!=0x204 || len<strlen(expected)+1 || strcmp((const char*)v,expected)) return 0;
            found|=!strcmp(key,"TITLE_ID")?1:2;
        } else if (!strcmp(key,"SAVEDATA_BLOCKS")) {
            if (format!=0x404 || len!=4 || max!=4) return 0;
            *blocks=le32(v);
            /* PS4 save blocks are 32 KiB. Bound untrusted allocation requests. */
            if (*blocks<96 || *blocks>524288) return 0;
            found|=4;
        } else if (!strcmp(key,"ACCOUNT_ID")) {
            if (len!=8 || max!=8) return 0;
            found|=8;
        } else if (!strcmp(key,"PARAMS")) {
            if (len<0x54 || max<0x54) return 0;
            if (!memchr(v+0x2c,0,16) || strcmp((const char*)v+0x2c,b->title)) return 0;
            found|=16;
        } else if (!strcmp(key,"MAINTITLE") || !strcmp(key,"SUBTITLE") || !strcmp(key,"DETAIL")) {
            if (format!=0x204) return 0;
            found|=!strcmp(key,"MAINTITLE")?32:!strcmp(key,"SUBTITLE")?64:128;
        } else if (!strcmp(key,"SAVEDATA_LIST_PARAM")) {
            if (len!=4 || max!=4 || format!=0x404) return 0;
            found|=256;
        }
    }
    return found==511;
}
static int read_sfo(zip_t *z, const google_backup *b, uint32_t *blocks)
{
    char name[128]; snprintf(name,sizeof(name),"%s/sce_sys/param.sfo",b->directory);
    zip_stat_t st;
    if (zip_stat(z,name,0,&st) || !st.size || st.size>1024*1024) return 0;
    unsigned char *p=malloc(st.size); if (!p) return 0;
    zip_file_t *f=zip_fopen(z,name,0); int ok=0;
    if (f) {
        ok=zip_fread(f,p,st.size)==(zip_int64_t)st.size && sfo_check(p,st.size,b,blocks);
        if (zip_fclose(f)) ok=0;
    }
    free(p); return ok;
}
/* Walk under the mount with directory descriptors; never follow symlinks. */
static int destination(int root, const char *name, int directory)
{
    char path[1024]; if (!*name || strlen(name)>=sizeof(path)) return -1;
    strcpy(path,name); int parent=dup(root); if (parent<0) return -1;
    char *part=path;
    while (*part) {
        char *slash=strchr(part,'/'); if (slash) *slash=0;
        if (!*part || !strcmp(part,".") || !strcmp(part,"..") || strchr(part,'\\') || strchr(part,':')) break;
        if (slash || directory) {
            if (mkdirat(parent,part,0700) && errno!=EEXIST) break;
            int next=openat(parent,part,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);
            if (next<0) break;
            if (close(parent)) { close(next); return -1; }
            parent=next;
            if (!slash || !slash[1]) return parent;
            part=slash+1;
        } else {
            int fd=openat(parent,part,O_WRONLY|O_CREAT|O_NOFOLLOW|O_NONBLOCK,0600);
            struct stat st;
            if (fd>=0 && (fstat(fd,&st) || !S_ISREG(st.st_mode) || ftruncate(fd,0))) { close(fd); fd=-1; }
            if (close(parent)) { if (fd>=0) close(fd); return -1; }
            return fd;
        }
    }
    close(parent); return -1;
}
static int copy_entries(zip_t *z, const google_backup *b, const char *mount, const google_restore_io *io)
{
    char path[256]; if (strlen(mount)>=sizeof(path)) return 0;
    strcpy(path,mount); size_t len=strlen(path);
    while (len>1 && path[len-1]=='/') path[--len]=0;
    int root=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW); if (root<0) return 0;
    int ok=1; size_t prefix=strlen(b->directory)+1;
    zip_int64_t count=zip_get_num_entries(z,0);
    for (zip_uint64_t i=0;ok && i<(zip_uint64_t)count;i++) {
        zip_stat_t st;
        if (io->cancelled(io->data) || zip_stat_index(z,i,0,&st)) { ok=0; break; }
        if (!st.name || strlen(st.name)<prefix || strncmp(st.name,b->directory,prefix-1) || st.name[prefix-1]!='/') { ok=0; break; }
        const char *name=st.name+prefix; if (!*name) continue;
        int dir=name[strlen(name)-1]=='/';
        int fd=destination(root,name,dir); if (fd<0) { ok=0; break; }
        if (!dir) {
            zip_file_t *f=zip_fopen_index(z,i,0); uint64_t total=0;
            unsigned char buffer[16384]; zip_int64_t n=0;
            if (!f) ok=0;
            while (ok && (n=zip_fread(f,buffer,sizeof(buffer)))>0) {
                if (io->cancelled(io->data) || (uint64_t)n>st.size-total) { ok=0; break; }
                size_t written=0;
                while (written<(size_t)n) {
                    ssize_t w=write(fd,buffer+written,n-written);
                    if (w<0 && errno==EINTR) continue;
                    if (w<=0) { ok=0; break; }
                    written+=w;
                }
                total+=written;
            }
            if (n<0 || total!=st.size) ok=0;
            if (f && zip_fclose(f)) ok=0;
            if (fsync(fd)) ok=0;
        }
        if (close(fd)) ok=0;
    }
    if (close(root)) ok=0;
    return ok;
}
int google_restore_run(google_backup *b, const google_restore_io *io)
{
    int result=GOOGLE_UPLOAD_FAILED, mounted=0, fd=-1; zip_t *z=NULL;
    uint32_t blocks=0; char mount[256], expected[288]; struct stat before, after;
    b->diagnostic[0]=0;
#define FAIL(s) do { snprintf(b->diagnostic,sizeof(b->diagnostic),"%s",s); goto done; } while (0)
    if (strlen(b->title)!=9 || strncmp(b->title,"CUSA",4) || !*b->directory || strlen(b->directory)>=32 ||
        !strcmp(b->directory,".") || !strcmp(b->directory,"..")) FAIL("Unsupported title/save directory; no save written.");
    for (unsigned i=4;i<9;i++) if (!isdigit((unsigned char)b->title[i])) FAIL("Invalid title ID.");
    for (const unsigned char *p=(const unsigned char*)b->directory; *p; p++)
        if (*p<32 || *p==127 || *p=='/' || *p=='\\' || *p==':') FAIL("Invalid save directory.");
    /* Only a private download job's fixed basename is accepted. */
    if (strncmp(b->temp_dir,GOOGLE_BACKUP_CACHE "drive-",strlen(GOOGLE_BACKUP_CACHE "drive-")) ||
        strchr(b->temp_dir+strlen(GOOGLE_BACKUP_CACHE),'/')) FAIL("Invalid private archive path.");
    snprintf(expected,sizeof(expected),"%s/backup.zip",b->temp_dir);
    if (strcmp(expected,b->archive)) FAIL("Invalid private archive path.");
    if (lstat(b->temp_dir,&after) || !S_ISDIR(after.st_mode)) FAIL("Invalid private archive directory.");
    fd=open(b->archive,O_RDONLY|O_NOFOLLOW);
    if (fd<0 || fstat(fd,&before) || !S_ISREG(before.st_mode)) FAIL("Archive unavailable.");
    google_backup hash=*b;
    if (!google_backup_hash(&hash,io->cancelled,io->data) || hash.size!=b->size || strcmp(hash.md5,b->md5) ||
        !google_download_zip(b,io->cancelled,io->data)) FAIL("Archive revalidation failed; no save written.");
    if (lstat(b->archive,&after) || before.st_dev!=after.st_dev || before.st_ino!=after.st_ino ||
        before.st_size!=after.st_size || before.st_mtime!=after.st_mtime) FAIL("Archive changed; no save written.");
    int error; int zipfd=dup(fd);
    if (zipfd<0) FAIL("Archive unavailable.");
    z=zip_fdopen(zipfd,ZIP_RDONLY|ZIP_CHECKCONS,&error);
    if (!z) { close(zipfd); FAIL("Archive unavailable."); }
    if (!read_sfo(z,b,&blocks)) FAIL("SFO invalid or disagrees with selected title/save directory; no save written.");
    if (io->cancelled(io->data)) goto done;
    int absent=io->absent(io->data,b);
    if (absent!=1) FAIL(absent==0?"Save already exists for this PS4 user. Restore refused; never overwritten.":"Cannot confirm empty target; restore refused.");
    if (io->cancelled(io->data)) goto done;
    if (!io->create_mount(io->data,b,blocks,mount,sizeof(mount))) FAIL("Create/mount failed. A partial target may remain; inspect it manually.");
    mounted=1;
    if (!copy_entries(z,b,mount,io)) FAIL("Copy failed or cancelled. Partial target retained for manual review.");
    if (io->cancelled(io->data)) goto done;
    if (!io->ownership(io->data,mount)) FAIL("Ownership patch failed. Partial target retained for manual review.");
    if (io->cancelled(io->data)) goto done;
    if (!io->details(io->data,b,mount)) FAIL("Save metadata update failed. Partial target retained for manual review.");
    result=GOOGLE_UPLOAD_SUCCESS;
done:
    if (mounted && !io->unmount(io->data)) {
        b->mount_blocked=1; result=GOOGLE_UPLOAD_FAILED;
        snprintf(b->diagnostic,sizeof(b->diagnostic),"Unmount failed. Restart the app; target retained. Restore NOT successful.");
    }
    if (!b->mount_blocked && io->cancelled(io->data)) {
        result=GOOGLE_UPLOAD_CANCELLED;
        snprintf(b->diagnostic,sizeof(b->diagnostic),"Restore cancelled.%s",mounted?" Partial target retained; inspect manually.":" No target created.");
    }
    if (result==GOOGLE_UPLOAD_SUCCESS && !io->finish(io->data)) {
        result=GOOGLE_UPLOAD_CANCELLED;
        snprintf(b->diagnostic,sizeof(b->diagnostic),"Restore cancelled at completion. Target retained; inspect manually.");
    }
    if (z) zip_discard(z);
    if (fd>=0 && close(fd)) { result=GOOGLE_UPLOAD_FAILED; snprintf(b->diagnostic,sizeof(b->diagnostic),"Archive close failed; target retained."); }
    return result;
#undef FAIL
}
