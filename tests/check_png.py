#!/usr/bin/env python3
"""Decode every <w>x<h>.png in a directory with the standard library and compare with <w>x<h>.alpha."""
import glob, os, struct, sys, zlib

def check(path):
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "bad signature"
    pos, chunks, idat, ihdr = 8, [], b"", None
    while pos < len(data):
        n, = struct.unpack(">I", data[pos:pos + 4]); typ = data[pos + 4:pos + 8]; body = data[pos + 8:pos + 8 + n]
        crc, = struct.unpack(">I", data[pos + 8 + n:pos + 12 + n])
        assert crc == zlib.crc32(typ + body) & 0xFFFFFFFF, "bad CRC in %s" % typ
        chunks.append(typ)
        if typ == b"IHDR": ihdr = struct.unpack(">IIBBBBB", body)
        if typ == b"IDAT": idat += body
        pos += 12 + n
    assert chunks[0] == b"IHDR" and chunks[-1] == b"IEND" and b"IDAT" in chunks, chunks
    w, h, depth, ctype, comp, filt, inter = ihdr
    assert (depth, ctype, comp, filt, inter) == (8, 6, 0, 0, 0), ihdr
    raw = zlib.decompress(idat)                       # also checks the zlib header and the Adler-32
    assert len(raw) == h * (1 + 4 * w), "wrong decoded size"
    alpha = open(path[:-4] + ".alpha", "rb").read()
    for y in range(h):
        row = raw[y * (1 + 4 * w):(y + 1) * (1 + 4 * w)]
        assert row[0] == 0, "filter byte"
        px = row[1:]
        assert px[0::4] == b"\x00" * w and px[1::4] == b"\x00" * w and px[2::4] == b"\x00" * w, "rgb must be 0"
        assert px[3::4] == alpha[y * w:(y + 1) * w], "alpha mismatch in row %d" % y
    try:                                              # optional second opinion from a real image library
        from PIL import Image
        im = Image.open(path).convert("RGBA")
        assert im.size == (w, h) and im.getchannel("A").tobytes() == alpha, "PIL disagrees"
    except ImportError:
        pass
    return w, h

d = sys.argv[1]
files = sorted(glob.glob(os.path.join(d, "*.png")))
assert files, "no PNG files"
for f in files:
    print("png ok", os.path.basename(f), "%dx%d" % check(f))
