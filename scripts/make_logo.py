#!/usr/bin/env python3
"""Draws the RAWR logo.

A heavy grey R with three claw marks burnt out of its upper right corner:
where a claw crosses the letter the grey is removed and an orange flame is
left behind, so the letter looks torn open.

Everything is computed from signed distance fields, so the shape stays
crisp at any size and the script is the real source of the artwork.

Usage: make_logo.py <output.png> [size]
"""

import math
import struct
import sys
import zlib

SIZE = 512
SS = 2  # supersampling factor

GREY = (154, 162, 172)
GREY_DARK = (108, 116, 128)
ORANGE = (240, 120, 24)
ORANGE_HOT = (255, 186, 92)
ORANGE_DARK = (150, 62, 10)


def sd_segment(px, py, ax, ay, bx, by):
    """Distance from p to the segment ab."""
    pax, pay = px - ax, py - ay
    bax, bay = bx - ax, by - ay
    denom = bax * bax + bay * bay
    if denom == 0:
        return math.hypot(pax, pay)
    h = max(0.0, min(1.0, (pax * bax + pay * bay) / denom))
    return math.hypot(pax - bax * h, pay - bay * h)


def sd_rounded_box(px, py, cx, cy, hw, hh, r):
    qx = abs(px - cx) - (hw - r)
    qy = abs(py - cy) - (hh - r)
    return math.hypot(max(qx, 0.0), max(qy, 0.0)) + min(max(qx, qy), 0.0) - r


def sd_r(px, py):
    """The letter R, as a union of primitives."""
    # vertical stem
    d = sd_rounded_box(px, py, 0.30, 0.50, 0.115, 0.375, 0.045)
    # bowl: the upper half of a ring
    dx, dy = px - 0.47, py - 0.345
    ring = abs(math.hypot(dx, dy) - 0.215) - 0.105
    if dy > 0:
        ring = max(ring, dy - 0.02)
    d = min(d, ring)
    # leg
    d = min(d, sd_segment(px, py, 0.47, 0.50, 0.80, 0.87) - 0.105)
    return d


def sd_ellipse(px, py, cx, cy, rx, ry):
    dx = (px - cx) / rx
    dy = (py - cy) / ry
    k = math.hypot(dx, dy)
    return (k - 1.0) * min(rx, ry)


def flame_sdf(px, py):
    """A flame licking up over the upper right of the letter.

    The shape is a union of a few tongues of fire with a wavy edge, so it
    reads as a scorch rather than a blob.
    """
    # the main body of the flame, biting into the bowl of the letter
    d = sd_ellipse(px, py, 0.70, 0.29, 0.165, 0.185)
    # tongues reaching up and to the right
    d = min(d, sd_ellipse(px, py, 0.745, 0.135, 0.062, 0.135))
    d = min(d, sd_ellipse(px, py, 0.815, 0.215, 0.048, 0.088))
    d = min(d, sd_ellipse(px, py, 0.660, 0.105, 0.040, 0.098))
    # a tongue reaching left into the letter
    d = min(d, sd_ellipse(px, py, 0.575, 0.255, 0.085, 0.080))
    # wavy edge
    wobble = 0.016 * math.sin(px * 47.0) * math.sin(py * 37.0 + 1.3)
    return d + wobble


def claw_sd(px, py, index):
    """One claw mark: a tapered slash with a hooked tip."""
    pts = [
        (0.665 + index * 0.070, 0.090),
        (0.640 + index * 0.070, 0.190),
        (0.585 + index * 0.068, 0.275),
        (0.505 + index * 0.062, 0.330),
    ]
    widths = [0.026, 0.021, 0.014, 0.004]
    d = 1e9
    for i in range(len(pts) - 1):
        ax, ay = pts[i]
        bx, by = pts[i + 1]
        d = min(d, sd_segment(px, py, ax, ay, bx, by) - widths[i + 1])
    return d


def render(size):
    n = size * SS
    rows = []
    for y in range(n):
        row = bytearray()
        py = (y + 0.5) / n
        for x in range(n):
            px = (x + 0.5) / n
            dr = sd_r(px, py)
            df = flame_sdf(px, py)
            dc = min(claw_sd(px, py, i) for i in range(3))

            if df <= 0.0:
                # the flame. hot in the middle, deeper orange towards the rim
                t = max(0.0, min(1.0, (df + 0.16) / 0.16))
                col = tuple(int(ORANGE_HOT[i] + (ORANGE[i] - ORANGE_HOT[i]) * t) for i in range(3))
                a = 1.0
                if dc <= 0.0:
                    # the claw marks cut through the flame
                    col = tuple(int(c * 0.45) for c in col)
            elif dr <= 0.0:
                # the letter, scorched where the flame is about to reach it
                t = max(0.0, min(1.0, df / 0.055))
                base = tuple(int(GREY_DARK[i] + (GREY[i] - GREY_DARK[i]) * max(0.0, min(1.0, (py - 0.12) / 0.76)))
                             for i in range(3))
                burn = tuple(int(ORANGE_DARK[i] + (base[i] - ORANGE_DARK[i]) * t) for i in range(3))
                col = burn
                a = 1.0
                if dc <= 0.002:
                    # a claw went all the way through the letter
                    a = 0.0
            else:
                col = (0, 0, 0)
                a = 0.0
            row += bytes((col[0], col[1], col[2], int(round(a * 255))))
        rows.append(bytes(row))

    # downsample
    out = bytearray()
    for y in range(size):
        out += b"\x00"
        for x in range(size):
            acc = [0, 0, 0, 0]
            for dy in range(SS):
                r = rows[y * SS + dy]
                for dx in range(SS):
                    o = (x * SS + dx) * 4
                    acc[0] += r[o]
                    acc[1] += r[o + 1]
                    acc[2] += r[o + 2]
                    acc[3] += r[o + 3]
            npx = SS * SS
            out += bytes((acc[0] // npx, acc[1] // npx, acc[2] // npx, acc[3] // npx))
    return bytes(out)


def write_png(path, size, raw):
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    open(path, "wb").write(png)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "rawr.png"
    size = int(sys.argv[2]) if len(sys.argv) > 2 else SIZE
    raw = render(size)
    write_png(out, size, raw)
    print("wrote %s (%dx%d, RGBA, transparent background)" % (out, size, size))


if __name__ == "__main__":
    main()
