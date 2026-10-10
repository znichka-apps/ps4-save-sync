"""Fail without printing secret values if staged files contain local credentials."""
import fnmatch
import json
from pathlib import Path
import re
import subprocess
import sys

def git(*args):
    return subprocess.check_output(['git', *args])

def main():
    root = Path(__file__).resolve().parents[1]
    values = []
    config = root / 'include/google_build_config.h'
    if config.exists():
        for value in re.findall(r'^#define GDRIVE_CLIENT_(?:ID|SECRET) (.+)$', config.read_text(), re.M):
            values.append(json.loads(value).encode())
    paths = git('diff', '--cached', '--name-only', '-z').decode().split('\0')
    forbidden = ('client_secret*.json', 'credentials*.json', 'google_credentials*.json',
                 '*oauth*credentials*.json', '*token*.json', 'access_token*', 'refresh_token*',
                 'google_build_config.h', 'google_build_config.tmp', '.env', '.env.*',
                 '*.pkg', '*.elf', '*.bin', '*.o', '*.exe', '*.pyc')
    count = 0
    for path in filter(None, paths):
        if any(fnmatch.fnmatch(Path(path).name.lower(), pattern) for pattern in forbidden):
            print('Credential/build artifact staged; audit failed (path and values suppressed).', file=sys.stderr)
            return 1
        data = git('show', ':' + path)
        if any(value and value in data for value in values):
            print('Local OAuth registration found in staged data; audit failed (values suppressed).', file=sys.stderr)
            return 1
        count += 1
    print(f'Staging audit passed: {count} files; no credential files, build artifacts, or local OAuth registration values.')
    return 0

if __name__ == '__main__':
    try:
        sys.exit(main())
    except Exception:
        print('Staging audit could not complete; sensitive details suppressed.', file=sys.stderr)
        sys.exit(1)
