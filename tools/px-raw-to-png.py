#!/usr/bin/env python3
"""Convert the pixel probe's /dev/shmem/aa_px_<n>.raw files (native/src/px_probe.c) to PNG.

A file is a text header line "KBPX1 <w> <h> <stride> <format> <seq> <ms>" and h rows of w*4 bytes.
Format 8 (SCREEN_FORMAT_RGBA8888) and 9 (RGBX8888) are stored as little-endian 0xAARRGGBB words,
i.e. bytes B, G, R, A. Other formats are written as if they were the same, with a warning.

usage: px-raw-to-png.py file.raw [more.raw ...]   (writes file.png next to each)
Needs Pillow (pip install pillow).
"""
import sys
from PIL import Image


def convert(path):
    with open(path, "rb") as f:
        data = f.read()
    end = data.index(b"\n")
    head = data[:end].decode("ascii").split()
    if head[0] != "KBPX1":
        raise ValueError(f"{path}: not a pixel probe file")
    w, h, stride, fmt, seq, ms = (int(v) for v in head[1:7])
    if fmt not in (8, 9):
        print(f"{path}: format {fmt} is not RGBA8888/RGBX8888, colours may be wrong", file=sys.stderr)
    rows = data[end + 1:]
    if len(rows) < w * 4 * h:                     # cut short (copied while written): black rest
        print(f"{path}: {len(rows)} of {w * 4 * h} bytes, the rest is black", file=sys.stderr)
        rows += bytes(w * 4 * h - len(rows))
    img = Image.frombuffer("RGBA", (w, h), rows[: w * 4 * h], "raw", "BGRA", w * 4, 1)
    out = path[:-4] + ".png" if path.endswith(".raw") else path + ".png"
    img.convert("RGB").save(out)
    print(f"{out}: {w}x{h}, probe {seq}, {ms} ms")


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    for p in sys.argv[1:]:
        convert(p)
