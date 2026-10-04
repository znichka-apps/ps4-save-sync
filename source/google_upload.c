/* Drive resumable protocol; no mounts, global selection, or credential logging. */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <ctype.h>
#include "google_upload.h"
#define FILES "https://www.googleapis.com/drive/v3/files"
#define START "https://www.googleapis.com/upload/drive/v3/files?uploadType=resumable&fields=id,size,md5Checksum"
#define MARKER "ps4-save-sync.v1"
static const char *string(cJSON *j, const char *key)
{
    cJSON *p = cJSON_GetObjectItemCaseSensitive(j, key);
    return cJSON_IsString(p) ? p->valuestring : NULL;
}
static int identifier(const char *s)
{
    if (!s || !*s || strlen(s) > 128) return 0;
    for (; *s; s++) if (!isalnum((unsigned char)*s) && *s != '_' && *s != '-') return 0;
    return 1;
}
static int folder_time(const char *s, char out[32])
{
    /* Drive returns UTC RFC3339; normalize optional fractional seconds for sorting. */
    if (!s) return 0;
    size_t n = strlen(s);
    if (n < 20 || n > 30 || s[n-1] != 'Z') return 0;
    for (size_t i = 0; i < 19; i++) {
        char expected = i==4 || i==7 ? '-' : i==10 ? 'T' : i==13 || i==16 ? ':' : 0;
        if (expected ? s[i]!=expected : (s[i]<'0' || s[i]>'9')) return 0;
    }
    memcpy(out,s,19); out[19]='.'; memset(out+20,'0',9); out[29]=0;
    if (n == 20) return 1;
    if (s[19] != '.' || n < 22) return 0;
    for (size_t i=20;i<n-1;i++) {
        if (s[i]<'0' || s[i]>'9') return 0;
        out[i]=s[i];
    }
    return 1;
}
int google_upload_session_url(const char *url)
{
    const char *prefix = "https://www.googleapis.com/upload/drive/v3/files?";
    if (!url || strncmp(url,prefix,strlen(prefix)) || strlen(url) >= 2048) return 0;
    for (const unsigned char *p = (const unsigned char*)url; *p; p++)
        if (*p <= 32 || *p >= 127 || *p == '#' || *p == '\\') return 0;
    const char *query = url + strlen(prefix); int resumable = 0, upload_id = 0;
    while (*query) {
        const char *end = strchr(query,'&'); size_t n = end ? (size_t)(end-query) : strlen(query);
        if (n >= 11 && !strncmp(query,"uploadType=",11)) {
            if (n != strlen("uploadType=resumable") || strncmp(query,"uploadType=resumable",n)) return 0;
            resumable++;
        }
        if (n >= 10 && !strncmp(query,"upload_id=",10)) {
            if (n == 10) return 0;
            upload_id++;
        }
        if (!end) break;
        query = end + 1;
    }
    return resumable == 1 && upload_id == 1;
}
static void dispose(google_upload_response *r) { cJSON_Delete(r->json); memset(r,0,sizeof(*r)); }
static int call(const google_upload_io *io, google_upload_request *q, google_upload_response *r, int retry_auth)
{
    memset(r,0,sizeof(*r)); io->request(io->data,q,r);
    if (r->transport == CURLE_OK && r->status == 401 && retry_auth && !io->cancelled(io->data)) {
        dispose(r); if (!io->refresh(io->data)) return 0;
        io->request(io->data,q,r);
    }
    return r->transport == CURLE_OK && !r->invalid_headers;
}
int google_upload_folder(const google_upload_io *io, char *id, int allow_create)
{
    const char *query = "trashed = false and mimeType = 'application/vnd.google-apps.folder' and 'root' in parents and appProperties has { key='ps4SaveSync' and value='" MARKER "' }";
    char *escaped = curl_easy_escape(NULL,query,0);
    if (!escaped) return 0;
    char page[512] = "", earliest[40] = "", url[4096]; int ok = 0; id[0] = 0;
    for (unsigned i = 0; i < 128 && !io->cancelled(io->data); i++) {
        char *token = curl_easy_escape(NULL,page,0); if (!token) break;
        snprintf(url,sizeof(url),FILES "?spaces=drive&pageSize=100&fields=nextPageToken,files(id,createdTime)&q=%s&pageToken=%s",escaped,token);
        curl_free(token);
        google_upload_request q = {.method="GET",.url=url}; google_upload_response r;
        int received = call(io,&q,&r,1);
        cJSON *files = cJSON_GetObjectItemCaseSensitive(r.json,"files");
        if (!received || r.status != 200 || !cJSON_IsArray(files) ||
            cJSON_GetObjectItemCaseSensitive(r.json,"error")) { dispose(&r); break; }
        cJSON *item; int valid = 1;
        cJSON_ArrayForEach(item,files) {
            const char *candidate = string(item,"id");
            const char *created = string(item,"createdTime");
            char normalized[32];
            if (!identifier(candidate) || !folder_time(created,normalized)) { valid = 0; break; }
            /* Oldest creation stays canonical when a later first-use race adds a folder. */
            if (!id[0] || strcmp(normalized,earliest) < 0 ||
                (!strcmp(normalized,earliest) && strcmp(candidate,id) < 0)) {
                snprintf(id,129,"%s",candidate); snprintf(earliest,sizeof(earliest),"%s",normalized);
            }
        }
        const char *next = string(r.json,"nextPageToken");
        if (!valid || (cJSON_GetObjectItemCaseSensitive(r.json,"nextPageToken") && !next) ||
            (next && (!*next || strlen(next) >= sizeof(page) || !strcmp(next,page)))) { dispose(&r); break; }
        if (!next) { ok = 1; dispose(&r); break; }
        snprintf(page,sizeof(page),"%s",next); dispose(&r);
    }
    curl_free(escaped);
    if (!ok || io->cancelled(io->data)) return 0;
    if (id[0]) return 1;
    if (!allow_create) return 0;
    google_upload_request q = {.method="POST",.url=FILES "?fields=id",
        .json="{\"name\":\"PS4 Save Sync\",\"mimeType\":\"application/vnd.google-apps.folder\",\"parents\":[\"root\"],\"appProperties\":{\"ps4SaveSync\":\"" MARKER "\"}}"};
    google_upload_response r;
    /* No blind retry after a lost creation response; the next job searches again. */
    ok = call(io,&q,&r,1) && r.status == 200 && identifier(string(r.json,"id"));
    dispose(&r);
    /* Resolve a concurrent creator before choosing this upload's parent. */
    return ok && google_upload_folder(io,id,0);
}
static char *metadata(const google_backup *b, const char *folder_id, const char *file_id)
{
    cJSON *j = cJSON_CreateObject(), *details = cJSON_CreateObject(), *parents = cJSON_CreateArray(), *props = cJSON_CreateObject();
    char size[32], name[256]; char *description = NULL, *out = NULL;
    if (!j || !details || !parents || !props) goto done;
    snprintf(size,sizeof(size),"%" PRIu64,b->size);
    snprintf(name,sizeof(name),"%s-%s-%s.zip",b->title,b->directory,b->utc);
#define ADD(o,k,v) do { if (!cJSON_AddStringToObject(o,k,v)) goto done; } while (0)
    ADD(details,"metadataVersion","1"); ADD(details,"gameName",b->game);
    ADD(details,"titleId",b->title); ADD(details,"saveDirectory",b->directory);
    ADD(details,"backupUtc",b->utc); ADD(details,"archiveFormat","apollo-decrypted-zip");
    ADD(details,"archiveFormatVersion","1"); ADD(details,"byteSize",size);
    ADD(details,"checksumAlgorithm","md5"); ADD(details,"checksum",b->md5);
    description = cJSON_PrintUnformatted(details); if (!description) goto done;
    ADD(j,"id",file_id); ADD(j,"name",name); ADD(j,"mimeType","application/zip"); ADD(j,"description",description);
    ADD(props,"ps4SaveSync",MARKER); ADD(props,"backupVersion","1");
    cJSON *parent = cJSON_CreateString(folder_id);
    if (!parent || !cJSON_AddItemToArray(parents,parent)) { cJSON_Delete(parent); goto done; }
    if (!cJSON_AddItemToObject(j,"parents",parents)) goto done;
    parents = NULL;
    if (!cJSON_AddItemToObject(j,"appProperties",props)) goto done;
    props = NULL; out = cJSON_PrintUnformatted(j);
done:
    free(description); cJSON_Delete(j); cJSON_Delete(details); cJSON_Delete(parents); cJSON_Delete(props);
    return out;
#undef ADD
}
static int complete(cJSON *j, const google_backup *b, const char *id)
{
    char size[32]; snprintf(size,sizeof(size),"%" PRIu64,b->size);
    const char *remote_id = string(j,"id"), *remote_size = string(j,"size"), *md5 = string(j,"md5Checksum");
    return !cJSON_GetObjectItemCaseSensitive(j,"error") && remote_id && !strcmp(id,remote_id) &&
        remote_size && !strcmp(size,remote_size) && md5 && !strcmp(b->md5,md5);
}
static int verify(const google_upload_io *io, const google_backup *b, const char *id)
{
    char url[256]; snprintf(url,sizeof(url),FILES "/%s?fields=id,size,md5Checksum",id);
    google_upload_request q = {.method="GET",.url=url}; google_upload_response r;
    int ok = call(io,&q,&r,1) && r.status == 200 && complete(r.json,b,id);
    dispose(&r); return ok;
}
static int offset(const char *range, uint64_t size, uint64_t *next)
{
    if (!*range) { *next = 0; return 1; }
    if (strncmp(range,"bytes=0-",8)) return 0;
    const char *p = range + 8; uint64_t value = 0;
    if (!*p) return 0;
    for (; *p; p++) {
        if (*p < '0' || *p > '9' || value > (UINT64_MAX - (*p-'0'))/10) return 0;
        value = value * 10 + (*p-'0');
    }
    if (value >= size) return 0;
    *next = value + 1; return 1;
}
int google_upload_run(const google_backup *b, const google_upload_io *io)
{
    char folder_id[129], file_id[129], session[2048], range[96];
    google_upload_response r = {0}; FILE *fp = NULL; char *body = NULL;
    int result = GOOGLE_UPLOAD_FAILED, final_sent = 0;
    if (!b->size || b->size > INT64_MAX || !google_upload_folder(io,folder_id,1)) goto done;
    google_upload_request q = {.method="GET",.url=FILES "/generateIds?count=1&space=drive&type=files"};
    if (!call(io,&q,&r,1) || r.status != 200) goto done;
    cJSON *ids = cJSON_GetObjectItemCaseSensitive(r.json,"ids"), *first = cJSON_GetArrayItem(ids,0);
    if (!cJSON_IsArray(ids) || cJSON_GetArraySize(ids) != 1 || !cJSON_IsString(first) || !identifier(first->valuestring)) goto done;
    snprintf(file_id,sizeof(file_id),"%s",first->valuestring); dispose(&r);
    body = metadata(b,folder_id,file_id); if (!body) goto done;
    q = (google_upload_request){.method="POST",.url=START,.json=body,.total=b->size};
    if (!call(io,&q,&r,1) || r.status != 200 || !google_upload_session_url(r.location)) {
        /* No ZIP bytes were sent; this attempt cannot have completed a backup. */
        goto done;
    }
    snprintf(session,sizeof(session),"%s",r.location); dispose(&r);
    fp = fopen(b->archive,"rb"); if (!fp) goto done;
    uint64_t next = 0, highest_sent = 0; int probe = 0; unsigned recovery = 0, stalls = 0;
    while (!io->cancelled(io->data)) {
        uint64_t length = b->size - next;
        if (length > GOOGLE_UPLOAD_CHUNK) length = GOOGLE_UPLOAD_CHUNK;
        if (probe) snprintf(range,sizeof(range),"bytes */%" PRIu64,b->size);
        else snprintf(range,sizeof(range),"bytes %" PRIu64 "-%" PRIu64 "/%" PRIu64,next,next+length-1,b->size);
        q = (google_upload_request){.method="PUT",.url=session,.range=range,.file=probe ? NULL : fp,
            .offset=next,.length=probe ? 0 : length,.total=b->size};
        if (!probe) {
            if (next+length == b->size) final_sent = 1;
            if (next+length > highest_sent) highest_sent = next+length;
        }
        int received = call(io,&q,&r,0);
        if (received && (r.status == 200 || r.status == 201)) {
            result = complete(r.json,b,file_id) || verify(io,b,file_id) ? GOOGLE_UPLOAD_SUCCESS : GOOGLE_UPLOAD_UNCERTAIN;
            goto done;
        }
        if (io->cancelled(io->data)) { result = final_sent ? GOOGLE_UPLOAD_UNCERTAIN : GOOGLE_UPLOAD_CANCELLED; goto done; }
        if (received && r.status == 308) {
            uint64_t confirmed;
            if (!offset(r.range,b->size,&confirmed) || confirmed > highest_sent) {
                result = final_sent ? GOOGLE_UPLOAD_UNCERTAIN : GOOGLE_UPLOAD_FAILED; goto done;
            }
            if (confirmed == b->size) {
                /* All bytes acknowledged is not completion. Ask for completion proof. */
                if (++stalls > 3) { result = GOOGLE_UPLOAD_UNCERTAIN; goto done; }
                probe = 1; dispose(&r); continue;
            }
            if (confirmed <= next) {
                if (++stalls > 3) { result = final_sent ? GOOGLE_UPLOAD_UNCERTAIN : GOOGLE_UPLOAD_FAILED; goto done; }
            }
            else stalls = 0;
            next = confirmed; probe = 0;
            io->progress(io->data,next,b->size); dispose(&r); continue;
        }
        if (received && r.status == 401) {
            if (++recovery > 4 || !io->refresh(io->data)) { result = final_sent ? GOOGLE_UPLOAD_UNCERTAIN : GOOGLE_UPLOAD_FAILED; goto done; }
            probe = 1; dispose(&r); continue; /* Query before resending any bytes. */
        }
        if (!received || r.status >= 500 || r.status == 429) {
            if (++recovery > 4 || !io->wait(io->data,1U << recovery)) {
                result = final_sent ? GOOGLE_UPLOAD_UNCERTAIN :
                    (io->cancelled(io->data) ? GOOGLE_UPLOAD_CANCELLED : GOOGLE_UPLOAD_FAILED);
                goto done;
            }
            probe = 1; dispose(&r); continue;
        }
        /* Expiration/conflict: check this generated ID; never create another backup. */
        result = verify(io,b,file_id) ? GOOGLE_UPLOAD_SUCCESS : (final_sent ? GOOGLE_UPLOAD_UNCERTAIN : GOOGLE_UPLOAD_FAILED);
        goto done;
    }
    result = final_sent ? GOOGLE_UPLOAD_UNCERTAIN : GOOGLE_UPLOAD_CANCELLED;
done:
    if (io->cancelled(io->data) && result == GOOGLE_UPLOAD_FAILED) result = GOOGLE_UPLOAD_CANCELLED;
    dispose(&r); free(body); if (fp) fclose(fp);
    return result;
}
