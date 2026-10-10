"""Complete a GP4 rootdir tree without changing metadata or file entries."""
import argparse
from pathlib import Path
import sys
from xml.dom import Node, minidom
from xml.parsers.expat import ExpatError


def children(node, name):
    return [child for child in node.childNodes
            if child.nodeType == Node.ELEMENT_NODE and child.tagName == name]


def ensure_directories(path):
    path = Path(path)
    document = minidom.parse(str(path))
    try:
        project = document.documentElement
        if project.tagName != 'psproject':
            raise ValueError('Expected a GP4 psproject element.')
        files = children(project, 'files')
        roots = children(project, 'rootdir')
        if len(files) != 1 or len(roots) > 1:
            raise ValueError('Expected one files element and at most one rootdir.')
        # Validate all paths before modifying the document. LibOrbisPkg splits
        # targ_path on forward slashes; do not normalize or rewrite file entries.
        parents = set()
        for file in children(files[0], 'file'):
            target = file.getAttribute('targ_path')
            parts = target.split('/')
            if not target or '\\' in target or any(part in ('', '.', '..') for part in parts):
                raise ValueError('Expected relative, slash-separated file targ_path values.')
            parents.add(tuple(parts[:-1]))
        root = roots[0] if roots else document.createElement('rootdir')
        if not roots:
            project.appendChild(root)
        added = 0
        for parts in sorted(parents):
            parent = root
            for name in parts:
                matches = [child for child in children(parent, 'dir')
                           if child.getAttribute('targ_name') == name]
                if matches:
                    parent = matches[0]
                else:
                    child = document.createElement('dir')
                    child.setAttribute('targ_name', name)
                    parent.appendChild(child)
                    parent = child
                    added += 1
        if added or not roots:
            # minidom keeps comments, namespace declarations, and unknown fields.
            # Publish only after serialization succeeds; a no-op keeps exact bytes.
            temporary = path.with_name(path.name + '.tmp')
            try:
                temporary.write_bytes(document.toxml(encoding='utf-8'))
                temporary.replace(path)
            finally:
                temporary.unlink(missing_ok=True)
        return added
    finally:
        document.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('gp4', type=Path)
    args = parser.parse_args()
    try:
        added = ensure_directories(args.gp4)
    except (OSError, ValueError, ExpatError) as error:
        print(f'GP4 directory repair failed: {error}', file=sys.stderr)
        return 1
    print(f'GP4 directory tree complete: added {added} directory declarations.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
