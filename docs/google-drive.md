# Google Drive connection milestone

This milestone connects Google Drive and checks `files.list` only. It does not
upload, download, or restore saves. Package identity remains `PSSY00001` /
`IV0000-PSSY00001_00-PS4SAVESYNC00000`, separate from Apollo. Existing upstream
credits and licenses remain in place.

## Build registration

Create a Google OAuth client of type **TVs and Limited Input devices**, enable
the Drive API, and configure the consent screen/test users as appropriate.
The implementation follows Google's [device-flow documentation](https://developers.google.com/identity/protocols/oauth2/limited-input-device)
and requests only `https://www.googleapis.com/auth/drive.file`.

Generate the build header locally, using the existing credentials JSON outside
the repository:

```sh
python3 tools/configure_google.py --credentials /external/path/client_secret.json
```

Alternatively set `GDRIVE_CLIENT_ID` and `GDRIVE_CLIENT_SECRET` in the environment
and run `python3 tools/configure_google.py`. The generated
`include/google_build_config.h` and temporary file are ignored. Neither the
downloaded JSON nor refresh/access tokens are copied into the repository.
Missing/invalid registration fails with a sanitized error. Regenerate the header
when changing clients; Make tracks it as a dependency of the Google module.

GitHub Actions obtains registration exclusively from repository secrets named
`GDRIVE_CLIENT_ID` and `GDRIVE_CLIENT_SECRET`. Fork PRs without those secrets fail
clearly at configuration. The workflow has not been run for this change. Google
explains that distributed device clients cannot keep a registration secret
confidential; these values will necessarily be present in the installed binary.

## Console behavior

Settings contains **Connect Google Drive**, **Connection Status**, and
**Disconnect Google Drive**. Connect displays Google's returned verification URL
and case-sensitive code while a worker polls at the returned interval. A
`slow_down` response adds five seconds to subsequent intervals. The console's
Cancel button (Circle or Cross, shown on screen) cancels polling or network work;
after the result, Cancel returns to Settings. Local
credential updates finish atomically before leaving the result screen. Other
settings/save actions are suspended while this screen is open, preventing
overlapping app save-data mounts; rendering and controller input continue.

Connection Status reads this PS4 user's saved refresh token, refreshes access,
and requests `GET /drive/v3/files?pageSize=1&fields=files(id)`. A 401 triggers one
refresh and retry. Access tokens are never persisted. An empty `files` array is
a successful check: `drive.file` exposes files authorized for this application,
not the user's complete Drive. Expired/revoked refresh credentials are reported
without deleting them automatically; disconnect and connect again.
Replacement refresh tokens are saved immediately on refresh, so a subsequent
Drive failure or cancellation cannot discard a rotated credential.

The Google module uses fresh libcurl handles with peer and hostname verification
required, TLS 1.2 minimum, HTTPS-only protocols, no redirects or verbose logging,
and a packaged Mozilla CA bundle. It never calls Apollo's insecure HTTP helper.
Certificate failure, missing CA file, or a wrong console clock fails closed.
Responses are bounded and parsed using unmodified cJSON 1.7.19. Token-bearing
application buffers are wiped before freeing; secrets/responses never enter
debug logs. Network requests have 10-second connection and 30-second total
timeouts and cancellation callbacks; polling waits check cancellation every 50ms.

Refresh credentials live in `GoogleAuth/refresh_token` in this application's own
PS4 save-data container, mounted with the captured local PS4 user ID. This is
separate from the unchanged `app_config_t` / `Settings/settings.bin`. Writes use
a sibling temporary file, flush, file fsync, and rename before unmounting. The
save list excludes this private directory; generic save mounts and command
execution also refuse it, preventing app save exports/backups from including it.
Disconnect removes this user's token and leftover temporary file, then unmounts.
It sends no revocation request, so another console's authorization is unaffected.

## Host checks

On Linux with a C compiler, libcurl development headers, and SDL2 development
headers:

```sh
sh tools/test_google_host.sh
```

The runner builds with `-Wall -Wextra -Werror` and address/undefined-behavior
sanitizers, using synthetic registration values. It tests the production OAuth
worker/JSON parsing with mocked HTTP: pending, slowdown timing, denial,
expiration, cancellation, TLS/network failure, malformed/missing token responses,
Drive failure, persistence failure, refresh, 401 retry, missing credentials,
invalid grant, rotation retained after Drive failure/cancellation, and local-only
disconnect. It asserts secure transport options
on every mocked request. Store tests use a host SDK shim and mocked mounts but
real file operations, including per-user isolation, fsync/rename failures
preserving previous credentials, mount/unmount failures, malformed credential
files, and deletion. These are **not PS4 ABI, TLS-backend, or runtime tests**.

## Remaining PS4 verification

Build the package with OpenOrbis and the existing libcurl/mbedTLS dependencies,
install it, and verify:

- Package identity and existing save/settings operations are unchanged.
- Packaged CA path works with the console's mbedTLS backend; missing/untrusted CA
  and incorrect hostname certificates fail, with no insecure fallback.
- URL/code readability (including 40-character URLs and 15 `W` characters),
  controller responsiveness, cancel, denial, expiration, and network loss.
- Authorization and Drive check; relaunch refresh; per-user isolation; local
  disconnect; a second PS4 stays connected.
- Mount/unmount, fsync, and rename behavior on PS4 save data, including failure
  and interrupted-write recovery. Export/backup flows omit `GoogleAuth`.
- Debug logging enabled still emits no credentials or token responses.

No PS4 build, installation, or runtime success is asserted by the host checks.

## Credential exclusion

The downloaded OAuth JSON remains outside the repository. Ignore rules also
exclude `client_secret*.json`, credentials/token JSON, local environment files,
`access_token`, `refresh_token`, their temporary files, and generated Google build
headers. Build outputs and host-test artifacts are ignored. Only synthetic
credentials appear in committed tests; the smoke test keeps real tokens in
memory and emits only sanitized results.

## GP4 packaging

`create-gp4` output is completed by `tools/ensure_gp4_dirs.py` before it is
published as `pkg.gp4`. Every parent directory in a file's `targ_path` receives a
nested `<dir targ_name="...">` declaration under `<rootdir>`, including
`assets/google`. Existing directories, metadata, namespace declarations, comments,
and file entries are preserved. Makefile/helper dependencies trigger regeneration
without being included in the packaged file list. Temporary GP4 output is ignored
and only replaces the final manifest after the repair succeeds.

[LibOrbisPkg's filesystem builder](https://github.com/maxton/LibOrbisPkg/blob/master/LibOrbisPkg/PFS/PfsProperties.cs)
loads directory declarations separately and throws in `FindDir` when a file's
parent is missing. The CA file remains `assets/google/cacert.pem`, matching the
installed path in `google_drive.c`; TLS verification remains required.

Run `python3 tools/test_ensure_gp4_dirs.py` for representative nested paths,
missing/empty rootdir, metadata/file preservation, invalid-path failure, and
idempotence. These tests also run in Actions. No failing generated GP4 was
available locally; the helper addresses the directory invariant behind the
reported exception. GitHub Actions must verify the actual PKG build.
