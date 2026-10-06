#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/host/include build/host/store
# Tests never embed local OAuth registration values.
printf '%s\n' '#define GDRIVE_CLIENT_ID "synthetic-client"' '#define GDRIVE_CLIENT_SECRET "synthetic-secret"' > build/host/include/google_build_config.h
cc -std=gnu11 -Wall -Wextra -Werror -Wno-deprecated-declarations -fsanitize=address,undefined -g \
  -Ibuild/host/include -Iinclude tools/test_google_drive.c source/google_upload.c source/google_download.c source/google_backup.c source/cJSON.c -lzip -lcurl -lSDL2 -lmbedtls -lmbedx509 -lmbedcrypto -lm -o build/host/test_google_drive
build/host/test_google_drive
cc -std=gnu11 -Wall -Wextra -Werror -Wno-deprecated-declarations -fsanitize=address,undefined -g \
  -Iinclude tools/test_google_ca.c -lcurl -lmbedtls -lmbedx509 -lmbedcrypto -o build/host/test_google_ca
build/host/test_google_ca
cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -Itools/host_include -Iinclude tools/test_google_store.c -o build/host/test_google_store
build/host/test_google_store
cc -std=gnu11 -Wall -Wextra -Werror -Wno-deprecated-declarations -fsanitize=address,undefined -g \
  -Iinclude tools/test_google_upload.c source/google_upload.c source/google_backup.c source/cJSON.c \
  -lcurl -lmbedcrypto -o build/host/test_google_upload
build/host/test_google_upload
cc -std=gnu11 -Wall -Wextra -Werror -Wno-deprecated-declarations -fsanitize=address,undefined -g \
  -DGOOGLE_BACKUP_CACHE='"build/host/cache/"' \
  -Itools/upload_host_include -Itools/host_include -Iinclude tools/test_google_save.c \
  source/google_save.c source/google_backup.c source/save_zip.c -lzip -lmbedcrypto \
  -Wl,--wrap=mkdtemp,--wrap=time -o build/host/test_google_save
build/host/test_google_save
cc -std=gnu11 -Wall -Wextra -Werror -Wno-deprecated-declarations -fsanitize=address,undefined -g \
  -D__PS4__ -DGOOGLE_BACKUP_CACHE='"build/host/cache/"' \
  -Itools/upload_host_include -Itools/host_include -Iinclude tools/test_google_save.c \
  source/google_save.c source/google_backup.c source/save_zip.c -lzip -lmbedcrypto \
  -Wl,--wrap=mkdtemp,--wrap=time -o build/host/test_google_save_ps4
build/host/test_google_save_ps4
for platform in host ps4; do
  platform_flag=''
  if [ "$platform" = ps4 ]; then platform_flag='-D__PS4__'; fi
  cc -std=gnu11 -Wall -Wextra -Werror -Wno-deprecated-declarations -fsanitize=address,undefined -g \
    $platform_flag -Iinclude tools/test_save_zip.c source/save_zip.c -lzip \
    -Wl,--wrap=zip_open,--wrap=zip_register_cancel_callback_with_state,--wrap=opendir,--wrap=readdir,--wrap=lstat \
    -Wl,--wrap=zip_add_dir,--wrap=zip_source_file,--wrap=zip_add,--wrap=zip_file_set_external_attributes,--wrap=closedir,--wrap=zip_close \
    -o build/host/test_save_zip_$platform
  build/host/test_save_zip_$platform
done

cc -std=gnu11 -Wall -Wextra -Werror -Wno-deprecated-declarations -fsanitize=address,undefined -g \
  -DGOOGLE_BACKUP_CACHE='"build/host/cache/"' -Iinclude tools/test_google_download.c \
  source/google_download.c source/google_backup.c source/cJSON.c -lzip -lcurl -lmbedcrypto -o build/host/test_google_download
build/host/test_google_download

cc -std=gnu11 -Wall -Wextra -Werror -Wno-deprecated-declarations -fsanitize=address,undefined -g \
  -DGOOGLE_BACKUP_CACHE='"/tmp/ps4-save-sync-restore-cache/"' -Iinclude tools/test_google_restore_stage.c \
  source/google_restore.c source/restore_fs.c source/google_download.c source/google_backup.c source/cJSON.c \
  -lzip -lcurl -lmbedcrypto -Wl,--wrap=rmdir -o build/host/test_google_restore_stage
build/host/test_google_restore_stage

cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -DGOOGLE_REPLACE_ROOT='"/tmp/ps4-save-sync-replace-test/"' -Iinclude \
  tools/test_google_replace.c source/google_replace.c -o build/host/test_google_replace
build/host/test_google_replace
cc -std=gnu11 -Wall -Wextra -Werror -D__PS4__ \
  -Itools/restore_host_include -Itools/host_include -Iinclude \
  -c source/google_replace_ps4.c -o build/host/google_replace_ps4.o

cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -Itools/restore_host_include -Iinclude tools/test_save_create.c -o build/host/test_save_create
build/host/test_save_create

cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -D__PS4__ \
  -Itools/restore_host_include -Itools/host_include -Iinclude \
  tools/test_save_mount_ps4.c source/save_mount.c source/restore_fs.c tools/restore_sdk_mock.c \
  -lsqlite3 -Wl,--wrap=lstat,--wrap=fstatat,--wrap=mkdirat -o build/host/test_save_mount_ps4
build/host/test_save_mount_ps4

cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -Itools/restore_host_include -Iinclude tools/test_google_restore_ps4.c -lmbedcrypto \
  -o build/host/test_google_restore_ps4
build/host/test_google_restore_ps4
cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g -D__PS4__ \
  -Itools/restore_host_include -Itools/host_include -Iinclude \
  tools/test_google_replace_ps4.c -o build/host/test_google_replace_ps4
build/host/test_google_replace_ps4

cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -Itools/restore_host_include -Iinclude tools/test_copy_cleanup.c source/common.c \
  -Wl,--wrap=malloc -lz -o build/host/test_copy_cleanup
build/host/test_copy_cleanup
