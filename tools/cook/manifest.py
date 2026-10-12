#!/usr/bin/env python3
"""manifest.json for a cooked folder: path, size and sha1 of every file.

The browser import checks it instead of re-reading 1 GB to decide whether
an OPFS copy is complete. Paths are relative and use forward slashes.
"""
import hashlib
import json
import os
import sys

NAME = 'manifest.json'
VERSION = 1


def _sha1(path):
    h = hashlib.sha1()
    with open(path, 'rb') as f:
        for chunk in iter(lambda: f.read(1 << 20), b''):
            h.update(chunk)
    return h.hexdigest()


def _files(root):
    for dp, _, names in os.walk(root):
        for n in names:
            p = os.path.join(dp, n)
            rel = os.path.relpath(p, root).replace(os.sep, '/')
            if rel != NAME and not rel.endswith('.tmp'):
                yield rel, p


def write(root):
    entries = [{'path': rel, 'size': os.path.getsize(p), 'sha1': _sha1(p)}
               for rel, p in sorted(_files(root))]
    with open(os.path.join(root, NAME), 'w') as f:
        json.dump({'version': VERSION, 'files': entries}, f, indent=0)
    return len(entries)


def check(root):
    """Returns a list of problems; empty means the folder matches its manifest."""
    path = os.path.join(root, NAME)
    if not os.path.isfile(path):
        return ['%s missing: cook again, or run cook.py --write-manifest' % NAME]
    with open(path) as f:
        manifest = json.load(f)
    problems = []
    listed = set()
    for e in manifest['files']:
        listed.add(e['path'])
        p = os.path.join(root, *e['path'].split('/'))
        if not os.path.isfile(p):
            problems.append('missing: ' + e['path'])
        elif os.path.getsize(p) != e['size']:
            problems.append('size differs: ' + e['path'])
        elif _sha1(p) != e['sha1']:
            problems.append('content differs: ' + e['path'])
    problems += ['not in manifest: ' + rel for rel, _ in _files(root) if rel not in listed]
    return problems


if __name__ == '__main__':
    problems = check(sys.argv[1])
    for pr in problems[:50]:
        print('PROBLEM', pr)
    print('%d problems' % len(problems))
    sys.exit(1 if problems else 0)
