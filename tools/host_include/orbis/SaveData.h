/* Host-test shim only. Not an SDK header or PS4 ABI validation. */
#ifndef HOST_SAVE_DATA_H
#define HOST_SAVE_DATA_H
#include <stdint.h>
#define ORBIS_SAVE_DATA_BLOCKS_MIN2 96
#define ORBIS_SAVE_DATA_MOUNT_MODE_RDWR 2
#define ORBIS_SAVE_DATA_MOUNT_MODE_CREATE2 32
typedef struct { char data[32]; } OrbisSaveDataDirName;
typedef struct { char data[16]; } OrbisSaveDataMountPoint;
typedef struct {
    int32_t userId;
    uint32_t unk1;
    const OrbisSaveDataDirName *dirName;
    uint64_t blocks;
    uint32_t mountMode;
    uint8_t reserved[32];
    uint32_t unk2;
} OrbisSaveDataMount2;
typedef struct { char mountPathName[16]; } OrbisSaveDataMountResult;
int32_t sceSaveDataMount2(const OrbisSaveDataMount2*, OrbisSaveDataMountResult*);
int32_t sceSaveDataUmount(OrbisSaveDataMountPoint*);
#endif
