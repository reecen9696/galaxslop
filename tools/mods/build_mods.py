#!/usr/bin/env python3
"""Cooks the port's own resources (assets/mods/) into the layout the game reads.

  build_mods.py <assets/mods> <output dir>

Sounds (.wav, port/sfx.h) are copied as they are.  Every archive under the source folder (big-endian RARC, optionally Yaz0,
as exporters write them) is written to the same relative path under the
output folder: file names lower-cased and the root folder named after the
archive, as on the disc (ObjectData/Kinopio.arc holds kinopio/kinopio.bdl,
kinopio/wait.bck...), then cooked to little-endian like the disc's own
archives (tools/cook/cook.py).  The DVD layer serves that folder next to the
game's files (platform/src/dvd/dvd.cpp, port_dvd_overlay_root).

These are original resources made for the port, not game data; see
assets/mods/README.md.
"""
import collections
import os
import shutil
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'cook'))
sys.path.insert(0, os.path.join(HERE, '..'))

import cook  # noqa: E402
import rarc_swap  # noqa: E402
from rarc import yaz0_decompress  # noqa: E402


def name_hash(name):
    """JKRArchive's file name hash."""
    h = 0
    for c in name.encode('shift_jis'):
        h = (h * 3 + c) & 0xFFFF
    return h


def pack_rarc(root, files):
    """A one-folder big-endian RARC holding `files` ([(name, bytes)])."""
    strings = bytearray()
    offs = {}
    for n in ['.', '..', root] + [n for n, _ in files]:
        if n not in offs:
            offs[n] = len(strings)
            strings += n.encode('shift_jis') + b'\0'
    while len(strings) % 32:
        strings += b'\0'

    entries = bytearray()
    payload = bytearray()
    for i, (n, blob) in enumerate(files):
        # 0x11: a file, loaded to main RAM (as the disc's archives).
        entries += struct.pack('>HHIIII', i, name_hash(n), 0x11000000 | offs[n], len(payload), len(blob), 0)
        payload += blob
        while len(payload) % 32:
            payload += b'\0'
    for n, target in (('.', 0), ('..', 0xFFFFFFFF)):
        entries += struct.pack('>HHIIII', 0xFFFF, name_hash(n), 0x02000000 | offs[n], target, 0x10, 0)
    while len(entries) % 32:
        entries += b'\0'

    node = struct.pack('>4sIHHI', b'ROOT', offs[root], name_hash(root), len(files) + 2, 0)
    node += b'\0' * (32 - len(node))
    info_size = 0x20
    node_off = info_size
    entry_off = node_off + len(node)
    str_off = entry_off + len(entries)
    data_off = str_off + len(strings)  # from the info block
    info = struct.pack('>IIIIIIHBBI', 1, node_off, len(files) + 2, entry_off, len(strings), str_off, len(files), 1, 0, 0)
    header = struct.pack('>4sIIIIIII', b'RARC', 0x20 + data_off + len(payload), 0x20, data_off, len(payload), len(payload), 0, 0)
    return header + info + node + entries + strings + payload


def build_archive(src):
    data = bytearray(yaz0_decompress(open(src, 'rb').read()))
    if data[:4] != b'RARC':
        raise SystemExit('%s: not a RARC archive' % src)
    files = []
    for _, path, flags, off, size in rarc_swap.entries(data):
        if flags & rarc_swap.FLAG_COMPRESSED:
            raise SystemExit('%s: %s is compressed inside the archive; export it uncompressed' % (src, path))
        files.append((path.rsplit('/', 1)[-1].lower(), bytes(data[off:off + size])))
    files.sort()
    root = os.path.splitext(os.path.basename(src))[0].lower()
    stats = collections.Counter()
    cooked = cook.cook_rarc(bytearray(pack_rarc(root, files)), stats)
    unknown = [k for k in stats if k.startswith('UNKNOWN')]
    if unknown:
        raise SystemExit('%s: files the cooker does not know: %s' % (src, ', '.join(unknown)))
    return cooked, stats


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    src_root, out_root = sys.argv[1], sys.argv[2]
    count = 0
    for dirpath, _, names in os.walk(src_root):
        for n in sorted(names):
            src = os.path.join(dirpath, n)
            rel = os.path.relpath(src, src_root)
            dst = os.path.join(out_root, rel)
            if n.lower().endswith('.wav'):
                # Sounds (port/sfx.h) are read as they are.
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copyfile(src, dst)
                count += 1
                print('mods: %s (copied)' % rel)
                continue
            if not n.lower().endswith('.arc'):
                continue
            cooked, stats = build_archive(src)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(dst + '.tmp', 'wb') as fh:
                fh.write(cooked)
            os.replace(dst + '.tmp', dst)
            count += 1
            print('mods: %s (%s)' % (rel, ', '.join('%d %s' % (v, k) for k, v in sorted(stats.items()))))
    print('mods: %d file(s) -> %s' % (count, out_root))


if __name__ == '__main__':
    main()
