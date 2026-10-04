#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/host/include build/host/store
# Tests never embed local OAuth registration values.
printf '%s\n' '#define GDRIVE_CLIENT_ID "synthetic-client"' '#define GDRIVE_CLIENT_SECRET "synthetic-secret"' > build/host/include/google_build_config.h
cc -std=gnu11 -Wall -Wextra -Werror -Wno-deprecated-declarations -fsanitize=address,undefined -g \
  -Ibuild/host/include -Iinclude tools/test_google_drive.c source/google_upload.c source/google_backup.c source/cJSON.c -lcurl -lSDL2 -lmbedtls -lmbedx509 -lmbedcrypto -lm -o build/host/test_google_drive
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
  source/google_save.c source/google_backup.c source/save_zip.c -lzip -lmbedcrypto -o build/host/test_google_save
build/host/test_google_save
