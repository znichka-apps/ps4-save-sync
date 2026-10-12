#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include "google_restore_log.h"

#ifndef GOOGLE_RESTORE_LOG_PATH
#define GOOGLE_RESTORE_LOG_PATH "/data/ps4-save-sync/google_restore.log"
#endif

static char last_failure[192];
static unsigned attempt_sequence;
static char attempt[48] = "unmarked";

void google_restore_boundary(const char *step, int curl_result, long http_status,
    unsigned long long bytes)
{
    int saved=errno;
    char line[192];
    int n=snprintf(line,sizeof(line),"attempt=%s step=%s curl=%d http=%ld bytes=%llu\n",
        attempt,step,curl_result,http_status,bytes);
    if (n>0 && (size_t)n<sizeof(line)) {
        int fd=open(GOOGLE_RESTORE_LOG_PATH ".boundary",O_WRONLY|O_CREAT|O_TRUNC,0600);
        if (fd>=0) {
            size_t offset=0;
            while (offset<(size_t)n) {
                ssize_t written=write(fd,line+offset,(size_t)n-offset);
                if (written<0 && errno==EINTR) continue;
                if (written<=0) break;
                offset+=(size_t)written;
            }
            (void)fsync(fd);
            (void)close(fd);
        }
    }
    errno=saved;
}

void google_restore_begin_attempt(void)
{
    int saved=errno;
    snprintf(attempt,sizeof(attempt),"%lld-%u",(long long)time(NULL),++attempt_sequence);
    last_failure[0]=0;
    google_restore_boundary("attempt begin",0,0,0);
    int fd=open(GOOGLE_RESTORE_LOG_PATH,O_WRONLY|O_CREAT|O_TRUNC,0600);
    if (fd>=0) {
        char line[96];
        int n=snprintf(line,sizeof(line),"restore attempt=%s begin\n",attempt);
        if (n>0 && (size_t)n<sizeof(line)) {
            size_t offset=0;
            while (offset<(size_t)n) {
                ssize_t written=write(fd,line+offset,(size_t)n-offset);
                if (written<0 && errno==EINTR) continue;
                if (written<=0) break;
                offset+=(size_t)written;
            }
            (void)fsync(fd);
        }
        (void)close(fd);
    }
    errno=saved;
}

void google_restore_clear_failure(void)
{
    last_failure[0]=0;
}

void google_restore_last_failure(char *out, size_t capacity)
{
    if (capacity) snprintf(out,capacity,"%s",last_failure);
}

void google_restore_log(const char *stage, int op, int error, int zip_error,
    int native, const char *call, int failed)
{
    int saved=errno;
    char line[384];
    int n=snprintf(line,sizeof(line),"restore attempt=%s stage=%s op=%d errno=%d zip=%d native=%d call=%s result=%s\n",
        attempt,stage,op,error,zip_error,native,call,failed?"failed":"ok");
    if (failed && !last_failure[0]) snprintf(last_failure,sizeof(last_failure),
        "Restore %s [op=%d errno=%d zip=%d native=%d call=%s]. ZIP retained.",
        stage,op,error,zip_error,native,call);
    if (n>0 && (size_t)n<sizeof(line)) {
        int fd=open(GOOGLE_RESTORE_LOG_PATH,O_WRONLY|O_CREAT|O_APPEND,0600);
        if (fd>=0) {
            size_t offset=0;
            while (offset<(size_t)n) {
                ssize_t written=write(fd,line+offset,(size_t)n-offset);
                if (written<0 && errno==EINTR) continue;
                if (written<=0) break;
                offset+=(size_t)written;
            }
            (void)fsync(fd);
            (void)close(fd);
        }
    }
    errno=saved;
}
