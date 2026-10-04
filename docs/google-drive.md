# Google Drive connection, backup browsing and downloads

The app connects Google Drive, uploads one selected PS4 HDD save, and browses
and downloads marked backups into private temporary storage. Restoration, bulk
uploads, and automatic sync are not implemented.
Package identity remains `PSSY00001` /
`IV0000-PSSY00001_00-PS4SAVESYNC00000`, separate from Apollo. Existing upstream
credits and licenses remain in place.

Console testing reported by the owner confirms Google connection, Drive
`files.list`, refresh after a full app restart, local disconnect, and reconnect.
The owner also confirmed two separate ZIP uploads of the same save, verified
the archive contents, and verified the original game save. Those are reported
console upload results; browsing/download PS4 build and runtime verification
remain pending.

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

Settings contains **Google Drive Backups**, **Connect Google Drive**, **Connection Status**, and
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
For the PS4 mbedTLS backend, the CA bundle is read sequentially in bounded chunks
(512 KiB maximum), without seek operations, and NUL-terminated for
`mbedtls_x509_crt_parse`. Any nonzero parser result, including partial parsing,
fails closed. The verified mbedTLS backend receives the parsed trust chain through
libcurl's SSL-context callback. `CAINFO` and `CAPATH` are NULL to bypass only the
built-in certificate-file loader; peer and hostname verification remain enabled.
The callback changes only the trust chain, preserving curl's other validation.
Each request owns its chain until after `curl_easy_cleanup`; fresh connections,
disabled reuse, and disabled TLS session caching prevent contexts from escaping
that lifetime. Other TLS backends are refused before casting the context.
Unauthenticated discovery failures have a dedicated screen diagnostic: failed
libcurl option or transport error number and `curl_easy_strerror` text, non-200
HTTP status, malformed/non-object JSON, or missing/unexpected device endpoint.
The screen also reports whether the packaged CA file exists/is readable and
libcurl's version/TLS backend. Discovery uses a `CURL_ERROR_SIZE` error buffer
kept alive through `curl_easy_cleanup`. Only allowlisted error reasons and a
strictly formatted mbedTLS code reach the UI; raw error text, URLs/proxy details,
headers, request bodies, and response bodies are withheld. Successful discovery
clears these details before device authorization. CA loading/parser/context
failures also show safe local diagnostics during authenticated requests: bytes
read, parser result, and callback status, without their error buffers or response
bodies. Diagnostics wrap within the existing Google screen.
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

## Single-save upload

An individual PS4 HDD save has **Back up to Google Drive** under File Backup.
Confirmation identifies its game, title ID, and save directory. This action is
not offered for trophies, locked USB saves, or other platforms. The existing
Google screen shows preparation/upload status, bytes and percentage, and Cancel.
It blocks other app save/settings operations while the worker runs. A failed
save unmount blocks further operations until app restart.

The worker copies the selected metadata before starting and never depends on
`selected_entry` or its temporary path. It uses the existing HDD mounting pipeline
and shared `zip_directory` routine with exactly the `zipSave`/FTP relative layout:
`SAVE_DIRECTORY/...`, including `sce_sys`. No owner index/XML sidecars are added.
The shared ZIP walker now propagates enumeration/file/close errors instead of
reporting partial archives as successful; libzip cancellation covers compression.
It refuses symlinks and special files. All operations read source files; no
resigning, SFO edits, or writes to the selected save are performed.

Each ZIP lives in an exclusively created `cache/drive-XXXXXX/backup.zip`. Export
and game-save unmount finish before hashing, mounting GoogleAuth, or networking.
The worker hashes sequentially with bounded buffers and cleans only its own ZIP
and directory on every exit. It does not recursively remove unknown files. A
cleanup failure is reported alongside the upload outcome. App termination or a
power loss can leave a cache directory; the existing explicit cache-clean action
can remove it after restarting, with no upload active.

Folder discovery pages through non-trashed, root-parent My Drive folders marked
with private `appProperties.ps4SaveSync = ps4-save-sync.v1`. A folder named
**PS4 Save Sync** with that marker is created only after a complete empty search.
The same OAuth client/account reuses it across consoles, even if its name changes.
If several marked folders exist, both consoles select the oldest creation time,
with file ID as a deterministic tie-breaker. After creating a folder the worker
performs another complete search before choosing the parent, resolving concurrent
creators consistently. Drive does not provide atomic unique-folder creation:
simultaneous first use can leave an extra empty marked folder; no folders are
deleted automatically. Folder creation is not retried after a lost response.
See Google's [folder guide](https://developers.google.com/workspace/drive/api/guides/folder)
and [private properties guide](https://developers.google.com/workspace/drive/api/guides/properties).

Each backup uses a new [pre-generated Drive file ID](https://developers.google.com/workspace/drive/api/guides/create-file#generate_ids_to_use_with_your_files)
and a POST creation request. Earlier backups are never overwritten. Metadata
version 1 is JSON in the file's `description`: gameName, titleId, saveDirectory,
backupUtc (UTC ISO 8601), archiveFormat (`apollo-decrypted-zip`),
archiveFormatVersion (1), byteSize, checksumAlgorithm (`md5`), and checksum.
JSON escaping preserves game names without the 124-byte app-property limit.
The backup also carries the stable app marker and backupVersion in appProperties.
No credentials, hardware IDs, or account/user IDs are added to metadata. The ZIP
retains the existing decrypted export contents, including the game's original SFO.
MD5 is used to compare against Drive's native `md5Checksum`, not as authentication.

The [documented resumable upload protocol](https://developers.google.com/workspace/drive/api/guides/manage-uploads#resumable)
uses 256 KiB chunks (except the final chunk), streamed from disk with at most
16 KiB per curl read callback. Session URLs require the exact HTTPS Google origin
and upload path, with resumable/upload_id query parameters, before authorization
is sent. Redirects, verbose logging, and session-URL logging remain disabled;
all requests retain verified mbedTLS trust and hostname checks.

Server Range responses determine the next offset. Interrupted/5xx/rate-limited
requests use bounded backoff and empty PUT status probes. A 401 refreshes using
the existing rotation-safe store and probes status before resending bytes.
Expired sessions and terminal errors can check the allocated file ID; they never
start a replacement upload. Success requires HTTP completion (or a confirming
file lookup), matching file ID, size, and MD5. Unverified completion, lost final
responses, and cancellation during the final chunk are reported as **uncertain**.
Check Drive before retrying that outcome; a manual new attempt creates another
backup. Sessions are not persisted or resumed after app restart. Cancellation
stops local work; it does not revoke OAuth or delete remote files.

## Host checks

On Linux with a C compiler, libcurl, mbedTLS, libzip, and SDL2 development headers
(`libcurl4-openssl-dev libmbedtls-dev libzip-dev libsdl2-dev` on Ubuntu):

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
files, and deletion. These are **not PS4 ABI, live TLS transport, or runtime tests**.
Discovery tests also cover failed setopt/status lookup, error-buffer lifetime
through cleanup, sanitized TLS/proxy details, CA presence/readability, HTTP/JSON
failure distinctions and endpoint validation. CA tests use the real bundled
certificates, parser, and SSL configuration with mocked backend/file failures:
short sequential reads, empty/oversized/NUL/malformed/partially parsed bundles,
allocation/read/close failures, backend mismatch, missing callback/context,
reuse-guard failure, unchanged configuration outside trust anchors, and chain
lifetime through curl cleanup. Authenticated failures expose only local CA
diagnostics. The reported certificate-file I/O failure's exact operation remains
unconfirmed. The owner has since confirmed working console authentication with
the in-memory CA path; host checks remain simulations.

Upload host tests cover real archive entry names and contents, source preservation,
unique staging paths, cancellation and failures with required unmount, no token
mount/networking after staging failure, private credential guards, metadata
escaping, folder creation/reuse/pagination, chunk alignment and partial-offset
recovery, 401 refresh failure, lost final-response recovery without duplicate
creation, invalid session/header rejection, and size/checksum verification. The
production curl read/header callbacks are exercised with mocked transport, real
disk reads, and the same verified CA setup. PS4 save mounts and live Drive transfers remain outside the host checks.
Download host tests cover listing and escaped page tokens, invalid/cyclic tokens,
metadata/version/date/size/checksum rejection, refresh failures, streamed disk
transfers, cancellation, HTTP/network/short-read failures, checksum mismatch,
and unsafe paths, symlinks, special files, malformed ZIPs and CRC validation.
Production curl download callbacks are checked for secure TLS options, progress,
oversized responses, multiplication overflow, cancellation, and disk-write failure.

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

## Google Drive Backups

Open **Google Drive Backups** in Settings using the connected PS4 user. The
worker refreshes that user's existing credentials and searches the same private
marker and canonical oldest root folder used by uploads; it never creates a
folder or writes to Drive. A missing folder or failed search is reported. The
`drive.file` scope is unchanged; backups made with another OAuth client or an
unconnected account may be inaccessible.

The browser requests ten files per page, ordered by Drive creation time newest
first. It shows game/title ID, and the selected save directory, backup UTC and
byte size. Up/Down selects a row, Confirm downloads it, R1 fetches the next page,
and Cancel returns. Empty pages can still have a next page. Reopen the entry to
restart from page one. Malformed backups are excluded and counted on that page;
invalid page tokens and a token repeating the current page fail closed. The
folder search retains its existing bounded pagination. File names are never
used for parsing, paths or formatting. Descriptions are bounded untrusted JSON,
with duplicate keys, embedded NULs, control characters, unknown versions,
invalid UTF-8 or Unicode direction/control characters, invalid dates/title
IDs/directory components, overflowing sizes, and conflicting
Drive/backup size or MD5 rejected. Only the existing metadata version 1 and
`apollo-decrypted-zip` format version 1 are supported.

Selection triggers a fresh metadata lookup and requires it to match the listed
backup. The media GET streams directly to a generated
`cache/drive-XXXXXX/backup.zip`, never to a remote-supplied filename. Every write
is capped by the expected size, and the curl response byte count, disk byte count
and sequential MD5 must match both Drive metadata and the backup description.
Metadata requests retain the existing timeouts; media downloads use a ten-second
connection timeout and abort after thirty seconds without useful transfer rather
than limiting the entire download to thirty seconds. Controller rendering stays
on the main thread; cancellation interrupts network, hash and archive reads.
An HTTP 401 permits one rotation-safe token refresh and a clean restart of that
request. Other failures require a manual retry; no range resume is implemented.

Validation opens ZIPs read-only and checks raw central/local filenames plus
libzip consistency. Embedded NULs, absolute/traversal/backslash/drive paths,
symlinks and special files, duplicate names or file/directory conflicts,
encryption, and entries outside the exact save-directory root are rejected.
The archive must include a nonempty `SAVE_DIRECTORY/sce_sys/param.sfo` and regular
save files. Each entry is streamed through libzip to verify sizes and CRCs,
without extraction. The v1 reader supports single-disk ZIPs without ZIP64 or
trailing data, at most 4096 entries, 4 GiB per uncompressed file and 16 GiB total
expanded bytes. Compression is limited to stored/deflated entries, central
directory metadata to 8 MiB, extra fields to 4096 bytes and per-entry comments
to 1024 bytes. These conservative limits bound validation work; unsupported
archives are not marked ready.

Completion identifies title ID, save directory and backup time, and explicitly
reports whether validation passed. A successful ZIP stays in private temporary
storage until another browse/download starts or normal app shutdown. Failures
and cancellation remove only the job's fixed ZIP and exclusive directory;
cleanup errors are shown. Abrupt termination can leave an isolated cache entry
for the existing cache-clean action after restart. No game save is mounted,
modified, overwritten, extracted or restored by browsing/downloading.
MD5/CRC consistency checks detect transfer/archive corruption; they do not
provide authenticity against an account owner editing both content and metadata.

## Verify browsing and download on PS4 (pending)

1. Build/install this milestone with OpenOrbis and the existing verified TLS and
   libzip dependencies. Host results do not assert PS4 ABI or installation success.
2. Under the connected user, open **Google Drive Backups**. Confirm the two
   reported uploads are separate selectable rows with the correct game, title
   ID, directory, timestamps and sizes. Check a renamed folder/file still works.
3. Download each backup and expect **validation passed. Temporary ZIP ready.**
   Compare the private ZIP's size/MD5 and contents with the previously verified
   backups using development tooling. Confirm the original game save still loads.
4. With more than ten backups, test R1 pagination (including a page with invalid
   entries), selection, restarting from page one, and empty/missing-folder results.
5. Cancel during transfer, hashing and ZIP verification; disconnect networking,
   fill the cache storage, and verify responsive UI plus partial-file cleanup.
   Confirm a corrupt ZIP or mismatched metadata never reports ready.
6. Repeat after full app restart and under another PS4 user. Verify token refresh,
   per-user credentials, account isolation, disconnect/reconnect, no overlapping
   game/GoogleAuth mounts, and no credentials or response bodies in debug logs.
7. Verify successful-cache cleanup on the next browse and normal shutdown, then
   exercise existing save export/upload/settings behavior for regressions.

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
