# Restore SDK compatibility

The package workflow downloads illusion0001/OpenOrbis-PS4-Toolchain release
`0.0.1.416` and builds `bucanero/oosdk_libraries/libzip-1.9.2`.
Build `3591a52` called libc `lstat` on the private archive directory. Disassembly
of that release's actual `libc.a` shows `lstat -> fstatat`, whose entire
implementation sets FreeBSD `ENOSYS` (78) and returns -1. `mkdirat` is also an
unconditional ENOSYS stub, and the existing-save lookup used the same broken
`lstat`. Host success did not exercise these implementations.

## Audited implementations

| API | Evidence in the CI SDK / CI libzip | Restore treatment |
| --- | --- | --- |
| `lstat`, `fstatat` | `libc.a`: lstat calls fstatat; fstatat stores errno=78, returns -1 | Resolve `lstat` directly from `libkernel_sys`; no stat fallback |
| `mkdirat` | `libc.a`: stores errno=78, returns -1; `libkernel_sys.so` exports mkdirat | Resolve native mkdirat; retain descriptor-relative extraction |
| `open`, `openat` | Actual libc wrappers call `_open`, `_openat`; `libkernel.so` exports both | Keep directory descriptors and no-follow flags, never use pathname concatenation for extraction |
| `O_NOFOLLOW`, `O_DIRECTORY`, `O_EXCL` | CI `bits/fcntl.h`: BSD values 0400, 0400000, 04000; open wrappers forward flags | Reject directory/file symlinks; keep exclusive key/volume creation in `sd.c` |
| `fstat` | Actual libc wrapper jumps to kernel `_fstat` | Check opened directory ownership/mode, archive type/owner/link count, identity and destination type/link count |
| `ftruncate` | libc archive delegates to kernel import; kernel exports ftruncate | Truncate only after destination descriptor passes regular-file and single-link checks; never use O_TRUNC before validation |
| `dup`, `read`, `write`, `lseek`, `close`, `fsync`, `geteuid` | Kernel import library exports; libc stdio uses `_read`/`_readv`, `__lseek -> lseek`, `_close` | Retain descriptors, bounded reads/writes, sync and checked closes |
| `fdopen`, `fread`, `fseeko`, `ftello`, `fclose` | Actual libc fdopen binds stdio callbacks; stdio read/seek callbacks delegate to kernel | Use descriptor streams for raw ZIP validation |
| `zip_fdopen` | CI libzip source: dup, fdopen(rb), zip_source_filep_create, zip_open_from_source; success closes supplied fd | Pass a duplicate, retain original archive fd; close supplied duplicate only on failure |
| libzip file-source metadata | CI zip_source_file_stdio.c: fstat(fileno(stream)) for descriptor streams, fread/fseeko/fclose | ZIP validation and SFO/extraction read the opened archive, not a reopened pathname |
| errno | Actual libc `__errno_location -> __error`, exported by kernel | Native POSIX exports and libc share errno; retain numeric errno and native resolution codes |
| module lookup | CI headers/imports: GetModuleList, GetModuleInfo, Dlsym, LoadStartModule | Reuse libkernel_sys loaded by loadPrivLibs; load only if absent; fail closed on errors/missing symbols |
| download/cache cleanup | mkdir, access, unlink, rmdir kernel exports; mkdtemp uses mkdir; download stdio follows the audited open/read/write/seek path | Existing exclusively created 0700 job directory and fixed archive basename remain required |
| save creation / metadata | sd.c uses sceKernelOpen/Write/Fsync/Close kernel exports; SQLite and Apollo retain their existing adapters | Existing key, orphan volume or DB row refuses restore; key and volume use O_EXCL; partial targets are retained |

The `.so` files are **firmware import libraries**, not implementations of
firmware syscalls. Their exports establish the native ABI available to this
SDK; libc disassembly establishes which calls are actual stubs. Host simulation
does not prove console firmware behavior. If a native export is unavailable or
returns ENOSYS, restore fails closed with numeric diagnostics. A console smoke
test remains necessary to verify firmware behavior.

Run the reproducible audit against those exact inputs:

```sh
python3 tools/audit_restore_sdk.py "$OO_PS4_TOOLCHAIN" oosdk_libraries/libzip-1.9.2
```

## Security and diagnostics

Build f871543's `op=3 errno=22` does not distinguish a syscall failure from
an archive policy rejection. Op 3 now reports fixed `call=openat`, `call=fstat`,
`call=type`, `call=owner`, or `call=link` names. Syscall errno is captured
immediately; policy failures explicitly report EINVAL. No paths or archive
contents are included. Focused host injections assert each exact diagnostic,
archive retention and zero target creation/writes.

The actual release libc openat disassembly tests O_CREAT bit 9, passes mode zero
when that bit is absent, and forwards edx (flags) unchanged to `_openat`.
The SDK defines O_NONBLOCK=04, O_NOFOLLOW=0400 and O_CREAT=01000 (octal).
These are consistent with the wrapper; the import library is not firmware
implementation evidence that O_NONBLOCK causes EINVAL. No flag removal,
pathname fallback or policy relaxation is justified by the reported combined
diagnostic. Keep the existing call until the split console result identifies
the failing branch.

The private job directory is opened with O_DIRECTORY/O_NOFOLLOW and checked
with fstat for owner and no group/other permissions. The fixed `backup.zip` is
opened relative to that directory, with O_NOFOLLOW/O_NONBLOCK. It must be a
regular, single-link file owned by the directory owner. Hashing, raw ZIP checks,
CRC/name/type checks, SFO checks and extraction all use the opened descriptor.
After validation, descriptor metadata and native lstat metadata must agree on
device, inode, type, owner, size and nanosecond modification/change times. The
private directory pathname must still identify the opened directory.

Extraction remains relative to the mount descriptor, checks each directory with
O_DIRECTORY/O_NOFOLLOW, rejects symlink/FIFO/hardlink destinations, and truncates
only a verified regular single-link file. Existing-save refusal and exclusive
key/volume creation are preserved.

Diagnostics include operation numbers, errno, native resolution/hash codes and
ZIP open/validation codes. Top-level operations: 1 initialization/metadata,
2 private directory, 3 archive open/fstat, 4 hash/ZIP revalidation, 5 identity,
6 ZIP open for extraction, 7 SFO, 8 absence, 9 create/mount, 10 copy,
11 ownership, 12 details, 13 unmount, 14 archive close, 15 directory close.
Extraction operations: 100 mount open, 101 dup, 102 mkdirat, 103 directory
openat, 104 file openat, 105 fstat/type/link check, 106 ftruncate, 107 write,
108 fsync, 109 entry close, 110 mount close. Cancelled work reports cancellation
and whether a target was created.

## Tests

`tools/test_google_host.sh` runs restore in normal and `__PS4__` modes. The latter
links ENOSYS lstat/fstatat/mkdirat stubs and mocks kernel module lookup to return
native implementations. It asserts successful restore never calls those libc
stubs. Resolution/list/info failures, NULL exports and native ENOSYS fail closed.
Both modes test existing saves, invalid archives/SFO, descriptor no-follow flags,
private permissions, archive symlinks/hardlinks, archive inode replacement,
private directory replacement with a symlink, destination symlinks/FIFOs/hardlinks,
and numeric diagnostics for fstat/ftruncate/openat/write/fsync/zip_fdopen failures.
The existing exclusive-creation tests cover racing key/volume creation and
negative kernel descriptors.

Run the suite on a POSIX filesystem: WSL's default Windows-mounted DrvFS does not
enforce mkdtemp's 0700 permissions and correctly fails the private-mode check.
Compilation checks used the downloaded CI SDK with the PS4 target and
`-Wall -Werror` for google_restore, google_restore_ps4, google_download,
google_backup, restore_fs, save_target and saves.
