#!/usr/bin/env python3
"""Builds a synthetic Reflexive Arcade installation to test RAWR end to end.

The fixture is generated from the same maths the real wrapper uses, so a
successful unwrap proves the whole chain works: seed derivation, payload
decryption, resource restore and cleanup. It contains no game code and no
copyrighted material, so it can live in the repository and run in CI on
every platform.

Usage:  make_fixture.py <output folder>
"""

import os
import struct
import sys
import zlib

FILE_ALIGNMENT = 0x200
SECTION_ALIGNMENT = 0x1000
HEADER_SIZE = 0x400

# A plausible x86 function prologue at the entry point.
ENTRY_CODE = bytes([
    0x55,                    # push ebp
    0x8B, 0xEC,              # mov ebp, esp
    0x33, 0xC0,              # xor eax, eax
    0x55,                    # push ebp
    0x8B, 0xEC,              # mov ebp, esp
    0x5D,                    # pop ebp
    0xC3,                    # ret
]) + bytes(range(0x10, 0x40))


def crc32_sections(data, sections, crc=0):
    for raw_offset, raw_size in sections:
        crc = zlib.crc32(data[raw_offset:raw_offset + raw_size], crc)
    return crc


def build_pe(sections, entry_rva, extra_rdata=b""):
    """sections: list of (name, vsize, payload bytes)."""
    header = bytearray(HEADER_SIZE)
    header[0:2] = b"MZ"
    struct.pack_into("<I", header, 0x3C, 0x80)
    header[0x80:0x84] = b"PE\x00\x00"

    pe_off = 0x80
    opt_off = pe_off + 24
    sect_off = opt_off + 0xE0

    struct.pack_into("<H", header, pe_off + 4, 0x014C)     # machine i386
    struct.pack_into("<H", header, pe_off + 6, len(sections))
    struct.pack_into("<H", header, pe_off + 20, 0xE0)      # optional header size
    struct.pack_into("<H", header, opt_off, 0x010B)        # magic PE32
    struct.pack_into("<I", header, opt_off + 16, entry_rva)
    struct.pack_into("<I", header, opt_off + 28, 0x00400000)
    struct.pack_into("<I", header, opt_off + 32, SECTION_ALIGNMENT)
    struct.pack_into("<I", header, opt_off + 36, FILE_ALIGNMENT)
    struct.pack_into("<H", header, opt_off + 68, 2)        # subsystem: windows gui

    image = bytearray(header)
    table = sect_off
    for i, (name, vsize, payload) in enumerate(sections):
        raw = len(image)
        image += payload
        while len(image) % FILE_ALIGNMENT:
            image += b"\x00"
        struct.pack_into("<8s", image, table + i * 40, name.encode()[:8])
        struct.pack_into("<I", image, table + i * 40 + 8, vsize)
        struct.pack_into("<I", image, table + i * 40 + 12, 0x1000 + i * 0x1000)
        struct.pack_into("<I", image, table + i * 40 + 16, len(payload))
        struct.pack_into("<I", image, table + i * 40 + 20, raw)
        struct.pack_into("<I", image, table + i * 40 + 36, 0x60000020)
    size_of_image = 0x1000 + len(sections) * 0x1000
    struct.pack_into("<I", image, opt_off + 56, size_of_image)
    struct.pack_into("<I", image, opt_off + 60, HEADER_SIZE)
    return bytes(image), image


def stream_init(seed):
    state = [0] * 250
    current = seed & 0xFFFFFFFF
    for index in range(249, -1, -1):
        product = (current * 0x41C64E6D + 0x3039) & 0xFFFFFFFFFFFFFFFF
        current = product & 0xFFFFFFFF
        state[index] = (product >> 16) & 0xFFFFFFFF
    clear_mask = 0xFFFFFFFF
    bit = 0x80000000
    index = 3
    while bit != 0:
        state[index] = (state[index] & clear_mask) | bit
        clear_mask >>= 1
        bit >>= 1
        index += 7
    return state


def keystream(seed, length):
    state = stream_init(seed)
    a, b = 0, 0x67
    out = bytearray(length)
    for i in range(length):
        state[a] ^= state[b]
        out[i] = state[a] & 0xFF
        a = (a + 1) % 250
        b = (b + 1) % 250
    return bytes(out)


def stream_decrypt(data, seed):
    """The wrapper subtracts the keystream, so this is the inverse."""
    key = keystream(seed, len(data))
    return bytes((v - k) & 0xFF for v, k in zip(data, key))


def stream_encrypt(data, seed):
    """The wrapper subtracts the keystream, so encryption adds it."""
    key = keystream(seed, len(data))
    return bytes((v + k) & 0xFF for v, k in zip(data, key))


def pe_section_list(data):
    """Returns [(name, raw_offset, raw_size)] of a PE image."""
    pe_off = struct.unpack_from("<I", data, 0x3C)[0]
    count = struct.unpack_from("<H", data, pe_off + 6)[0]
    opt_size = struct.unpack_from("<H", data, pe_off + 20)[0]
    table = pe_off + 24 + opt_size
    out = []
    for i in range(count):
        off = table + i * 40
        name = data[off:off + 8].rstrip(b"\x00").decode()
        raw_size = struct.unpack_from("<I", data, off + 16)[0]
        raw_off = struct.unpack_from("<I", data, off + 20)[0]
        out.append((name, raw_off, raw_size))
    return out


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    root = sys.argv[1]
    support = os.path.join(root, "ReflexiveArcade")
    os.makedirs(support, exist_ok=True)

    # ------------------------------------------------------------------
    # The launcher stub. It carries the Reflexive marker so RAWR can
    # recognise it, and its section bytes feed the seed.
    # ------------------------------------------------------------------
    stub_text = b"\x55\x8B\xEC" + bytes((i * 3) & 0xFF for i in range(0x10, 0x100)) + b"ReflexiveArcade.dll\x00"
    stub_rdata = b"ReflexiveArcade.dll\x00radll_GetUnlockCode\x00" + bytes((i * 5) & 0xFF for i in range(0x40))
    stub = bytearray()
    for name, payload in ((".text", stub_text), (".rdata", stub_rdata), (".data", bytes(0x200))):
        stub += payload
    stub_pe, _ = build_pe(
        [(".text", 0x200, stub_text), (".rdata", 0x200, stub_rdata), (".data", 0x200, bytes(0x200))],
        0x1010)
    with open(os.path.join(root, "FixtureGame.exe"), "wb") as f:
        f.write(stub_pe)

    # ------------------------------------------------------------------
    # RAW_003 is a PE as well, and its sections feed the seed.
    # ------------------------------------------------------------------
    raw3_text = b"\x55\x8B\xEC" + bytes((i * 11) & 0xFF for i in range(0x10, 0x100))
    raw3_pe, _ = build_pe(
        [(".text", 0x200, raw3_text), (".rdata", 0x200, bytes(0x200)), (".data", 0x200, bytes(0x200))],
        0x1010)
    with open(os.path.join(support, "RAW_003.wdt"), "wb") as f:
        f.write(raw3_pe)

    # ------------------------------------------------------------------
    # The four support images. Only their sizes matter.
    # ------------------------------------------------------------------
    image_sizes = {}
    for name, size in (("background.jpg", 517), ("button_normal.jpg", 101),
                       ("button_hover.jpg", 173), ("button_pressed.jpg", 89)):
        path = os.path.join(support, name)
        with open(path, "wb") as f:
            f.write(bytes((i * 7) & 0xFF for i in range(size)))
        image_sizes[name] = size

    # ------------------------------------------------------------------
    # Seed one: CRC32 over the stub sections, the RAW_003 sections and the
    # image sizes, exactly like the wrapper did it.
    # ------------------------------------------------------------------
    seed1 = crc32_sections(stub_pe, [(o, s) for (_n, o, s) in pe_section_list(stub_pe)])
    seed1 = crc32_sections(raw3_pe, [(o, s) for (_n, o, s) in pe_section_list(raw3_pe)], seed1)
    for name in ("background.jpg", "button_normal.jpg", "button_hover.jpg", "button_pressed.jpg"):
        seed1 = zlib.crc32(image_sizes[name].to_bytes(4, "little"), seed1)

    config_plain = (
        "App Version String=Rev. 1\r\n"
        "Application Name=Fixture Game\r\n"
        "Application Price=0.00\r\n"
        "Demo Time Seconds=3600\r\n"
        "Enforce Demo Time=1\r\n"
        "Decrement Time When Idle=0\r\n"
        "Is .NET Executable=0\r\n"
        "Game Needs Short Fixed Encryption=0\r\n"
    ).encode("latin-1")
    config_enc = stream_encrypt(config_plain, seed1)
    with open(os.path.join(support, "RAW_002.wdt"), "wb") as f:
        f.write(config_enc)

    # ------------------------------------------------------------------
    # Seed two: derived from the encrypted configuration.
    # ------------------------------------------------------------------
    dword_count = len(config_enc) // 4
    seed2 = sum(struct.unpack_from("<I", config_enc, i * 4)[0] for i in range(dword_count)) & 0xFFFFFFFF

    # ------------------------------------------------------------------
    # The game itself, with its entry region encrypted.
    # ------------------------------------------------------------------
    game_text = ENTRY_CODE + bytes((i * 3) & 0xFF for i in range(0x40, 0x1F0))
    game_pe, game_image = build_pe(
        [(".text", 0x200, game_text), (".rdata", 0x200, bytes(0x200)), (".data", 0x200, bytes(0x200))],
        0x1010)
    game_image = bytearray(game_pe)
    # The encrypted region starts at the entry point inside .text and runs
    # to the end of that section's usable raw bytes.
    text_raw = 0x400
    region_start = text_raw + 0x10
    region_len = 0x200 - 0x10
    encrypted = bytearray(game_image)
    encrypted[region_start:region_start + region_len] = stream_encrypt(
        bytes(game_image[region_start:region_start + region_len]), seed2)
    with open(os.path.join(root, "FixtureGame.RWG"), "wb") as f:
        f.write(bytes(encrypted))

    # Keep a copy of the untouched game so the test can compare it.
    with open(os.path.join(root, "..", "fixture_game_original.bin"), "wb") as f:
        f.write(game_pe)

    print("fixture written to %s" % root)
    print("  seed1 = 0x%08x" % seed1)
    print("  seed2 = 0x%08x" % seed2)
    return 0


if __name__ == "__main__":
    sys.exit(main())
