#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <zip.h>
#include "google_download.h"
static cJSON *metadata;
static google_remote_backup remote;
static int stop, mode, requests, refreshes, chunks;
static const char *archive="build/host/download-fixture.zip";
static int cancelled(void *p) { (void)p; return stop; }
static void progress(void *p,uint64_t a,uint64_t b) { (void)p;(void)a;(void)b; }
static int refresh(void *p) { (void)p; refreshes++; return mode!=9 && mode!=14; }
static int wait_fn(void *p,unsigned s) { (void)p;(void)s; return 1; }
static void add(zip_t *z,const char *name,const char *value,unsigned type) {
    zip_source_t *s=zip_source_buffer(z,value,strlen(value),0); assert(s);
    zip_int64_t i=zip_file_add(z,name,s,0); assert(i>=0);
    assert(!zip_set_file_compression(z,i,ZIP_CM_STORE,0));
    assert(!zip_file_set_external_attributes(z,i,0,ZIP_OPSYS_UNIX,(type|0644)<<16));
}
static void fixture(const char *bad,unsigned type) {
    int e; zip_t *z=zip_open(archive,ZIP_CREATE|ZIP_TRUNCATE,&e); assert(z);
    add(z,"SAVE/sce_sys/param.sfo","original sfo",0100000);
    add(z,bad?bad:"SAVE/data.bin","original save",type);
    assert(!zip_close(z));
}
static cJSON *make_metadata(void) {
    google_backup hash={0}; strcpy(hash.archive,archive);
    assert(google_backup_hash(&hash,cancelled,NULL));
    cJSON *j=cJSON_CreateObject(), *details=cJSON_CreateObject(), *props=cJSON_CreateObject();
    cJSON_AddStringToObject(j,"id","backup-id"); cJSON_AddStringToObject(j,"mimeType","application/zip");
    char size[32]; snprintf(size,sizeof(size),"%llu",(unsigned long long)hash.size);
    cJSON_AddStringToObject(j,"size",size); cJSON_AddStringToObject(j,"md5Checksum",hash.md5);
    cJSON_AddStringToObject(props,"ps4SaveSync","ps4-save-sync.v1"); cJSON_AddStringToObject(props,"backupVersion","1");
    cJSON_AddItemToObject(j,"appProperties",props);
    cJSON_AddStringToObject(details,"metadataVersion","1"); cJSON_AddStringToObject(details,"archiveFormat","apollo-decrypted-zip");
    cJSON_AddStringToObject(details,"archiveFormatVersion","1"); cJSON_AddStringToObject(details,"gameName","Game %s \"Name\"");
    cJSON_AddStringToObject(details,"titleId","CUSA12345"); cJSON_AddStringToObject(details,"saveDirectory","SAVE");
    cJSON_AddStringToObject(details,"backupUtc","2026-10-04T12:30:00Z"); cJSON_AddStringToObject(details,"byteSize",size);
    cJSON_AddStringToObject(details,"checksumAlgorithm","md5"); cJSON_AddStringToObject(details,"checksum",hash.md5);
    char *description=cJSON_PrintUnformatted(details); assert(description);
    cJSON_AddStringToObject(j,"description",description); free(description); cJSON_Delete(details); return j;
}
static void request(void *p,const google_upload_request *q,google_upload_response *r) {
    (void)p; requests++; r->transport=CURLE_OK; r->status=200;
    if (mode==8 || mode==9) { if (requests==1) { r->status=401; return; } }
    if (q->download) {
        if ((mode==13 || mode==14) && requests==2) {
            assert(fwrite("error",1,5,q->download)==5); r->downloaded=5; r->status=401; return;
        }
        FILE *f=fopen(archive,"rb"); assert(f); unsigned char buffer[17]; size_t n;
        while ((n=fread(buffer,1,sizeof(buffer),f))) {
            chunks++;
            if (mode==1) { r->transport=CURLE_RECV_ERROR; break; }
            if (mode==2) { stop=1; r->transport=CURLE_ABORTED_BY_CALLBACK; break; }
            if (mode==3) buffer[0]^=1;
            assert(fwrite(buffer,1,n,q->download)==n); r->downloaded+=n;
            if (mode==4) break;
        }
        fclose(f); if (mode==5) r->status=403;
        return;
    }
    if (strstr(q->url,"pageSize=10")) {
        assert(strstr(q->url,"pageToken="));
        r->json=cJSON_CreateObject(); cJSON *files=cJSON_CreateArray();
        cJSON_AddItemToArray(files,cJSON_Duplicate(metadata,1));
        cJSON *invalid=cJSON_Duplicate(metadata,1); cJSON_ReplaceItemInObject(invalid,"mimeType",cJSON_CreateString("text/plain"));
        cJSON_AddItemToArray(files,invalid); cJSON_AddItemToObject(r->json,"files",files);
        if (mode==15) for (unsigned i=0;i<9;i++) cJSON_AddItemToArray(files,cJSON_Duplicate(metadata,1));
        if (mode==16) cJSON_ReplaceItemInObject(r->json,"files",cJSON_CreateArray());
        if (!strstr(q->url,"pageToken=next%2Btoken")) cJSON_AddStringToObject(r->json,"nextPageToken","next+token");
        if (mode==6) cJSON_ReplaceItemInObject(r->json,"nextPageToken",cJSON_CreateNumber(1));
        if (mode==7) cJSON_AddStringToObject(r->json,"nextPageToken","next+token");
    } else {
        r->json=cJSON_Duplicate(metadata,1);
        if (mode==10) cJSON_ReplaceItemInObject(r->json,"size",cJSON_CreateString("1"));
        if (mode==11) r->status=404;
    }
}
static void change(const char *key,const char *value) {
    cJSON *j=cJSON_Parse(cJSON_GetObjectItemCaseSensitive(metadata,"description")->valuestring); assert(j);
    cJSON_ReplaceItemInObject(j,key,cJSON_CreateString(value));
    char *s=cJSON_PrintUnformatted(j); cJSON_ReplaceItemInObject(metadata,"description",cJSON_CreateString(s)); free(s); cJSON_Delete(j);
}
int main(void) {
    mkdir("build/host/cache",0700); fixture(NULL,0100000); metadata=make_metadata();
    assert(google_download_metadata(metadata,&remote));
    google_upload_io io={NULL,request,refresh,cancelled,progress,wait_fn}; google_backup_page page;
    assert(google_download_list(&io,"folder-id","",&page)); assert(page.count==1 && page.rejected==1 && !strcmp(page.next,"next+token"));
    assert(google_download_list(&io,"folder-id",page.next,&page)); assert(page.count==1 && !page.next[0]);
    mode=6; assert(!google_download_list(&io,"folder-id","",&page));
    mode=7; assert(!google_download_list(&io,"folder-id","next+token",&page));
    mode=8; requests=0; assert(google_download_list(&io,"folder-id","",&page)); assert(refreshes==1);
    mode=9; requests=0; assert(!google_download_list(&io,"folder-id","",&page));
    mode=15; assert(!google_download_list(&io,"folder-id","",&page));
    mode=16; assert(google_download_list(&io,"folder-id","",&page)); assert(!page.count && page.next[0]);
    mode=0;
    const char *keys[]={"metadataVersion","archiveFormatVersion","saveDirectory","backupUtc","titleId","byteSize","checksum","gameName","archiveFormat"};
    const char *values[]={"2","2","../escape","2026-02-30T00:00:00Z","BAD123456","18446744073709551616","00000000000000000000000000000000","Game\nControl","unknown-zip"};
    for (unsigned i=0;i<sizeof(keys)/sizeof(*keys);i++) {
        cJSON *saved=cJSON_Duplicate(metadata,1); change(keys[i],values[i]);
        assert(!google_download_metadata(metadata,&remote)); cJSON_Delete(metadata); metadata=saved;
    }
    assert(google_download_metadata(metadata,&remote));
    cJSON *saved=cJSON_Duplicate(metadata,1);
    cJSON *details=cJSON_Parse(cJSON_GetObjectItemCaseSensitive(metadata,"description")->valuestring);
    cJSON_AddStringToObject(details,"metadataVersion","1"); char *duplicate=cJSON_PrintUnformatted(details);
    cJSON_ReplaceItemInObject(metadata,"description",cJSON_CreateString(duplicate));
    assert(!google_download_metadata(metadata,&remote)); free(duplicate); cJSON_Delete(details); cJSON_Delete(metadata); metadata=saved;
    assert(google_download_metadata(metadata,&remote));
    for (mode=0;mode<=5;mode++) {
        google_remote_backup job=remote; stop=0; requests=chunks=0;
        int result=google_download_run(&job,&io);
        if (!mode) { assert(result==GOOGLE_UPLOAD_SUCCESS); assert(chunks>1); assert(!access(job.backup.archive,F_OK)); assert(google_backup_cleanup(&job.backup)); }
        else { assert(result==(mode==2?GOOGLE_UPLOAD_CANCELLED:GOOGLE_UPLOAD_FAILED)); assert(!job.backup.temp_dir[0]); }
    }
    mode=0; stop=0; requests=chunks=0;
    assert(google_download_recheck(&remote,&io) && requests==1 && !chunks);
    saved=cJSON_Duplicate(metadata,1); change("gameName","Changed game");
    assert(!google_download_recheck(&remote,&io)); cJSON_Delete(metadata); metadata=saved;
    mode=10; assert(!google_download_recheck(&remote,&io));
    mode=0; stop=1; assert(!google_download_recheck(&remote,&io)); stop=0;
    for (mode=10;mode<=14;mode++) {
        if (mode==12) continue;
        google_remote_backup job=remote; stop=0; requests=chunks=0;
        int result=google_download_run(&job,&io);
        if (mode==13) { assert(result==GOOGLE_UPLOAD_SUCCESS); assert(google_backup_cleanup(&job.backup)); }
        else { assert(result==GOOGLE_UPLOAD_FAILED); assert(!job.backup.temp_dir[0]); if (mode<=11) assert(!chunks); }
    }
    mode=0; stop=0;
    google_remote_backup first=remote, second=remote; requests=0;
    assert(google_download_run(&first,&io)==GOOGLE_UPLOAD_SUCCESS);
    assert(google_download_run(&second,&io)==GOOGLE_UPLOAD_SUCCESS);
    assert(strcmp(first.backup.archive,second.backup.archive) && !access(first.backup.archive,F_OK));
    assert(google_backup_cleanup(&first.backup) && google_backup_cleanup(&second.backup));
    const char *unsafe[]={"SAVE/../escape","/SAVE/absolute","SAVE/a\\b","SAVE/C:drive","OTHER/file","SAVE/./file","SAVE//file"};
    google_backup b=remote.backup; strcpy(b.archive,archive);
    for (unsigned i=0;i<sizeof(unsafe)/sizeof(*unsafe);i++) { fixture(unsafe[i],0100000); assert(!google_download_zip(&b,cancelled,NULL)); }
    fixture("SAVE/link",0120000); assert(!google_download_zip(&b,cancelled,NULL));
    fixture("SAVE/special",0010000); assert(!google_download_zip(&b,cancelled,NULL));
    fixture(NULL,0100000);
    FILE *corrupt=fopen(archive,"rb+"); assert(corrupt);
    unsigned char raw[2048]; size_t len=fread(raw,1,sizeof(raw),corrupt); int found=0;
    for (size_t i=0;i+13<len;i++) if (!memcmp(raw+i,"original save",13)) {
        assert(!fseeko(corrupt,i,SEEK_SET)); assert(fputc('X',corrupt)!=EOF); found=1; break;
    }
    assert(found); fclose(corrupt); assert(!google_download_zip(&b,cancelled,NULL)); /* Real CRC error. */
    fixture(NULL,0100000); corrupt=fopen(archive,"ab"); assert(corrupt); fputs("trailing",corrupt); fclose(corrupt);
    assert(!google_download_zip(&b,cancelled,NULL));
    fixture(NULL,0100000); corrupt=fopen(archive,"rb+"); assert(corrupt);
    len=fread(raw,1,sizeof(raw),corrupt); found=0;
    for (size_t i=0;i+4<len;i++) if (!memcmp(raw+i,"PK\001\002",4)) {
        assert(!fseeko(corrupt,i+46+2,SEEK_SET)); assert(fputc(0,corrupt)!=EOF); found=1; break;
    }
    assert(found); fclose(corrupt); assert(!google_download_zip(&b,cancelled,NULL));
    int e; zip_t *z=zip_open(archive,ZIP_CREATE|ZIP_TRUNCATE,&e); assert(z);
    add(z,"SAVE/data.bin","save",0100000); assert(!zip_close(z)); assert(!google_download_zip(&b,cancelled,NULL));
    z=zip_open(archive,ZIP_CREATE|ZIP_TRUNCATE,&e); assert(z);
    add(z,"SAVE/sce_sys/param.sfo","sfo",0100000); add(z,"SAVE/a","file",0100000);
    add(z,"SAVE/a/b","conflict",0100000); assert(!zip_close(z)); assert(!google_download_zip(&b,cancelled,NULL));
    fixture(NULL,0100000); stop=1; assert(!google_download_zip(&b,cancelled,NULL)); stop=0;
    FILE *f=fopen(archive,"wb"); assert(f); fputs("not a zip",f); fclose(f); assert(!google_download_zip(&b,cancelled,NULL));
    unlink(archive); cJSON_Delete(metadata);
    puts("Google download tests passed (pagination, metadata, failures/cancellation, checksum, ZIP safety)."); return 0;
}
