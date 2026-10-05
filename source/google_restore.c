/* Local-only restore: no HTTP downloader, no archive-controlled destinations. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
#include <sys/stat.h>
#include <dirent.h>
#include <zip.h>
#include <mbedtls/md.h>
#include "google_restore.h"
#include "restore_fs.h"

/* Stable descriptor metadata, including sub-second mutations. */
static int same(const struct stat *a, const struct stat *b)
{
    return a->st_dev==b->st_dev && a->st_ino==b->st_ino && a->st_mode==b->st_mode &&
        a->st_uid==b->st_uid && a->st_size==b->st_size &&
        a->st_mtim.tv_sec==b->st_mtim.tv_sec && a->st_mtim.tv_nsec==b->st_mtim.tv_nsec &&
        a->st_ctim.tv_sec==b->st_ctim.tv_sec && a->st_ctim.tv_nsec==b->st_ctim.tv_nsec;
}
static int same_directory(const struct stat *a,const struct stat *b)
{
    return a->st_dev==b->st_dev && a->st_ino==b->st_ino && a->st_mode==b->st_mode && a->st_uid==b->st_uid;
}
static int safe_lstat(const char *path,struct stat *st) { return restore_fs_lstat(path,st); }

static int hash_fd(int fd, const google_backup *b, const google_restore_io *io, int *code)
{
    unsigned char buffer[16384], digest[16]; char hex[33]; uint64_t size=0;
    mbedtls_md_context_t md; mbedtls_md_init(&md);
    *code=mbedtls_md_setup(&md,mbedtls_md_info_from_type(MBEDTLS_MD_MD5),0);
    if (!*code) *code=mbedtls_md_starts(&md);
    int ok=!*code && lseek(fd,0,SEEK_SET)==0;
    while (ok) {
        ssize_t n=read(fd,buffer,sizeof(buffer));
        if (n<0 && errno==EINTR) continue;
        if (n<0 || io->cancelled(io->data)) { ok=0; break; }
        if (!n) break;
        if ((uint64_t)n>b->size-size) { ok=0; break; }
        size+=n; *code=mbedtls_md_update(&md,buffer,n); if (*code) ok=0;
    }
    if (ok) { *code=mbedtls_md_finish(&md,digest); if (*code) ok=0; }
    mbedtls_md_free(&md);
    if (!ok || size!=b->size) return 0;
    for (unsigned i=0;i<16;i++) snprintf(hex+i*2,3,"%02x",digest[i]);
    return !strcmp(hex,b->md5);
}

static unsigned le16(const unsigned char *p) { return p[0] | (unsigned)p[1]<<8; }
static uint32_t le32(const unsigned char *p) { return le16(p) | (uint32_t)le16(p+2)<<16; }
static void sfo_failure(char *out, size_t cap, const char *check, const char *field)
{
    if (out && cap) snprintf(out,cap,"SFO check=%s field=%s",check,field);
}
static const char *sfo_field(const char *key)
{
    static const char *known[]={"TITLE_ID","SAVEDATA_DIRECTORY","SAVEDATA_BLOCKS","ACCOUNT_ID",
        "PARAMS","MAINTITLE","SUBTITLE","DETAIL","SAVEDATA_LIST_PARAM"};
    for (unsigned i=0;i<sizeof(known)/sizeof(known[0]);i++) if (!strcmp(key,known[i])) return known[i];
    return "unrecognized field";
}
/* Bound every index before Apollo's SFO parser/patcher sees these bytes. */
static int sfo_check(const unsigned char *p, size_t n, const google_backup *b, uint32_t *blocks,
    char *failure, size_t failure_cap)
{
    if (n<20) { sfo_failure(failure,failure_cap,"header too short","SFO header"); return 0; }
    if (le32(p)!=0x46535000) { sfo_failure(failure,failure_cap,"invalid magic","SFO header"); return 0; }
    if (le32(p+4)!=0x101) { sfo_failure(failure,failure_cap,"unsupported version","SFO header"); return 0; }
    unsigned keys=le32(p+8), values=le32(p+12), count=le32(p+16), found=0, allocated=0;
    if (!count || count>256 || keys<20+count*16 || keys>=values || values>n) {
        sfo_failure(failure,failure_cap,"invalid key/value table offsets or entry count","SFO header"); return 0;
    }
    for (unsigned i=0;i<count;i++) {
        const unsigned char *e=p+20+i*16;
        unsigned k=le16(e), format=le16(e+2), len=le32(e+4), max=le32(e+8), off=le32(e+12);
        char indexed_field[48]; snprintf(indexed_field,sizeof(indexed_field),"index entry %u",i);
        if (k>=values-keys || !memchr(p+keys+k,0,values-keys-k)) {
            sfo_failure(failure,failure_cap,"invalid or unterminated key offset",indexed_field); return 0;
        }
        const char *key=(const char*)p+keys+k;
        const char *field=sfo_field(key);
        if (!max || len>max || off>n-values || max>n-values-off) {
            sfo_failure(failure,failure_cap,"invalid value length or offset",field); return 0;
        }
        if (strlen(key)>63) { sfo_failure(failure,failure_cap,"key exceeds 63 bytes",field); return 0; }
        if (allocated>n-values || max>n-values-allocated) {
            sfo_failure(failure,failure_cap,"aggregate value lengths exceed data table",field); return 0;
        }
        allocated+=max;
        const unsigned char *v=p+values+off;
        for (unsigned j=0;j<i;j++) {
            const unsigned char *prev=p+20+j*16;
            if (!strcmp(key,(const char*)p+keys+le16(prev))) {
                sfo_failure(failure,failure_cap,"duplicate key",field); return 0;
            }
        }
        /* String terminators may follow the declared length within the allocation. */
        if (format==0x204 && (!len || !memchr(v,0,max))) {
            sfo_failure(failure,failure_cap,"unterminated string value",field); return 0;
        }
        if (!strcmp(key,"TITLE_ID") || !strcmp(key,"SAVEDATA_DIRECTORY")) {
            const char *expected=!strcmp(key,"TITLE_ID")?b->title:b->directory;
            if (format!=0x204) { sfo_failure(failure,failure_cap,"expected string format",field); return 0; }
            if (len<strlen(expected)+1 || strcmp((const char*)v,expected)) {
                sfo_failure(failure,failure_cap,"does not match selected backup",field); return 0;
            }
            found|=!strcmp(key,"TITLE_ID")?1:2;
        } else if (!strcmp(key,"SAVEDATA_BLOCKS")) {
            if (format!=0x404 || len!=4 || max!=4) { sfo_failure(failure,failure_cap,"expected 4-byte integer",field); return 0; }
            *blocks=le32(v);
            /* PS4 save blocks are 32 KiB. Bound untrusted allocation requests. */
            if (*blocks<96 || *blocks>524288) { sfo_failure(failure,failure_cap,"block count outside supported range",field); return 0; }
            found|=4;
        } else if (!strcmp(key,"ACCOUNT_ID")) {
            if (len!=8 || max!=8) { sfo_failure(failure,failure_cap,"expected 8-byte value",field); return 0; }
            found|=8;
        } else if (!strcmp(key,"PARAMS")) {
            if (len<0x54 || max<0x54) { sfo_failure(failure,failure_cap,"value shorter than title identity structure",field); return 0; }
            if (!memchr(v+0x2c,0,16) || strcmp((const char*)v+0x2c,b->title)) {
                sfo_failure(failure,failure_cap,"embedded title ID does not match selected backup",field); return 0;
            }
            found|=16;
        } else if (!strcmp(key,"MAINTITLE") || !strcmp(key,"SUBTITLE") || !strcmp(key,"DETAIL")) {
            if (format!=0x204) { sfo_failure(failure,failure_cap,"expected string format",field); return 0; }
            found|=!strcmp(key,"MAINTITLE")?32:!strcmp(key,"SUBTITLE")?64:128;
        } else if (!strcmp(key,"SAVEDATA_LIST_PARAM")) {
            if (len!=4 || max!=4 || format!=0x404) { sfo_failure(failure,failure_cap,"expected 4-byte integer",field); return 0; }
            found|=256;
        }
    }
    if (found!=511) {
        static const struct { unsigned bit; const char *name; } required[]={
            {1,"TITLE_ID"},{2,"SAVEDATA_DIRECTORY"},{4,"SAVEDATA_BLOCKS"},{8,"ACCOUNT_ID"},
            {16,"PARAMS"},{32,"MAINTITLE"},{64,"SUBTITLE"},{128,"DETAIL"},{256,"SAVEDATA_LIST_PARAM"}};
        for (unsigned i=0;i<sizeof(required)/sizeof(required[0]);i++) if (!(found&required[i].bit)) {
            sfo_failure(failure,failure_cap,"required field missing",required[i].name); return 0;
        }
    }
    return 1;
}
static int read_sfo(zip_t *z, const google_backup *b, uint32_t *blocks, char *failure, size_t failure_cap)
{
    char name[128]; snprintf(name,sizeof(name),"%s/sce_sys/param.sfo",b->directory);
    zip_stat_t st;
    if (zip_stat(z,name,0,&st)) { sfo_failure(failure,failure_cap,"ZIP stat failed","param.sfo"); return 0; }
    if (!st.size || st.size>1024*1024) { sfo_failure(failure,failure_cap,"entry size outside 1..1048576 bytes","param.sfo"); return 0; }
    unsigned char *p=malloc(st.size); if (!p) { sfo_failure(failure,failure_cap,"allocation failed","param.sfo"); return 0; }
    zip_file_t *f=zip_fopen(z,name,0); int ok=0;
    if (!f) sfo_failure(failure,failure_cap,"ZIP entry open failed","param.sfo");
    if (f) {
        zip_int64_t bytes=zip_fread(f,p,st.size);
        if (bytes!=(zip_int64_t)st.size) sfo_failure(failure,failure_cap,"ZIP entry read failed or was truncated","param.sfo");
        else ok=sfo_check(p,st.size,b,blocks,failure,failure_cap);
        if (zip_fclose(f)) { ok=0; sfo_failure(failure,failure_cap,"ZIP entry close/CRC check failed","param.sfo"); }
    }
    free(p); return ok;
}
static int stage_error(google_backup *b,const char *what)
{
    if (!b->diagnostic[0]) snprintf(b->diagnostic,sizeof(b->diagnostic),
        "Safe staging %s failed [errno=%d]. Download retained.",what,errno);
    return 0;
}
static int remove_tree(const char *path,unsigned depth)
{
    struct stat st;
    if (depth>32) { errno=ELOOP; return 0; }
    if (safe_lstat(path,&st)) return errno==ENOENT;
    if (!S_ISDIR(st.st_mode)) return unlink(path)==0;
    DIR *d=opendir(path); if (!d) return 0;
    int ok=1; struct dirent *e;
    while ((e=readdir(d))!=NULL) {
        if (!strcmp(e->d_name,".") || !strcmp(e->d_name,"..")) continue;
        char child[1024]; int n=snprintf(child,sizeof(child),"%s/%s",path,e->d_name);
        if (n<0 || (size_t)n>=sizeof(child) || !remove_tree(child,depth+1)) { ok=0; break; }
    }
    if (closedir(d)) ok=0;
    if (ok && rmdir(path)) ok=0;
    return ok;
}
static int remove_stage(const char *path)
{
    if (!path || !*path || strstr(path,"/../") || !strstr(path,"/stage")) { errno=EINVAL; return 0; }
    return remove_tree(path,0);
}
/* The stage root is newly created below the private 0700 download directory.
   Each component comes from a ZIP name already checked by google_download_zip_fd. */
static int stage_dirs(const char *root,const char *relative,google_backup *b)
{
    char path[1024]; int n=snprintf(path,sizeof(path),"%s/%s",root,relative);
    if (n<0 || (size_t)n>=sizeof(path)) { errno=ENAMETOOLONG; return stage_error(b,"path validation"); }
    for (char *p=path+strlen(root)+1; *p; p++) if (*p=='/') {
        *p=0;
        if (mkdir(path,0700) && errno!=EEXIST) return stage_error(b,"directory creation");
        struct stat st;
        if (safe_lstat(path,&st) || !S_ISDIR(st.st_mode)) { errno=EINVAL; return stage_error(b,"directory type check"); }
        *p='/';
    }
    if (mkdir(path,0700) && errno!=EEXIST) return stage_error(b,"directory creation");
    struct stat st;
    if (safe_lstat(path,&st) || !S_ISDIR(st.st_mode)) { errno=EINVAL; return stage_error(b,"directory type check"); }
    return 1;
}
static int stage_entries(zip_t *z,google_backup *b,const char *stage,const google_restore_io *io)
{
    char root[512], save_root[768], relative_root[384];
    int n=snprintf(root,sizeof(root),"%s/PS4/APOLLO",stage);
    int m=snprintf(relative_root,sizeof(relative_root),"PS4/APOLLO/%s",b->directory);
    if (n<0 || (size_t)n>=sizeof(root) || m<0 || (size_t)m>=sizeof(relative_root) ||
        mkdir(stage,0700) || !stage_dirs(stage,relative_root,b))
        return stage_error(b,"root creation");
    m=snprintf(save_root,sizeof(save_root),"%s/%s",root,b->directory);
    if (m<0 || (size_t)m>=sizeof(save_root)) { errno=ENAMETOOLONG; return stage_error(b,"path validation"); }
    size_t prefix=strlen(b->directory)+1;
    zip_int64_t count=zip_get_num_entries(z,0);
    for (zip_uint64_t i=0;i<(zip_uint64_t)count;i++) {
        zip_stat_t st;
        if (io->cancelled(io->data)) { errno=ECANCELED; return stage_error(b,"cancellation"); }
        if (zip_stat_index(z,i,0,&st) || !st.name || strlen(st.name)<prefix ||
            strncmp(st.name,b->directory,prefix-1) || st.name[prefix-1]!='/') { errno=EINVAL; return stage_error(b,"archive root validation"); }
        const char *rel=st.name+prefix;
        if (!*rel) continue;
        char dest[1024];
        int directory=rel[strlen(rel)-1]=='/';
        n=snprintf(dest,sizeof(dest),"%s/%s",save_root,rel);
        if (n<0 || (size_t)n>=sizeof(dest)) { errno=ENAMETOOLONG; return stage_error(b,"path validation"); }
        if (directory) {
            dest[strlen(dest)-1]=0;
            char parent[1024]; strcpy(parent,dest); char *slash=strrchr(parent,'/');
            if (!slash) { errno=EINVAL; return stage_error(b,"parent validation"); }
            *slash=0;
            if (strcmp(parent,save_root) && !stage_dirs(save_root,parent+strlen(save_root)+1,b)) return 0;
            if (mkdir(dest,0700) && errno!=EEXIST) return stage_error(b,"directory creation");
            struct stat ds;
            if (safe_lstat(dest,&ds) || !S_ISDIR(ds.st_mode)) { errno=EINVAL; return stage_error(b,"directory type check"); }
            continue;
        }
        char parent[1024]; strcpy(parent,dest); char *slash=strrchr(parent,'/');
        if (!slash) { errno=EINVAL; return stage_error(b,"parent validation"); }
        *slash=0;
        if (!stage_dirs(save_root,parent+strlen(save_root)+1,b)) return 0;
        int fd=open(dest,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_NONBLOCK,0600);
        if (fd<0) return stage_error(b,"file creation");
        zip_file_t *f=zip_fopen_index(z,i,0); uint64_t total=0; int ok=f!=NULL;
        unsigned char buffer[16384]; zip_int64_t bytes=0;
        while (ok && (bytes=zip_fread(f,buffer,sizeof(buffer)))>0) {
            if (io->cancelled(io->data) || (uint64_t)bytes>st.size-total) { ok=0; errno=ECANCELED; break; }
            size_t offset=0;
            while (offset<(size_t)bytes) {
                ssize_t w=write(fd,buffer+offset,(size_t)bytes-offset);
                if (w<0 && errno==EINTR) continue;
                if (w<=0) { ok=0; if (!errno) errno=EIO; break; }
                offset+=(size_t)w;
            }
            total+=offset;
        }
        if (bytes<0 || total!=st.size || (f && zip_fclose(f))) ok=0;
        if (ok && fsync(fd)) ok=0;
        int saved=errno;
        if (close(fd)) ok=0;
        errno=saved;
        if (!ok) return stage_error(b,"file write");
    }
    return 1;
}
int google_restore_run(google_backup *b, const google_restore_io *io)
{
    int result=GOOGLE_UPLOAD_FAILED, fd=-1, op=1, zip_error=0, native=0, staged=0; zip_t *z=NULL;
    uint32_t blocks=0; char expected[288], stage[320]; struct stat before, after, private_stat;
    b->diagnostic[0]=0;
    errno=0;
    if (!restore_fs_init(&native)) {
        snprintf(b->diagnostic,sizeof(b->diagnostic),"Restore filesystem setup failed [native=%d]. Download retained.",native);
        return GOOGLE_UPLOAD_FAILED;
    }
    if (strlen(b->title)!=9 || strncmp(b->title,"CUSA",4) || !*b->directory || strlen(b->directory)>=32 ||
        !strcmp(b->directory,".") || !strcmp(b->directory,"..")) { errno=EINVAL; goto fail; }
    for (unsigned i=4;i<9;i++) if (!isdigit((unsigned char)b->title[i])) { errno=EINVAL; goto fail; }
    for (const unsigned char *p=(const unsigned char*)b->directory; *p; p++)
        if (*p<32 || *p==127 || *p=='/' || *p=='\\' || *p==':') { errno=EINVAL; goto fail; }
    if (strncmp(b->temp_dir,GOOGLE_BACKUP_CACHE "drive-",strlen(GOOGLE_BACKUP_CACHE "drive-")) ||
        strchr(b->temp_dir+strlen(GOOGLE_BACKUP_CACHE),'/')) { errno=EINVAL; goto fail; }
    snprintf(expected,sizeof(expected),"%s/backup.zip",b->temp_dir);
    if (strcmp(expected,b->archive)) { errno=EINVAL; goto fail; }
    op=2; errno=0;
    if (safe_lstat(b->temp_dir,&private_stat) || !S_ISDIR(private_stat.st_mode) ||
        (private_stat.st_mode&077) || private_stat.st_uid!=geteuid()) { errno=EINVAL; goto fail; }
    if (safe_lstat(b->archive,&before) || !S_ISREG(before.st_mode) || before.st_uid!=private_stat.st_uid ||
        before.st_nlink!=1 || before.st_size<0 || (uint64_t)before.st_size!=b->size) { errno=EINVAL; goto fail; }
    op=3; errno=0;
    fd=open(b->archive,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);
    if (fd<0) goto fail;
    if (fstat(fd,&after) || !same(&before,&after) || !S_ISREG(after.st_mode)) { errno=EINVAL; goto fail; }
    op=4; errno=0;
    if (!hash_fd(fd,b,io,&zip_error) || !google_download_zip_fd(b,fd,io->cancelled,io->data,&zip_error)) {
        if (io->cancelled(io->data)) { result=GOOGLE_UPLOAD_CANCELLED; goto done; }
        goto fail;
    }
    op=5; errno=0;
    struct stat path_after, dir_after;
    if (fstat(fd,&after) || !same(&before,&after) || safe_lstat(b->archive,&path_after) || !same(&before,&path_after) ||
        safe_lstat(b->temp_dir,&dir_after) || !same(&private_stat,&dir_after) || !S_ISDIR(dir_after.st_mode)) { errno=EINVAL; goto fail; }
    op=6; errno=0; int zipfd=dup(fd);
    if (zipfd<0) goto fail;
    z=zip_fdopen(zipfd,ZIP_RDONLY|ZIP_CHECKCONS,&zip_error);
    if (!z) { close(zipfd); goto fail; }
    op=7; errno=0;
    char sfo_diagnostic[160]={0};
    if (!read_sfo(z,b,&blocks,sfo_diagnostic,sizeof(sfo_diagnostic))) {
        errno=EINVAL;
        snprintf(b->diagnostic,sizeof(b->diagnostic),"Google restore failed [op=7 errno=%d zip=%d]: %.96s. Download retained.",errno,zip_error,
            sfo_diagnostic[0]?sfo_diagnostic:"SFO validation failed [field=param.sfo]");
        goto fail;
    }
    if (io->cancelled(io->data)) { result=GOOGLE_UPLOAD_CANCELLED; goto done; }
    op=8;
    if (io->absent(io->data,b)!=1) { errno=EEXIST; goto fail; }
    if (io->cancelled(io->data)) { result=GOOGLE_UPLOAD_CANCELLED; goto done; }
    snprintf(stage,sizeof(stage),"%s/stage",b->temp_dir);
    staged=1;
    if (!stage_entries(z,b,stage,io)) {
        if (io->cancelled(io->data)) { result=GOOGLE_UPLOAD_CANCELLED; goto done; }
        goto fail;
    }
    if (io->cancelled(io->data)) { result=GOOGLE_UPLOAD_CANCELLED; goto done; }
    op=9; errno=0;
    if (safe_lstat(b->archive,&path_after) || !same(&before,&path_after) || fstat(fd,&after) || !same(&before,&after) ||
        safe_lstat(b->temp_dir,&dir_after) || !same_directory(&private_stat,&dir_after) ||
        safe_lstat(stage,&dir_after) || !S_ISDIR(dir_after.st_mode) || (dir_after.st_mode&077)) { errno=EINVAL; goto fail; }
    if (io->absent(io->data,b)!=1) { errno=EEXIST; goto fail; }
    if (io->cancelled(io->data)) { result=GOOGLE_UPLOAD_CANCELLED; goto done; }
    op=10;
    if (!io->import_staged(io->data,b,stage)) {
        if (io->cancelled(io->data)) { result=GOOGLE_UPLOAD_CANCELLED; goto done; }
        goto fail;
    }
    result=GOOGLE_UPLOAD_SUCCESS;
done:
    if (staged && !remove_stage(stage)) { result=GOOGLE_UPLOAD_FAILED; if (!b->diagnostic[0]) snprintf(b->diagnostic,sizeof(b->diagnostic),"Staging cleanup failed [errno=%d]. Download retained.",errno); }
    if (result==GOOGLE_UPLOAD_SUCCESS && !io->finish(io->data)) result=GOOGLE_UPLOAD_CANCELLED;
    if (z) zip_discard(z);
    if (fd>=0 && close(fd)) { result=GOOGLE_UPLOAD_FAILED; if (!b->diagnostic[0]) snprintf(b->diagnostic,sizeof(b->diagnostic),"Archive close failed [errno=%d]. Download retained.",errno); }
    return result;
fail:
    if (!b->diagnostic[0]) snprintf(b->diagnostic,sizeof(b->diagnostic),"Google restore failed [op=%d errno=%d zip=%d]. Download retained.",op,errno,zip_error);
    result=GOOGLE_UPLOAD_FAILED;
    goto done;
}
