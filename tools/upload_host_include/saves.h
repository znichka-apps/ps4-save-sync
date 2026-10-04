/* Minimal host platform shim for the owned Google save adapter; no PS4 ABI claim. */
#ifndef UPLOAD_HOST_SAVES_H
#define UPLOAD_HOST_SAVES_H
#include <stdint.h>
#define APOLLO_SANDBOX_PATH "build/host/mount/%s/"
#define SAVE_FLAG_HDD 1024
#define SAVE_FLAG_PS4 16
#define FILE_TYPE_PS4 1
typedef struct { char *name,*title_id,*path,*dir_name; uint32_t blocks; uint16_t flags,type; void *codes; } save_entry_t;
extern struct test_app_config { uint32_t user_id; } apollo_config;
int orbis_SaveMount(const save_entry_t*,uint32_t,char*);
int orbis_SaveUmount(const char*);
#endif
