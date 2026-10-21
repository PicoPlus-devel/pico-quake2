#!/usr/bin/env python3
"""Lists what the flash asset cache holds, from an emulated region kept by the harness.

    Q2_FLASH_FILE=/tmp/fc.bin hosttest/q2_host ...
    python3 -I hosttest/fcdump.py /tmp/fc.bin            # table per category
    python3 -I hosttest/fcdump.py --csv /tmp/fc.bin      # one CSV line (sweep.sh)
    python3 -I hosttest/fcdump.py --list /tmp/fc.bin     # every entry

Reads the newer valid directory slot (layout in qcommon/fcache.c). Sizes are the
entries' lengths; "pages" rounds each entry up to the 256-byte allocation unit,
which is the room it takes. "glcmds" is the part of the cached MD2 models that the
software renderer does not use in place (the GL command lists).
"""
import struct
import sys
import zlib

FC_MAGIC = (ord('C') << 24) | (ord('F') << 16) | (ord('2') << 8) | ord('Q')
FC_VERSION = 1
SLOT_BYTES = 128 * 1024
PAGE = 256
HEADER = struct.Struct('<16I')
ENTRY = struct.Struct('<44sIIIHHI')
FC_RAW, FC_PIC8, FC_SOUND = 1, 2, 3

CATEGORIES = ('textures', 'models', 'skins', 'sprites', 'pics', 'env', 'sounds', 'other')


def read_slot(region, slot):
    base = slot * SLOT_BYTES
    h = HEADER.unpack_from(region, base)
    magic, version, generation, num = h[0], h[1], h[2], h[3]
    if magic != FC_MAGIC or version != FC_VERSION or num > (SLOT_BYTES - HEADER.size) // ENTRY.size:
        return None
    table = region[base + HEADER.size:base + HEADER.size + num * ENTRY.size]
    if zlib.crc32(table) != h[6]:
        return None
    return generation, h[5], [ENTRY.unpack_from(table, i * ENTRY.size) for i in range(num)]


def category(name, kind):
    if kind == FC_SOUND:
        return 'sounds'
    top = name.split('/', 1)[0]
    if kind == FC_RAW:
        if name.endswith('.wal'):
            return 'textures'
        if name.endswith('.md2'):
            return 'models'
        if name.endswith('.sp2'):
            return 'sprites'
    if kind == FC_PIC8:
        if top == 'models' or top == 'players':
            return 'skins'
        if top in ('sprites', 'pics', 'env'):
            return top
    return 'other'


def glcmd_bytes(region, offset, length):
    """GL command bytes of a cached MD2, and whether they sit at the end of the file."""
    if length < 68:
        return 0, False
    v = struct.unpack_from('<17i', region, offset)
    num_glcmds, ofs_glcmds, ofs_end = v[9], v[15], v[16]
    return num_glcmds * 4, ofs_glcmds + num_glcmds * 4 == ofs_end


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    if len(args) != 1:
        sys.exit(__doc__)
    with open(args[0], 'rb') as f:
        region = f.read()
    slots = [s for s in (read_slot(region, 0), read_slot(region, 1)) if s]
    if not slots:
        sys.exit('fcdump: no valid directory slot')
    _, seq, entries = max(slots, key=lambda s: s[0])
    live = [e for e in entries if e[1]]

    size = dict.fromkeys(CATEGORIES, 0)
    pages = dict.fromkeys(CATEGORIES, 0)
    count = dict.fromkeys(CATEGORIES, 0)
    glcmds = 0
    glcmds_at_end = True
    level_pages = 0
    rows = []
    for name, offset, length, _crc, kind, _flags, last_seq in live:
        name = name.split(b'\0', 1)[0].decode()
        c = category(name, kind)
        size[c] += length
        pages[c] += (length + PAGE - 1) // PAGE * PAGE
        count[c] += 1
        if last_seq == seq:
            level_pages += (length + PAGE - 1) // PAGE * PAGE
        if c == 'models':
            g, at_end = glcmd_bytes(region, offset, length)
            glcmds += g
            glcmds_at_end &= at_end
        rows.append((c, name, length, last_seq))

    total_pages = sum(pages.values())
    if '--csv' in sys.argv:
        print(','.join([str(len(live)), str(total_pages // 1024)]
                       + [str(pages[c] // 1024) for c in CATEGORIES] + [str(glcmds // 1024)]))
        return
    if '--list' in sys.argv:
        for c, name, length, last_seq in sorted(rows):
            print(f'{c:9s} {length:9d} seq {last_seq:4d}  {name}')
        return
    print(f'{len(live)} entries, {total_pages // 1024} KB in pages '
          f'({sum(size.values()) // 1024} KB of data); current level (seq {seq}) {level_pages // 1024} KB')
    print(f'{"category":9s} {"entries":>7s} {"KB":>7s} {"share":>6s}')
    for c in CATEGORIES:
        if count[c]:
            print(f'{c:9s} {count[c]:7d} {pages[c] // 1024:7d} {100 * pages[c] / total_pages:5.1f}%')
    print(f'MD2 GL commands: {glcmds // 1024} KB'
          + ('' if glcmds_at_end else ' (not all at the end of their file)'))


if __name__ == '__main__':
    main()
