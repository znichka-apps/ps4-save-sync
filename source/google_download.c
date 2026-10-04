/* Read-only Drive backups. Remote strings never become local filenames. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/stat.h>
#include <zip.h>
#include "google_download.h"
#define FILES "https://www.googleapis.com/drive/v3/files"
static const char *str(cJSON *j, const char *k) {
    cJSON *p = cJSON_GetObjectItemCaseSensitive(j,k);
    return cJSON_IsString(p) ? p->valuestring : NULL;
}
static int equal(cJSON *j, const char *k, const char *v) {
    const char *s = str(j,k); return s && !strcmp(s,v);
}
static int unique(cJSON *j) {
    if (!cJSON_IsObject(j)) return 0;
    for (cJSON *p=j->child;p;p=p->next)
        for (cJSON *q=p->next;q;q=q->next) if (!strcmp(p->string,q->string)) return 0;
    return 1;
}
static int display_text(const unsigned char *s) {
    while (*s) {
        unsigned cp=*s++, continuation=0, minimum=0;
        if (cp>=128) {
            if (cp>=0xc2 && cp<=0xdf) { cp&=31; continuation=1; minimum=128; }
            else if (cp>=0xe0 && cp<=0xef) { cp&=15; continuation=2; minimum=2048; }
            else if (cp>=0xf0 && cp<=0xf4) { cp&=7; continuation=3; minimum=65536; }
            else return 0;
            while (continuation--) { if ((*s&0xc0)!=0x80) return 0; cp=(cp<<6)|(*s++&63); }
            if (cp<minimum || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)) return 0;
        }
        if (cp<32 || (cp>=127 && cp<=159) || (cp>=0x200b && cp<=0x200f) ||
            (cp>=0x2028 && cp<=0x202e) || (cp>=0x2066 && cp<=0x2069) || cp==0xfeff) return 0;
    }
    return 1;
}
static int text(cJSON *j, const char *k, char *out, size_t cap) {
    const char *s = str(j,k);
    if (!s || !*s || strlen(s) >= cap) return 0;
    if (!display_text((const unsigned char*)s)) return 0;
    strcpy(out,s); return 1;
}
static int id(const char *s) {
    if (!s || !*s || strlen(s)>128) return 0;
    for (; *s; s++) if (!isalnum((unsigned char)*s) && *s!='-' && *s!='_') return 0;
    return 1;
}
static int number(const char *s, uint64_t *v) {
    *v=0; if (!s || !*s || (*s=='0' && s[1])) return 0;
    for (; *s; s++) {
        if (*s<'0' || *s>'9' || *v>((uint64_t)INT64_MAX-(*s-'0'))/10) return 0;
        *v=*v*10+(*s-'0');
    }
    return *v>0;
}
static int component(const char *s) {
    if (!*s || !strcmp(s,".") || !strcmp(s,"..")) return 0;
    for (; *s; s++) if ((unsigned char)*s<32 || *s==127 || *s=='/' || *s=='\\' || *s==':') return 0;
    return 1;
}
static int utc(const char *s) {
    if (strlen(s)!=20) return 0;
    for (unsigned i=0;i<20;i++) {
        char c=i==4||i==7?'-':i==10?'T':i==13||i==16?':':i==19?'Z':0;
        if (c ? s[i]!=c : !isdigit((unsigned char)s[i])) return 0;
    }
    int y, m, d, h, n, sec;
    if (sscanf(s,"%d-%d-%dT%d:%d:%dZ",&y,&m,&d,&h,&n,&sec)!=6 || y<1970 || m<1 || m>12 || h>23 || n>59 || sec>59) return 0;
    const int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    return d>=1 && d<=days[m-1]+(m==2 && y%4==0 && (y%100!=0 || y%400==0));
}
int google_download_metadata(cJSON *file, google_remote_backup *out) {
    google_remote_backup r={0}; uint64_t size;
    cJSON *props=cJSON_GetObjectItemCaseSensitive(file,"appProperties");
    const char *description=str(file,"description");
    if (!unique(file) || !unique(props) || !id(str(file,"id")) || !equal(file,"mimeType","application/zip") ||
        !equal(props,"ps4SaveSync","ps4-save-sync.v1") || !equal(props,"backupVersion","1") ||
        !description || strlen(description)>8192 || strstr(description,"\\u0000") || !number(str(file,"size"),&size)) return 0;
    cJSON *j=cJSON_ParseWithOpts(description,NULL,1); int ok=0;
    /* Duplicate keys are ambiguous; reject rather than choosing one. */
    if (!unique(j) || cJSON_GetArraySize(j)!=10) goto done;
    google_backup *b=&r.backup;
    if (!equal(j,"metadataVersion","1") || !equal(j,"archiveFormat","apollo-decrypted-zip") ||
        !equal(j,"archiveFormatVersion","1") || !equal(j,"checksumAlgorithm","md5") ||
        !text(j,"gameName",b->game,sizeof(b->game)) || !text(j,"titleId",b->title,sizeof(b->title)) ||
        !text(j,"saveDirectory",b->directory,sizeof(b->directory)) || !component(b->directory) ||
        !text(j,"backupUtc",b->utc,sizeof(b->utc)) || !utc(b->utc) ||
        !number(str(j,"byteSize"),&b->size) || b->size!=size ||
        !text(j,"checksum",b->md5,sizeof(b->md5)) || strlen(b->md5)!=32 ||
        !equal(file,"md5Checksum",b->md5) || strlen(b->title)!=9 || strncmp(b->title,"CUSA",4)) goto done;
    for (unsigned i=4;i<9;i++) if (!isdigit((unsigned char)b->title[i])) goto done;
    for (unsigned i=0;i<32;i++) if (!isdigit((unsigned char)b->md5[i]) && (b->md5[i]<'a' || b->md5[i]>'f')) goto done;
    strcpy(r.id,str(file,"id")); *out=r; ok=1;
done: cJSON_Delete(j); return ok;
}
static int call(const google_upload_io *io, google_upload_request *q, google_upload_response *r) {
    memset(r,0,sizeof(*r)); io->request(io->data,q,r);
    if (r->transport==CURLE_OK && r->status==401 && !io->cancelled(io->data)) {
        cJSON_Delete(r->json); memset(r,0,sizeof(*r));
        if (!io->refresh(io->data)) return 0;
        if (q->download && (fflush(q->download) || ftruncate(fileno(q->download),0) || fseeko(q->download,0,SEEK_SET))) return 0;
        io->request(io->data,q,r);
    }
    return r->transport==CURLE_OK && r->status==200 && !r->invalid_headers && !io->cancelled(io->data);
}
int google_download_list(const google_upload_io *io, const char *folder, const char *page, google_backup_page *out) {
    char token[512];
    if (!page || strlen(page)>=sizeof(token)) return 0;
    strcpy(token,page); page=token;
    memset(out,0,sizeof(*out));
    if (!id(folder)) return 0;
    char query[512], url[4096];
    snprintf(query,sizeof(query),"trashed = false and '%s' in parents and appProperties has { key='ps4SaveSync' and value='ps4-save-sync.v1' }",folder);
    char *q=curl_easy_escape(NULL,query,0), *p=curl_easy_escape(NULL,page,0);
    if (!q || !p) { curl_free(q); curl_free(p); return 0; }
    snprintf(url,sizeof(url),FILES "?spaces=drive&pageSize=10&orderBy=createdTime%%20desc&fields=nextPageToken,files(id,mimeType,size,md5Checksum,description,appProperties)&q=%s&pageToken=%s",q,p);
    curl_free(q); curl_free(p);
    google_upload_request request={.method="GET",.url=url}; google_upload_response r;
    int ok=call(io,&request,&r); cJSON *files=cJSON_GetObjectItemCaseSensitive(r.json,"files");
    const char *next=str(r.json,"nextPageToken");
    if (!ok || !unique(r.json) || cJSON_GetObjectItemCaseSensitive(r.json,"error") || !cJSON_IsArray(files) || cJSON_GetArraySize(files)>10 ||
        (cJSON_GetObjectItemCaseSensitive(r.json,"nextPageToken") && (!next || !*next || strlen(next)>=512 || !strcmp(next,page)))) ok=0;
    if (ok) {
        cJSON *item; cJSON_ArrayForEach(item,files) {
            if (google_download_metadata(item,&out->entries[out->count])) out->count++;
            else out->rejected++;
        }
        if (next) strcpy(out->next,next);
    }
    cJSON_Delete(r.json); if (!ok) memset(out,0,sizeof(*out)); return ok;
}
static unsigned u16(const unsigned char *p) { return p[0] | (unsigned)p[1]<<8; }
static uint64_t u32(const unsigned char *p) { return u16(p) | (uint64_t)u16(p+2)<<16; }
/* Check raw central/local names as well: C string APIs can hide embedded NULs.
   This v1 reader deliberately rejects ZIP64, multi-disk and trailing data. */
static int raw_names(const google_backup *b, int (*cancel)(void*), void *data) {
    FILE *f=fopen(b->archive,"rb"); if (!f) return 0;
    unsigned char tail[65557], header[46], local[30], name[1024], local_name[1024];
    int ok=0;
    if (fseeko(f,0,SEEK_END)) goto done;
    off_t length=ftello(f); if (length<22) goto done;
    size_t size=length>(off_t)sizeof(tail)?sizeof(tail):(size_t)length;
    if (fseeko(f,length-size,SEEK_SET) || fread(tail,1,size,f)!=size) goto done;
    size_t end=size-22;
    while (memcmp(tail+end,"PK\005\006",4)) { if (!end) goto done; end--; }
    const unsigned char *e=tail+end;
    unsigned count=u16(e+10);
    uint64_t offset=u32(e+16), central=u32(e+12), position=offset;
    if (end+22+u16(e+20)!=size || u16(e+4) || u16(e+6) || u16(e+8)!=count ||
        !count || count>4096 || central>8U*1024U*1024U || offset==UINT32_MAX ||
        offset+central!=(uint64_t)(length-size+end)) goto done;
    for (unsigned i=0;i<count;i++) {
        if (cancel(data) || position+46>offset+central || fseeko(f,position,SEEK_SET) ||
            fread(header,1,46,f)!=46 || memcmp(header,"PK\001\002",4)) goto done;
        unsigned n=u16(header+28), extra=u16(header+30), comment=u16(header+32);
        uint64_t loc=u32(header+42);
        if (!n || n>=sizeof(name) || extra>4096 || comment>1024 || position+46+n+extra+comment>offset+central ||
            u16(header+34) || loc==UINT32_MAX || u32(header+20)==UINT32_MAX || u32(header+24)==UINT32_MAX ||
            fread(name,1,n,f)!=n || memchr(name,0,n)) goto done;
        if (loc+30+n>offset || fseeko(f,loc,SEEK_SET) || fread(local,1,30,f)!=30 ||
            memcmp(local,"PK\003\004",4) || u16(local+26)!=n ||
            u16(local+6)!=u16(header+8) || u16(local+8)!=u16(header+10) ||
            fread(local_name,1,n,f)!=n || memcmp(name,local_name,n)) goto done;
        if (u16(local+28)>4096 || loc+30+n+u16(local+28)+u32(header+20)>offset) goto done;
        position+=46+n+extra+comment;
    }
    ok=position==offset+central;
done: if (fclose(f)) ok=0; return ok;
}
int google_download_zip(const google_backup *b, int (*cancel)(void*), void *data) {
    int error, ok=0, files=0, sfo=0; uint64_t expanded=0;
    if (!raw_names(b,cancel,data)) return 0;
    zip_t *z=zip_open(b->archive,ZIP_RDONLY|ZIP_CHECKCONS,&error); if (!z) return 0;
    zip_int64_t count=zip_get_num_entries(z,0);
    if (count<=0 || count>4096) goto done;
    for (zip_uint64_t i=0;i<(zip_uint64_t)count;i++) {
        zip_stat_t st; zip_uint8_t os; zip_uint32_t attr;
        if (cancel(data) || zip_stat_index(z,i,0,&st) || !st.name || strlen(st.name)>1023 ||
            zip_file_get_external_attributes(z,i,0,&os,&attr)) goto done;
        const char *name=st.name; size_t root=strlen(b->directory), len=strlen(name);
        if (len<=root || strncmp(name,b->directory,root) || name[root]!='/') goto done;
        char path[1024]; strcpy(path,name);
        for (char *start=path; *start;) {
            char *end=strchr(start,'/'); if (end) *end=0;
            if (!component(start)) goto done;
            if (!end) break;
            start=end+1;
        }
        unsigned mode=(attr>>16)&0170000;
        int dir=name[len-1]=='/';
        if ((os==ZIP_OPSYS_UNIX && mode && mode!=(dir?0040000:0100000)) ||
            st.encryption_method!=ZIP_EM_NONE ||
            (st.comp_method!=ZIP_CM_STORE && st.comp_method!=ZIP_CM_DEFLATE) || (dir && st.size)) goto done;
        for (zip_uint64_t k=0;k<i;k++) {
            const char *prior=zip_get_name(z,k,0); size_t plen=strlen(prior);
            if (!strcmp(prior,name) || (plen<len && prior[plen-1]!='/' && !strncmp(prior,name,plen) && name[plen]=='/') ||
                (len<plen && !dir && !strncmp(prior,name,len) && prior[len]=='/')) goto done;
        }
        if (dir) continue;
        if (st.size> (uint64_t)4*1024*1024*1024 || expanded>(uint64_t)16*1024*1024*1024-st.size) goto done;
        expanded+=st.size; files++;
        char required[128]; snprintf(required,sizeof(required),"%s/sce_sys/param.sfo",b->directory);
        if (!strcmp(name,required) && st.size) sfo=1;
        zip_file_t *f=zip_fopen_index(z,i,0); if (!f) goto done;
        unsigned char buffer[16384]; zip_int64_t n; uint64_t read=0; int valid=1;
        while ((n=zip_fread(f,buffer,sizeof(buffer)))>0) {
            if (cancel(data) || (uint64_t)n>st.size-read) { valid=0; break; }
            read+=(uint64_t)n;
        }
        if (n<0 || read!=st.size) valid=0;
        if (zip_fclose(f)) valid=0;
        if (!valid) goto done;
    }
    ok=files && sfo && !cancel(data);
done: zip_discard(z); return ok;
}
static int cache_directory(void) {
    char path[256];
    if (strlen(GOOGLE_BACKUP_CACHE)>=sizeof(path)) return 0;
    strcpy(path,GOOGLE_BACKUP_CACHE);
    for (char *p=path+1;*p;p++) if (*p=='/') {
        *p=0; int code=mkdir(path,0700);
        int ok=!code || !access(path,W_OK); *p='/';
        if (!ok) return 0;
    }
    return 1;
}
int google_download_run(google_remote_backup *remote, const google_upload_io *io) {
    google_backup *b=&remote->backup; google_upload_response r={0}; int result=GOOGLE_UPLOAD_FAILED;
    char url[256]; FILE *fp=NULL;
    if (!id(remote->id) || io->cancelled(io->data)) goto done;
    /* Re-read metadata at selection time, rejecting changed files. */
    snprintf(url,sizeof(url),FILES "/%s?fields=id,mimeType,size,md5Checksum,description,appProperties",remote->id);
    google_upload_request q={.method="GET",.url=url}; google_remote_backup fresh;
    if (!call(io,&q,&r) || !google_download_metadata(r.json,&fresh) ||
        strcmp(fresh.id,remote->id) || strcmp(fresh.backup.game,b->game) ||
        strcmp(fresh.backup.title,b->title) || strcmp(fresh.backup.directory,b->directory) ||
        strcmp(fresh.backup.utc,b->utc) || strcmp(fresh.backup.md5,b->md5) || fresh.backup.size!=b->size) goto done;
    cJSON_Delete(r.json); memset(&r,0,sizeof(r));
    if (!cache_directory()) goto done;
    snprintf(b->temp_dir,sizeof(b->temp_dir),GOOGLE_BACKUP_CACHE "drive-XXXXXX");
    if (!mkdtemp(b->temp_dir)) { b->temp_dir[0]=0; goto done; }
    snprintf(b->archive,sizeof(b->archive),"%s/backup.zip",b->temp_dir);
    fp=fopen(b->archive,"wb+"); if (!fp) goto done;
    snprintf(url,sizeof(url),FILES "/%s?alt=media",remote->id);
    q=(google_upload_request){.method="GET",.url=url,.download=fp,.total=b->size};
    if (!call(io,&q,&r) || r.downloaded!=b->size || fflush(fp) || ferror(fp)) goto done;
    if (fclose(fp)) { fp=NULL; goto done; } fp=NULL;
    google_backup hash=*b;
    if (!google_backup_hash(&hash,io->cancelled,io->data) || hash.size!=b->size || strcmp(hash.md5,b->md5) ||
        !google_download_zip(b,io->cancelled,io->data)) goto done;
    result=GOOGLE_UPLOAD_SUCCESS;
done:
    cJSON_Delete(r.json); if (fp) fclose(fp);
    if (result!=GOOGLE_UPLOAD_SUCCESS) {
        if (io->cancelled(io->data)) result=GOOGLE_UPLOAD_CANCELLED;
        if (!google_backup_cleanup(b)) snprintf(b->diagnostic,sizeof(b->diagnostic),"Temporary download cleanup failed.");
    }
    return result;
}
