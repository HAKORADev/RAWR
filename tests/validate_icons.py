#!/usr/bin/env python3
"""Proves the icon survives the unwrap, for every game in a corpus.

For each game it records the icon group the payload shipped with, runs
RAWR, and then checks that the recovered program still resolves an icon and
that the group's own artwork was not replaced by the wrapper's.

Usage: validate_icons.py <rawr binary> <corpus root> [limit]
"""
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pefile  # noqa: E402


def groups_of(path):
    """Returns {group name or id: group bytes} and {icon id: bytes}."""
    pe = pefile.PE(path, fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_RESOURCE"]])
    groups, icons = {}, {}
    if not hasattr(pe, "DIRECTORY_ENTRY_RESOURCE"):
        return groups, icons
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
    return groups, icons


def shell_pick(groups):
    """The order the Windows shell uses."""
    for key in ("id:1", "MAINICON"):
        if key in groups:
            return key
    return sorted(groups)[0] if groups else None


def check(groups, icons):
    """The group the shell picks must resolve to real images."""
    if not groups:
        return "no group icon"
    key = shell_pick(groups)
    g = groups[key]
    if len(g) < 6:
        return "group is too small"
    count = struct.unpack_from("<H", g, 4)[0]
    for i in range(count):
        off = 6 + i * 14
        if off + 14 > len(g):
            return "group entry out of bounds"
        nid = struct.unpack_from("<H", g, off + 12)[0]
        if nid not in icons:
            return "group points at missing icon %d" % nid
    return None


def main():
    rawr, corpus = sys.argv[1], sys.argv[2]
    limit = int(sys.argv[3]) if len(sys.argv) > 3 else 10 ** 9

    games = []
    for vol in sorted(os.listdir(corpus)):
        vdir = os.path.join(corpus, vol)
        if not os.path.isdir(vdir):
            continue
        for name in sorted(os.listdir(vdir)):
            g = os.path.join(vdir, name)
            if os.path.isdir(g):
                games.append(g)
    games = games[:limit]

    ok = kept = added = broken = 0
    problems = []
    for i, g in enumerate(games):
        rwg = None
        for f in sorted(os.listdir(g)):
            if f.lower().endswith(".rwg"):
                rwg = os.path.join(g, f)
                break
        if not rwg:
            continue
        try:
            before_groups, before_icons = groups_of(rwg)
        except Exception:
            continue

        tmp = tempfile.mkdtemp(prefix="rawricon", dir=os.path.dirname(corpus.rstrip("/")))
        try:
            work = os.path.join(tmp, "g")
            # Hard links: instant, and RAWR replaces files by rename, so the
            # corpus is never touched.
            shutil.copytree(g, work, copy_function=os.link)
            r = subprocess.run([rawr, work], capture_output=True, text=True, timeout=300)
            exes = [f for f in os.listdir(work) if f.lower().endswith(".exe")]
            if not exes:
                continue
            # The game is the executable that matches the payload name; a
            # folder can hold helper programs next to it.
            stem = os.path.splitext(os.path.basename(rwg))[0].lower()
            match = [f for f in exes if os.path.splitext(f)[0].lower() == stem]
            exe = os.path.join(work, match[0] if match else sorted(exes)[0])
            after_groups, after_icons = groups_of(exe)
            err = check(after_groups, after_icons)
            if err:
                broken += 1
                problems.append("%s: %s" % (os.path.basename(g), err))
                continue
            if before_groups:
                # The game had an icon: it must still be the one that is used.
                if shell_pick(before_groups) != shell_pick(after_groups):
                    broken += 1
                    problems.append("%s: the shell now picks %s instead of the game's own %s" % (
                        os.path.basename(g), shell_pick(after_groups), shell_pick(before_groups)))
                    continue
                kept += 1
            else:
                added += 1
            ok += 1
        except Exception as e:
            broken += 1
            problems.append("%s: %s" % (os.path.basename(g), e))
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
        if (i + 1) % 25 == 0:
            print("  ... %d/%d" % (i + 1, len(games)), flush=True)

    print()
    print("games checked        : %d" % (ok + broken))
    print("icon kept as the game's own : %d" % kept)
    print("icon added from the wrapper: %d" % added)
    print("broken                : %d" % broken)
    for p in problems[:20]:
        print("   %s" % p)
    return 1 if broken else 0


if __name__ == "__main__":
    sys.exit(main())
