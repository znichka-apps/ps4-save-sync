/* Deterministic Drive protocol tests, real JSON/hash/file streaming. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "google_upload.h"
typedef struct {
    const char *method, *url, *json, *range, *location;
    long status;
    CURLcode transport;
    uint64_t offset, length;
    int cancel, invalid;
} step;
static step steps[32]; static unsigned count, index_, refreshes, waits, starts, creates, data_requests;
static int cancelled_, refresh_ok;
static google_backup backup;
static char completion[256];
#define SESSION "https://www.googleapis.com/upload/drive/v3/files?uploadType=resumable&upload_id=synthetic"
static int cancelled(void *p) { (void)p; return cancelled_; }
static int refresh(void *p) { (void)p; refreshes++; return refresh_ok; }
static int wait_(void *p, unsigned seconds) { (void)p; assert(seconds <= 16); waits++; return !cancelled_; }
static void progress(void *p, uint64_t n, uint64_t total) { (void)p; assert(n <= total && total == backup.size); }
static void request(void *p, const google_upload_request *q, google_upload_response *r)
{
    (void)p; assert(index_ < count); step *s = &steps[index_++];
    assert(!strcmp(q->method,s->method) && strstr(q->url,s->url));
    if (q->json && strstr(q->url,"uploadType=resumable")) {
        starts++; cJSON *j = cJSON_Parse(q->json); assert(j);
        assert(!strcmp(cJSON_GetObjectItemCaseSensitive(j,"id")->valuestring,"new-file"));
        assert(!strcmp(cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(j,"parents"),0)->valuestring,"a-folder"));
        cJSON *details = cJSON_Parse(cJSON_GetObjectItemCaseSensitive(j,"description")->valuestring); assert(details);
        assert(!strcmp(cJSON_GetObjectItemCaseSensitive(details,"gameName")->valuestring,backup.game));
        assert(!strcmp(cJSON_GetObjectItemCaseSensitive(details,"titleId")->valuestring,backup.title));
        assert(!strcmp(cJSON_GetObjectItemCaseSensitive(details,"saveDirectory")->valuestring,backup.directory));
        assert(!strcmp(cJSON_GetObjectItemCaseSensitive(details,"backupUtc")->valuestring,backup.utc));
        assert(!strcmp(cJSON_GetObjectItemCaseSensitive(details,"checksum")->valuestring,backup.md5));
        assert(!strcmp(cJSON_GetObjectItemCaseSensitive(details,"archiveFormatVersion")->valuestring,"1"));
        assert(!strstr(q->json,"account") && !strstr(q->json,"userId") && !strstr(q->json,"token"));
        assert(q->total == backup.size);
        cJSON_Delete(details); cJSON_Delete(j);
    } else if (q->json) {
        creates++; assert(strstr(q->json,"PS4 Save Sync") && strstr(q->json,"appProperties"));
    }
    if (q->range) {
        assert(google_upload_session_url(q->url));
        if (q->file) {
            data_requests++;
            assert(q->offset == s->offset && q->length == s->length && q->length <= GOOGLE_UPLOAD_CHUNK);
            if (q->offset + q->length < backup.size) assert(q->length % GOOGLE_UPLOAD_CHUNK == 0);
            assert(fseeko(q->file,(off_t)q->offset,SEEK_SET) == 0);
            unsigned char bytes[4096]; uint64_t n = 0;
            while (n < q->length) {
                size_t size = q->length - n > sizeof(bytes) ? sizeof(bytes) : (size_t)(q->length - n);
                assert(fread(bytes,1,size,q->file) == size);
                for (size_t i = 0; i < size; i++) assert(bytes[i] == (q->offset+n+i)%251);
                n += size;
            }
        } else assert(!q->length && !strncmp(q->range,"bytes */",8));
    }
    r->transport = s->transport; r->status = s->status;
    if (s->json) r->json = cJSON_Parse(s->json);
    if (s->range) snprintf(r->range,sizeof(r->range),"%s",s->range);
    if (s->location) snprintf(r->location,sizeof(r->location),"%s",s->location);
    r->invalid_headers = s->invalid;
    if (s->cancel) cancelled_ = 1;
}
static google_upload_io io = {NULL,request,refresh,cancelled,progress,wait_};
static void reset(void)
{
    memset(steps,0,sizeof(steps)); count=index_=refreshes=waits=starts=creates=data_requests=0;
    cancelled_=0; refresh_ok=1;
}
static void add(const char *method, const char *url, long status, const char *json)
{
    steps[count++] = (step){.method=method,.url=url,.status=status,.json=json};
}
static void begin(int pages, int create)
{
    add("GET","q=",200,pages ? "{\"files\":[],\"nextPageToken\":\"p'&/\"}" : create ? "{\"files\":[]}" : "{\"files\":[{\"id\":\"a-folder\",\"createdTime\":\"2020-01-01T00:00:00.000Z\"}]}");
    if (pages) add("GET","pageToken=p%27%26%2F",200,"{\"files\":[{\"id\":\"0-folder\",\"createdTime\":\"2022-01-01T00:00:00Z\"},{\"id\":\"z-folder\",\"createdTime\":\"2020-01-01T00:00:00.001Z\"},{\"id\":\"a-folder\",\"createdTime\":\"2020-01-01T00:00:00Z\"}]}");
    if (create) {
        add("POST","files?fields=id",200,"{\"id\":\"new-racing-folder\"}");
        add("GET","q=",200,"{\"files\":[{\"id\":\"new-racing-folder\",\"createdTime\":\"2021-01-01T00:00:00.000Z\"},{\"id\":\"a-folder\",\"createdTime\":\"2020-01-01T00:00:00.000Z\"}]}");
    }
    add("GET","generateIds",200,"{\"ids\":[\"new-file\"]}");
    add("POST","uploadType=resumable",200,NULL); steps[count-1].location=SESSION;
}
static void chunk(uint64_t offset, uint64_t length, long status, const char *range, const char *json)
{
    add("PUT","upload_id=",status,json);
    steps[count-1].offset=offset; steps[count-1].length=length; steps[count-1].range=range;
}
static void run(int expected)
{
    assert(google_upload_run(&backup,&io) == expected && index_ == count);
    assert(starts <= 1 && creates <= 1);
}
static void normal(void)
{
    chunk(0,GOOGLE_UPLOAD_CHUNK,308,"bytes=0-262143",NULL);
    chunk(GOOGLE_UPLOAD_CHUNK,GOOGLE_UPLOAD_CHUNK,308,"bytes=0-524287",NULL);
    chunk(2*GOOGLE_UPLOAD_CHUNK,17,201,NULL,completion);
}
int main(void)
{
    assert(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK);
    strcpy(backup.archive,"build/host/upload.bin"); strcpy(backup.title,"CUSA12345");
    strcpy(backup.directory,"SAVE_\"\\quoted"); strcpy(backup.game,"Game \"quotes\" \\ slash\nUTF-8: \xc3\xa9");
    backup.user=42;
    FILE *fp=fopen(backup.archive,"wb"); assert(fp);
    for (unsigned i=0;i<2*GOOGLE_UPLOAD_CHUNK+17;i++) assert(fputc(i%251,fp)!=EOF);
    assert(!fclose(fp) && google_backup_hash(&backup,cancelled,NULL));
    snprintf(completion,sizeof(completion),"{\"id\":\"new-file\",\"size\":\"%" PRIu64 "\",\"md5Checksum\":\"%s\"}",backup.size,backup.md5);
    assert(google_upload_session_url(SESSION));
    const char *bad[]={"http://www.googleapis.com/upload/drive/v3/files?uploadType=resumable&upload_id=x",
        "https://www.googleapis.com.evil/upload/drive/v3/files?uploadType=resumable&upload_id=x",
        "https://evil@www.googleapis.com/upload/drive/v3/files?uploadType=resumable&upload_id=x",
        SESSION "#fragment", "https://www.googleapis.com:443/upload/drive/v3/files?uploadType=resumable&upload_id=x",
        "https://www.googleapis.com/upload/drive/v3/files?upload_id=x"};
    for (size_t i=0;i<sizeof(bad)/sizeof(*bad);i++) assert(!google_upload_session_url(bad[i]));
    reset(); begin(1,0); normal(); run(GOOGLE_UPLOAD_SUCCESS); assert(!creates);
    reset(); begin(0,1); normal(); run(GOOGLE_UPLOAD_SUCCESS); assert(creates==1);
    reset(); begin(0,0);
    chunk(0,GOOGLE_UPLOAD_CHUNK,0,NULL,NULL); steps[count-1].transport=CURLE_RECV_ERROR;
    add("PUT","upload_id=",308,NULL); steps[count-1].range="bytes=0-131071";
    chunk(131072,GOOGLE_UPLOAD_CHUNK,308,"bytes=0-393215",NULL);
    chunk(393216,131089,200,NULL,completion);
    run(GOOGLE_UPLOAD_SUCCESS); assert(waits==1);
    reset(); begin(0,0);
    chunk(0,GOOGLE_UPLOAD_CHUNK,401,NULL,NULL);
    add("PUT","upload_id=",308,NULL); /* no Range: server accepted zero bytes */
    normal(); run(GOOGLE_UPLOAD_SUCCESS); assert(refreshes==1);
    reset(); begin(0,0); normal(); steps[count-1].transport=CURLE_RECV_ERROR; steps[count-1].status=0;
    add("PUT","upload_id=",200,completion); run(GOOGLE_UPLOAD_SUCCESS); assert(data_requests==3);
    reset(); begin(0,0); normal(); steps[count-1].json="{}";
    add("GET","/new-file?",200,completion); run(GOOGLE_UPLOAD_SUCCESS);
    reset(); begin(0,0); normal(); steps[count-1].json="{}";
    add("GET","/new-file?",200,"{\"id\":\"new-file\",\"size\":\"1\",\"md5Checksum\":\"wrong\"}"); run(GOOGLE_UPLOAD_UNCERTAIN);
    reset(); begin(0,0); chunk(0,GOOGLE_UPLOAD_CHUNK,308,"bytes=0-999999",NULL); run(GOOGLE_UPLOAD_FAILED);
    reset(); begin(0,0); chunk(0,GOOGLE_UPLOAD_CHUNK,308,"bytes=1-2",NULL); run(GOOGLE_UPLOAD_FAILED);
    reset(); begin(0,0); steps[count-1].location=bad[1]; run(GOOGLE_UPLOAD_FAILED); assert(!data_requests);
    reset(); begin(0,0); chunk(0,GOOGLE_UPLOAD_CHUNK,308,"bytes=0-262143",NULL); steps[count-1].cancel=1;
    run(GOOGLE_UPLOAD_CANCELLED);
    reset(); begin(0,0); normal(); steps[count-1].cancel=1; steps[count-1].transport=CURLE_ABORTED_BY_CALLBACK;
    run(GOOGLE_UPLOAD_UNCERTAIN);
    reset(); begin(0,0); chunk(0,GOOGLE_UPLOAD_CHUNK,401,NULL,NULL); refresh_ok=0; run(GOOGLE_UPLOAD_FAILED);
    reset(); add("GET","q=",401,"{}"); refresh_ok=0; run(GOOGLE_UPLOAD_FAILED); assert(!starts && !creates);
    reset(); add("GET","q=",503,"{}"); run(GOOGLE_UPLOAD_FAILED); assert(!creates);
    reset(); cancelled_=1; run(GOOGLE_UPLOAD_CANCELLED);
    reset(); begin(0,0); normal(); steps[count-1].status=404; steps[count-1].json=NULL;
    add("GET","/new-file?",200,completion); run(GOOGLE_UPLOAD_SUCCESS);
    reset(); begin(0,0); normal(); steps[count-1].status=308; steps[count-1].json=NULL;
    steps[count-1].range="bytes=0-524304"; add("PUT","upload_id=",200,completion);
    run(GOOGLE_UPLOAD_SUCCESS); assert(data_requests==3);
    reset(); begin(0,0);
    for (unsigned i=0;i<4;i++) chunk(0,GOOGLE_UPLOAD_CHUNK,308,NULL,NULL);
    run(GOOGLE_UPLOAD_FAILED);
    reset(); begin(0,0); normal(); steps[count-1].transport=CURLE_RECV_ERROR; steps[count-1].status=0;
    add("PUT","upload_id=",401,"{}"); refresh_ok=0; run(GOOGLE_UPLOAD_UNCERTAIN);
    reset(); add("GET","q=",200,"{\"files\":[],\"nextPageToken\":123}");
    run(GOOGLE_UPLOAD_FAILED); assert(!creates);
    reset(); begin(0,0); steps[count-1].invalid=1; run(GOOGLE_UPLOAD_FAILED);
    assert(!remove(backup.archive)); curl_global_cleanup();
    puts("Google upload protocol tests passed (mock Drive; real JSON/hash/streaming).");
    return 0;
}
