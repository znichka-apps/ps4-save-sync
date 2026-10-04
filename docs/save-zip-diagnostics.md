# ZIP preparation diagnosis

Both Google staging and regular save ZIP export share `source/save_zip.c`.
Compared with `bf8a675:source/zip_util.c`, the checked exporter added `lstat`
classification and checks for callback registration, traversal, attributes, and
archive completion. The older exporter used `dirent.d_type`, ignored traversal
and `zip_close` failures, and considered an existing output file a success.
Those unchecked success semantics have not been restored.

## Supported cause and compatibility

The SDK release selected by `.github/workflows/build.yml` is
`illusion0001/OpenOrbis-PS4-Toolchain`, release `0.0.1.416`.
The downloaded `toolchain.tar.gz` has SHA-256
`c9c7e94c0f077ef2de2ac801577044179c2ce57a521c2544fec2b1ea3146facf`.
Disassembly of its actual `lib/libc.a` shows:

- `lstat` tail-calls `fstatat` with `AT_SYMLINK_NOFOLLOW`.
- `fstatat` stores `0x4e` (78, SDK `ENOSYS`) through `___errno_location`
  and returns `-1` unconditionally.

The [OpenOrbis libc source](https://github.com/OpenOrbis/musl/blob/master/src/stat/fstatat.c)
also implements that PS4 stub. This is a concrete SDK incompatibility introduced
by the checked exporter's unconditional `lstat`, consistent with both console
failures. No console execution was available during this local diagnosis.

On PS4, typed `readdir` entries now classify regular files and directories
without calling the stub. Symlinks and other types are rejected. `DT_UNKNOWN`
still goes through checked `lstat` and reports failure; there is no fallback to
`stat` that could follow a link outside the save. Host builds retain `lstat`.

The repository's actual `oosdk_libraries/libzip-1.9.2/lib/zip.h` and
`zip_progress.c` were inspected. Callback registration returns `int` (0 or -1)
and uses the existing callback signature. The numeric error accessors and
`ZIP_ER_CANCELLED` are available. No libzip API workaround was needed.

## Diagnostics and safety

Caller-owned diagnostic buffers report only fixed operation names and numeric
`errno`, libzip archive/system errors, or hash errors. Errors are captured before
free/discard/close/unlink/unmount cleanup. The first ZIP/hash failure survives
cleanup; an unmount failure includes its own diagnostic and any prior ZIP error.
Mount wrappers preserve `errno` across logging, notifications, and `rmdir`.

Google's worker copies the diagnostic to its status snapshot under the existing
SDL mutex. The panel wraps the complete diagnostic separately from summary and
cancellation messages. Regular save ZIP export displays its caller-owned
diagnostic after stopping the loading screen. No diagnostic includes paths,
credentials, session URLs, or file contents.

Cancellation during traversal and `zip_close`, checked archive completion,
partial-output cleanup, source-save protection, and unmount-before-hash/auth/
networking remain enforced. Unknown file types fail closed.

## Local verification

- `sh tools/test_google_host.sh`: all existing Google host tests plus the new
  ZIP fault-injection tests passed with AddressSanitizer/UndefinedBehaviorSanitizer.
  Both host and `__PS4__` ZIP branches were exercised. Failures cover every ZIP
  operation, error preservation, symlinks, and cancellation during archive close.
  Staging tests cover validation, cache creation, mkdtemp, mount, unmount,
  archive reading, UTC failure, layout/hash, and unchanged source data.
- Clang 21 compiled all seven changed C sources to FreeBSD x86-64 objects with
  `--target=x86_64-pc-freebsd12-elf`, the exact SDK headers, CI's UserService
  header updates, and libzip 1.9.2's public header. Portable dependency headers
  were supplied locally; host libzip is 1.11.4. This is compilation validation,
  not a complete PS4 package/link or console runtime test.
- `git diff --check` passed.

Downloads, disassembly, temporary headers, and object files are confined to the
ignored `build/compat` directory. Changes remain uncommitted.
