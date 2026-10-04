"""Generate ignored OAuth build registration; never print registration values."""
import argparse
import json
import os
from pathlib import Path
import sys

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--credentials', type=Path)
    args = parser.parse_args()
    try:
        if args.credentials:
            if args.credentials.resolve().is_relative_to(Path(__file__).resolve().parents[1]):
                raise ValueError()
            data = json.loads(args.credentials.read_text(encoding='utf-8-sig'))
            data = data.get('installed', data.get('web', data))
            values = [data.get('client_id'), data.get('client_secret')]
        else:
            values = [os.environ.get('GDRIVE_CLIENT_ID'), os.environ.get('GDRIVE_CLIENT_SECRET')]
        if any(not isinstance(v, str) or not v or len(v) > 2048 or
               any(ord(c) < 33 or ord(c) > 126 for c in v) for v in values):
            raise ValueError()
        target = Path(__file__).resolve().parents[1] / 'include/google_build_config.h'
        text = '/* Generated locally. Do not commit. */\n' + ''.join(
            '#define GDRIVE_CLIENT_' + name + ' ' + json.dumps(value) + '\n'
            for name, value in zip(('ID', 'SECRET'), values))
        temp = target.with_suffix('.tmp')
        temp.write_text(text, encoding='ascii')
        temp.replace(target)
    except Exception:
        print('Google configuration missing or invalid: provide external OAuth JSON or GDRIVE_CLIENT_ID and GDRIVE_CLIENT_SECRET.', file=sys.stderr)
        return 1
    print('Google build configuration generated (values suppressed).')
    return 0

if __name__ == '__main__':
    sys.exit(main())
