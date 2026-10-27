#!/usr/bin/env python3
"""Emulates the way the Windows shell finds an executable's icon, and checks
that the resource tree is self consistent.

Usage: check_icon.py <exe> [<expected ico file>]
"""
import struct
import sys

import pefile


def leaf_key(pe, out, path, e, depth=0):
    for a in e.directory.entries:
        for b in a.directory.entries:
            d = b.data.struct
            off = pe.get_offset_from_rva(d.OffsetToData)
            out.append((path + (a.name if a.name is not None else a.id), d.Size, off))


def main():
    path = sys.argv[1]
    expected = sys.argv[2] if len(sys.argv) > 2 else None
    pe = pefile.PE(path, fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_RESOURCE"]])
    if not hasattr(pe, "DIRECTORY_ENTRY_RESOURCE"):
        print("NO RESOURCE DIRECTORY")
        return 1

    groups = {}
    icons = {}
    for e in pe.DIRECTORY_ENTRY_RESOURCE.entries:
        if e.id == 14:
            for a in e.directory.entries:
                for b in a.directory.entries:
                    d = b.data.struct
                    off = pe.get_offset_from_rva(d.OffsetToData)
                    key = str(a.name) if a.name is not None else ("id:%d" % a.id)
                    groups[key] = pe.__data__[off:off + d.Size]
        if e.id == 3:
            for a in e.directory.entries:
                for b in a.directory.entries:
                    d = b.data.struct
                    off = pe.get_offset_from_rva(d.OffsetToData)
                    icons[int(a.id)] = pe.__data__[off:off + d.Size]

    print("groups : %s" % ", ".join(sorted(groups)))
    print("icons  : %d" % len(icons))

    problems = []
    # The shell tries the numeric id 1 first, then the name MAINICON, then
    # whatever group comes first.
    chosen = None
    for key in ("id:1", "MAINICON"):
        if key in groups:
            chosen = key
            break
    if chosen is None and groups:
        chosen = sorted(groups)[0]
    if chosen is None:
        print("NO GROUP ICON -> the shell has nothing to show")
        return 1
    print("shell picks: %s" % chosen)

    g = groups[chosen]
    reserved, typ, count = struct.unpack_from("<HHH", g, 0)
    if typ != 1:
        problems.append("group type is %d, expected 1" % typ)
    missing = []
    for i in range(count):
        w, h, cc, rs, planes, bits, size, nid = struct.unpack_from("<BBBBHHIH", g, 6 + i * 14)
        if nid not in icons:
            missing.append(nid)
        elif icons[nid][:4] == b"\x89PNG":
            pass
        else:
            hdr = struct.unpack_from("<I", icons[nid], 0)[0]
            if hdr not in (40, 108, 124):
                problems.append("icon %d is not a DIB or PNG" % nid)
    if missing:
        problems.append("group references %d missing icon ids: %s" % (len(missing), missing[:8]))

    if expected:
        orig = open(expected, "rb").read()
        n = struct.unpack_from("<H", orig, 4)[0]
        orig_hashes = set()
        for i in range(n):
            o = 6 + i * 16
            sz, off = struct.unpack_from("<I", orig, o + 8)[0], struct.unpack_from("<I", orig, o + 12)[0]
            import hashlib
            orig_hashes.add(hashlib.sha256(orig[off:off + sz]).hexdigest())
        import hashlib
        got = set(hashlib.sha256(v).hexdigest() for v in icons.values())
        shared = orig_hashes & got
        print("expected icon images: %d, matching the exe: %d" % (len(orig_hashes), len(shared)))
        if not shared:
            problems.append("the exe carries none of the expected icon images")

    if problems:
        print("PROBLEMS:")
        for p in problems:
            print("  - %s" % p)
        return 1
    print("icon lookup OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
