#!/usr/bin/env python3
"""Check the restore assumptions against CI's actual SDK objects and libzip source.

Usage: python3 tools/audit_restore_sdk.py SDK_DIR LIBZIP_1_9_2_DIR
Requires GNU nm/objdump; does not use the host's libc to establish support.
"""
import pathlib
import re
import subprocess
import sys

sdk, libzip = map(pathlib.Path, sys.argv[1:])


def output(*args):
    return subprocess.check_output(args, text=True, stderr=subprocess.DEVNULL)


disassembly = output("objdump", "-dr", str(sdk / "lib/libc.a"))


def body(name):
    match = re.search(r"^[0-9a-f]+ <" + re.escape(name) + r">:\n(.*?)(?=\n\n|\Z)",
                      disassembly, re.M | re.S)
    assert match, f"missing implementation: {name}"
    return match[1]


assert "fstatat" in body("lstat")
for name in ("fstatat", "mkdirat"):
    # FreeBSD ENOSYS = 78: compiled function stores 0x4e in errno then returns -1.
    code = body(name)
    assert "___errno_location" in code and "$0x4e" in code and "$0xffffffff" in code, name
    print(f"CI libc {name}: unconditional ENOSYS (78)")
for name, target in (("open", "_open"), ("openat", "_openat"), ("fstat", "_fstat"),
                     ("__errno_location", "__error"),
                     ("__stdio_read", "_readv"), ("__stdio_seek", "__lseek"),
                     ("__lseek", "lseek")):
    assert target in body(name), (name, target)
    print(f"CI libc {name}: delegates to {target}")

kernel = output("nm", "-D", str(sdk / "lib/libkernel.so"))
kernel_sys = output("nm", "-D", str(sdk / "lib/libkernel_sys.so"))
for symbol in ("_open", "_openat", "_fstat", "ftruncate", "fsync", "dup", "close",
               "read", "write", "lseek", "geteuid", "_readv", "_read", "_close",
               "sceKernelGetModuleList", "sceKernelGetModuleInfo", "sceKernelDlsym",
               "sceKernelLoadStartModule", "sceKernelOpen", "sceKernelWrite",
               "sceKernelFsync", "sceKernelClose", "mkdir", "access", "unlink", "rmdir"):
    assert re.search(r"\bT " + symbol + r"$", kernel, re.M), symbol
for symbol in ("lstat", "mkdirat"):
    assert re.search(r"\bT " + symbol + r"$", kernel_sys, re.M), symbol
flags = (sdk / "include/bits/fcntl.h").read_text()
for name, value in (("O_NOFOLLOW", "0400"), ("O_DIRECTORY", "0400000"), ("O_EXCL", "04000")):
    assert re.search(r"#define\s+" + name + r"\s+" + value + r"\b", flags), name

fdopen = (libzip / "lib/zip_fdopen.c").read_text()
for call in ("dup(fd_orig)", 'fdopen(fd, "rb")', "zip_source_filep_create", "zip_open_from_source", "close(fd_orig)"):
    assert call in fdopen, call
stdio = (libzip / "lib/zip_source_file_stdio.c").read_text()
for call in ("fstat(fileno", "fseeko(", "fread(", "fclose("):
    assert call in stdio, call
print("CI kernel exports, BSD no-follow/exclusive flags, and libzip descriptor dependencies verified.")
print("Import libraries describe firmware exports; on-console behavior still requires a console smoke test.")
