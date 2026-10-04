#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/host/include build/host/store
# Tests never embed local OAuth registration values.
printf '%s\n' '#define GDRIVE_CLIENT_ID "synthetic-client"' '#define GDRIVE_CLIENT_SECRET "synthetic-secret"' > build/host/include/google_build_config.h
cc -std=gnu11 -Wall -Wextra -Werror -Wno-deprecated-declarations -fsanitize=address,undefined -g \
  -Ibuild/host/include -Iinclude tools/test_google_drive.c source/cJSON.c -lcurl -lSDL2 -lm -o build/host/test_google_drive
build/host/test_google_drive
cc -std=gnu11 -Wall -Wextra -Werror -fsanitize=address,undefined -g \
  -Itools/host_include -Iinclude tools/test_google_store.c -o build/host/test_google_store
build/host/test_google_store
