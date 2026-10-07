# Google Drive connection, backup browsing, downloads and restore

The app connects Google Drive, uploads one selected PS4 HDD save, and browses
and downloads marked backups into private temporary storage. A verified selected
backup can be restored into an empty save slot for the current local PS4 user.
A separate explicit, best-effort replacement path exists for an occupied slot.
Replacement is not atomic: power loss can leave the save missing or partial.
Bulk uploads and automatic sync are not implemented.
Package identity remains `PSSY00001` /
`IV0000-PSSY00001_00-PS4SAVESYNC00000`, separate from Apollo. Existing upstream
credits and licenses remain in place.

## How to use in the current UI

Open **Google Drive** from the main screen and press Square for the two-page
**How to use** guide. It is also listed in Settings.

1. In Settings, choose **Connect Google Drive** and complete the displayed
   verification URL and user code.
2. In **HDD Saves**, select a save and choose **Back up to Google Drive**.
3. Open **Google Drive**, select a backup, and Confirm to download it. The app
   checks its checksum, ZIP contents, and SFO before making it ready.
4. Confirm again to restore into an **empty** save slot. An existing target is
   refused. The backup remains in Drive; failed restores retain the ZIP.
5. Triangle on a ready download offers **Replace** for an occupied slot. Before
   using it, keep a separate copy of the current save. Replace uploads and
   verifies a rollback backup, then removes the target. It is not atomic and
   recovery is best effort. Trying an empty disposable slot and verifying it on
   the PS4 is the safer path.

For cross-console use, the Andrey profiles on both consoles need the **same
Apollo offline Account ID**. Use **User Tools > Activate PS4 Accounts** for an
unactivated profile, then reboot. This does not require PSN sign-in; Chiaki can
still use the activated account. If account-ID lookup fails, check
`/data/ps4-save-sync/google_restore.log`, activate the local account if needed,
reboot, and retry. Do not use a backup's account ID or zero as a substitute.

Console testing reported by the owner confirms Google connection, Drive
`files.list`, refresh after a full app restart, local disconnect, and reconnect.
The owner also confirmed two separate ZIP uploads of the same save, verified
the archive contents, and verified the original game save. Those are reported
console upload results. The owner reports browsing and downloaded-backup
validation on PS4 as well. The owner reports a successful empty-slot restore
on a second PS4 and that the game loaded it. Replacement and recovery are not
yet verified on PS4.

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
**PS4 Cloud Save by Znichka** with that marker is created only after a complete
empty search. Existing marked folders keep their old names and remain usable.
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
(`libcurl4-openssl-dev libmbedtls-dev libzip-dev libsdl2-dev libsqlite3-dev` on Ubuntu):

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
Restore tests use actual ZIP/SFO fixture bytes, disk writes and SQLite queries:
absent targets, existing keys, orphan volumes, DB-only rows, inaccessible DBs,
wrong users, SFO title/directory mismatch and malformed offsets, archive changes,
unsafe paths and links, copy errors, cancellation before/during/after writes,
ownership and metadata-update failures, unmount failure, and success-boundary
cancellation. Separate tests exercise the real exclusive creation code with
mocked kernel/crypto/image functions, racing key/volume creation and write,
sync/close failures. The PS4 adapter's ownership fields and PSID HMAC are checked
after mocked SFO patch/read errors and incorrectly patched values. Transport
tests verify operation exclusion and per-user restore/discard gates. Platform
mounts, image creation, actual SFO patching on PFS, and game loadability still
require a real PS4.

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
storage until restore succeeds, the user explicitly discards/cancels the ready
download, or normal app shutdown. A ready download blocks another Google
operation until it is restored or discarded. Download failures
and cancellation remove only the job's fixed ZIP and exclusive directory;
cleanup errors are shown. Abrupt termination can leave an isolated cache entry
for the existing cache-clean action after restart. Browsing/downloading alone
does not mount, modify, overwrite or extract a game save.
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
7. Verify ready-cache cleanup on explicit discard and normal shutdown, then
   exercise existing save export/upload/settings behavior for regressions.

## Restore into an empty local slot

After **validation passed**, the panel shows **Confirm: Restore (empty slot
only)**. Confirmation identifies the game, title ID, save directory, backup UTC
and current PS4 user ID. Declining confirmation or cancelling the ready panel
discards only the private ZIP. It never deletes a game save. Confirmation does
not authorize an overwrite.

The worker refreshes this user's credentials and re-fetches the selected file's
metadata, requiring the same ID, versions, format, game, title, directory, UTC,
size and checksum. It then repeats the local size/MD5 and full ZIP validation
immediately before creation. The private archive must be a regular file in the
job's generated directory. The embedded `param.sfo` receives bounded structural
validation before Apollo parses it: unique bounded keys and values, terminated
strings, matching `TITLE_ID`/`SAVEDATA_DIRECTORY` and embedded PARAMS title,
required ownership/detail fields, and supported save allocation blocks (96 to
524288, 32 KiB per block). Restore directory names must fit the PS4 32-byte field.
Unsupported SFOs fail without creating a target.

Absence is checked against the current user's save key, PFS volume and savedata
database before staging and again immediately before import. Existing saves,
orphan files, stale DB rows or unknown lookup results are refused. The empty
mount helper repeats that check while creating the key and volume, so a racing
target is refused rather than truncated. The archive is streamed into the
private job directory in Apollo's standard decrypted layout:
`stage/PS4/APOLLO/<save-directory>/...`. Exactly one validated archive root is
stripped. No generic ZIP extractor is used. The existing `ReadUsbList` scanner
must find the staged title and directory before the normal **Copy save game to
HDD** implementation imports it. That copy path updates save details, patches
ownership to the current local account, user and PSID, then unmounts. IDs in the
backup are used only to validate title/save identity; source account and user
IDs are never trusted. Cancellation or any failure retains the original ZIP;
partial HDD targets are retained for manual inspection and never reported as a
successful restore.

The local PS4 user must have a nonzero account ID so the imported SFO and save
database can be assigned to that user. A PlayStation Network link or session is
not required. For an unactivated local user, use Apollo's **User Tools > Activate
PS4 Accounts**, reboot as Apollo requests, then retry restore. This is a user
action; restore never invents or substitutes an account ID. The import refreshes
the ID through `sceUserServiceGetNpAccountId` and stops before mounting or
copying if that SDK call fails or returns zero. The downloaded ZIP and any
replacement recovery journal remain available. The phase log records the raw
native status without exposing the account ID.

Rendering and controller input remain on the main thread. The existing exclusive
worker serializes save, credential and network phases. Google credentials are
read/refreshed before the target save mount; no credential mount or HTTP transfer
runs during staging or HDD import. Token handling and verified TLS remain
unchanged. Cancellation is checked during archive validation and staging, around
the save-list/import operation, and again after unmount through the atomic success
boundary. An accepted cancellation cannot report restore success, even when the
last write already completed.

On success the save is unmounted and the download is removed; a private-cache
cleanup error is reported separately. On failure or in-flight cancellation the
download remains available for inspection/retry or explicit discard. Newly
created partial targets are deliberately retained: this milestone has no
automatic target rollback/deletion, because uncertain creation/database/mount
state cannot establish safe deletion. A retry refuses such an existing target.
Inspect it manually and remove only a confirmed disposable target under the
same user, using normal save management after a clean restart. Unmount failure
blocks further save/credential/network operations until app restart; a failed
GoogleAuth credential unmount has the same restart requirement. Abrupt
termination also requires manual inspection and does not imply success.

## Replace an existing save (best effort)

Triangle on a ready Google download opens a separate replacement confirmation.
The confirmation names the title, save directory and PS4 user and warns that
power loss can leave the save missing or partial. Cross retains the existing
empty-slot operation. Replacement is allowed only for a confirmed present
target and a ZIP whose complete archive, checksum and embedded SFO match that
title and directory.

Before changing the target, the worker mounts it read-only, creates a rollback
ZIP, unmounts it, and validates the rollback ZIP. It copies both the selected
download and rollback ZIP into a private transaction directory at
`/data/ps4-save-sync/replace/tx-*`, validates those copies, and fsyncs them and
a versioned journal. It then uploads the rollback ZIP as a new Drive file.
The upload must return verified file ID, size and MD5; an uncertain upload is
not enough to proceed. No credential access or HTTP request occurs while a
game save is mounted. The transaction directory and both ZIPs are retained
after success and failure; Apollo's ordinary cache cleanup does not remove
them. They contain decrypted save data and require appropriate local access
control and available storage.

Only after the rollback upload and a durable journal checkpoint does the
worker disable cancellation, delete the target with Apollo's save-data delete
operation, confirm absence, and run the same staged empty-slot import used by
the working restore path. Success requires a checked unmount and a durable
completion checkpoint. If delete or import fails, the worker attempts to
delete any partial target and import the verified rollback ZIP into an empty
slot. This can also fail. No automatic recursion over save files or direct
key/volume/database deletion is used.

An interrupted or failed recovery leaves the journal and both ZIPs intact.
On reopening Google Drive, the panel shows **Recovery pending**; R1 explicitly
retries rollback from the retained local ZIP without network access. Recovery
can replace a save that was fully imported just before a power loss but before
the completion checkpoint. Do not assume either result until the disposable
save has been inspected and loaded in the game. If the journal cannot be
validated or a save cannot be unmounted, further Google save operations are
blocked; restart and investigate the preserved transaction. The OpenOrbis
backup-related declarations and link stubs do not establish transactional
firmware behavior, so this path makes no atomicity or guaranteed-recovery claim.

The per-user phase log at `/data/ps4-save-sync/replace/phase-<user-id>.log`
uses `fsync` for each successfully written record. During rollback it records the ZIP validation result,
journal checkpoint, target presence result, delete result, and import result.
The PS4 adapter also records the numeric `sceSaveDataDelete` status and the
post-delete absence check separately. Import failures include the restore
diagnostic when available (operation, errno, and ZIP status); a failed unmount
has its own phase record. `rollback: after failed` is a summary marker and
cannot by itself identify which operation failed. The optional debug log is
`/data/ps4-save-sync/apollo.log`.

During replacement or recovery import, the same phase log records `import account lookup`,
`import input`, `import scan`,
`import target absence`, `import mount`, `import copy`, `import metadata`,
`import ownership`, `import unmount`, and `import adapter` results as the steps
run. Each result includes the call name, its raw `native` return, and an errno
captured immediately after the call; `errno=0` means the failed operation did
not provide a POSIX errno. An `import input` failure names the rejected guard,
including a missing local `account_id`. Mount failures name the inner filesystem,
save creation, database, or private mount call. Copy failures name the directory
or file operation, and unmount records the private SDK status. The account and
empty-slot guards remain required.
The scan result distinguishes a missing staged `param.sfo` from a scanner match
failure. Later steps are absent when an earlier step stops the import, except
that unmount is still attempted after a successful mount.
If the private SDK mount or unmount call reports failure, its return and POSIX
`errno` are captured immediately. The mount directory is retained and Replace
stops before rollback because the firmware mount state cannot be confirmed.
There is no automatic unmount retry.

### Disposable-save replacement test on PS4

1. Close the game. Use a disposable save with known old progress on the target
   console and newer progress in a Drive ZIP from the same title ID and exact
   save directory. Make and verify an independent USB copy of the old save first.
2. Download the newer ZIP. Confirm Cross still refuses the occupied slot. Press
   Triangle, check the title, directory and user in the replacement warning, and
   decline once; verify the old progress still loads. Repeat and accept.
3. With development logging or a breakpoint at the delete checkpoint, verify
   that a new rollback file is already in the marked Drive folder with the
   expected size and MD5, and that the local `replace/tx-*` directory contains
   `source.zip`, `rollback.zip` and `journal`. If backup creation, validation,
   upload or confirmation fails, verify the old save still loads and no
   delete/import occurred.
4. On success, check the save is unmounted, launch the game, verify newer
   progress, and reboot the console to check it again. Verify both ZIPs and the
   completed journal remain locally, and the rollback file remains on Drive.
5. With another disposable save, inject a controlled delete or import failure.
   Verify the app either restores and unmounts the old progress or shows
   **Recovery pending** with both ZIPs and journal retained. If pending, restart
   the app, press R1 to retry local rollback, then verify the old progress in
   game. Repeat with a failed rollback and retry after fixing the fault.
6. For an interruption test, use only the independently backed-up disposable
   save. Interrupt during delete/import, reboot, then check for **Recovery
   pending** and use R1. Recovery can fail or restore old progress after the
   new data had fully copied; inspect the save and the retained files before
   any further operation. Never use this test on the only copy of valued data.

## Safe disposable-save restore test on PS4 (pending)

1. Build/install with OpenOrbis and the existing dependencies. Keep the game
   closed throughout download/restore. Use one current PS4 user and a genuinely
   disposable save; record its title ID and exact directory. Make an independent
   USB backup and verify that backup before deleting anything. Do not use your
   only copy of an important save.
2. Upload the disposable save, then browse and download that exact backup.
   Verify the confirmation's game, title ID, directory, timestamp and user ID.
   Restore while the original slot still exists: expect **Save already exists**,
   no overwrite and no success. Confirm the original still loads unchanged.
3. Close the game and app. Through normal PS4 save management, delete only the
   independently backed-up disposable save for that same user. Restart Save Sync
   and confirm that exact slot is absent. Do not edit/remove database rows or
   other users' files manually to force an empty-slot result.
4. Download the same backup again, choose **Restore**, inspect all confirmation
   fields and accept. Expect **Restore complete for this PS4 user. Save unmounted
   successfully.** Verify the title/save entry and its details, mount read-only
   with development tooling if available, and check the restored SFO account,
   PARAMS user ID and PSID HMAC belong to the current user. Check copied file
   contents and private ZIP cleanup, then close Save Sync and launch the game.
   Confirm the disposable progress loads; restart the console and repeat loading.
5. Download it again and attempt another restore. Expect refusal with no change
   to the restored save. Separately decline confirmation/cancel the ready panel:
   expect ZIP cleanup and no target changes. Repeat under another local user to
   check that credentials/confirmation/target checks remain scoped to that user.
6. With another empty disposable slot, cancel during hash/ZIP validation and copy,
   and exercise storage, ownership-patch and unmount failures using controlled
   development tooling. Expect no successful result on failure/cancellation.
   Pre-creation cancellation leaves no target; post-creation cancellation/failure
   may retain a partial slot. Restart after unmount failure, inspect the exact
   current-user disposable slot, and delete it only after confirming it is the
   newly created test target. Never delete an existing save to clear an error.
7. Verify PS4 support for exclusive kernel opens, descriptor-relative no-follow
   extraction, PFS file sync/close, SFO compatibility, image allocation, DB details,
   ownership patching and unmount. Check UI responsiveness, rejection of overlapping
   operations and continued token isolation/TLS verification. Repeat export/upload
   regressions. Repeat the empty disposable-slot procedure on each target console
   before relying on cross-console use.

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
