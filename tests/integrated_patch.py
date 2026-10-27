#!/usr/bin/env python3
"""Prototype of the "integrated wrapper" patch.

The Reflexive binding layer that some games compile into their own
executable resolves radll_HasTheProductBeenPurchased through
GetProcAddress and then calls it through a pointer. Replacing that
indirect call with "mov eax, 1; ret" makes the game behave as if it were
registered. Both are six bytes, so the patch never moves any code.

Usage: integrated_patch.py <exe> [--apply]
"""
import struct
import sys

import pefile

TARGET = b"radll_HasTheProductBeenPurchased\x00"
CALL_INDIRECT = b"\xff\x15"          # call dword ptr [imm32]
RETURN_ONE = b"\xb8\x01\x00\x00\x00\xc3"  # mov eax, 1; ret


def find_string(data, needle):
    pos = 0
    while True:
        i = data.find(needle, pos)
        if i < 0:
            return None
        if i == 0 or data[i - 1] == 0:
            return i
        pos = i + 1


def rva_to_offset(pe, rva):
    for s in pe.sections:
        span = max(s.Misc_VirtualSize, s.SizeOfRawData)
        if s.VirtualAddress <= rva < s.VirtualAddress + span:
            return s.PointerToRawData + (rva - s.VirtualAddress)
    return None


def analyze(path):
    data = open(path, "rb").read()
    pe = pefile.PE(path, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase

    soff = find_string(data, TARGET)
    if soff is None:
        return None, "the export name is not in the image"
    srva = None
    for s in pe.sections:
        if s.PointerToRawData <= soff < s.PointerToRawData + s.SizeOfRawData:
            srva = s.VirtualAddress + (soff - s.PointerToRawData)
            break
    sva = base + srva
    needle = b"\x68" + struct.pack("<I", sva)

    # The push of the export name, followed by the GetProcAddress call and
    # the store of the resolved pointer.
    push_at = None
    for s in pe.sections:
        if not (s.Characteristics & 0x20000000):
            continue
        blob = data[s.PointerToRawData:s.PointerToRawData + s.SizeOfRawData]
        off = blob.find(needle)
        if off >= 0:
            push_at = s.PointerToRawData + off
            break
    if push_at is None:
        return None, "the export name is never pushed"

    fnptr = None
    window = data[push_at:push_at + 48]
    for i in range(len(window) - 6):
        # mov dword ptr [imm32], eax
        if window[i] == 0xA3:
            fnptr = struct.unpack_from("<I", window, i + 1)[0]
            break
        # mov dword ptr [imm32], eax
        if window[i] == 0x89 and window[i + 1] == 0x05:
            fnptr = struct.unpack_from("<I", window, i + 2)[0]
            break
    if fnptr is None:
        return None, "the resolved pointer is not stored nearby"

    call_pat_a = CALL_INDIRECT + struct.pack("<I", fnptr)   # call dword ptr [fnptr]
    call_pat_b = b"\xff\x25" + struct.pack("<I", fnptr)    # jmp  dword ptr [fnptr]  (a thunk)
    call_at = None
    kind = None
    for s in pe.sections:
        if not (s.Characteristics & 0x20000000):
            continue
        blob = data[s.PointerToRawData:s.PointerToRawData + s.SizeOfRawData]
        off = blob.find(call_pat_a)
        if off >= 0:
            call_at = s.PointerToRawData + off
            kind = "call"
            break
        off = blob.find(call_pat_b)
        if off >= 0:
            call_at = s.PointerToRawData + off
            kind = "thunk"
            break
    if call_at is None:
        return None, "the function pointer is never called"

    return (call_at, kind), None


def main():
    path = sys.argv[1]
    apply = "--apply" in sys.argv
    site, err = analyze(path)
    if site is None:
        print("%s: no patch site (%s)" % (path, err))
        return 1
    off, kind = site
    data = bytearray(open(path, "rb").read())
    print("%s: %s site at file offset 0x%x" % (path, kind, off))
    print("   before: %s" % data[off:off + 6].hex())
    if apply:
        data[off:off + 6] = RETURN_ONE
        open(path, "wb").write(bytes(data))
        print("   after : %s  (written)" % RETURN_ONE.hex())
    else:
        print("   after : %s" % RETURN_ONE.hex())
    return 0


if __name__ == "__main__":
    sys.exit(main())
