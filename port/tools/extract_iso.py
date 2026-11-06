#!/usr/bin/env python3
# Extract files from a raw PSX CD image (2352-byte Mode2/Form1 sectors) via its
# ISO9660 filesystem. The port needs FROGPSX.MWD (and optionally the .STR/.WAV
# streams) from your own game disc; this pulls them out of a .bin backup.
#
#   python3 extract_iso.py Frogger.bin out_dir
import sys, os

SECTOR = 2352
USER   = 2048
HDR    = 24          # sync(12)+header(4)+subheader(8) before 2048 user bytes

def read_lba(f, lba, count=1):
    out = b""
    for i in range(count):
        f.seek((lba + i) * SECTOR + HDR)
        out += f.read(USER)
    return out

def u32le(b, o): return b[o] | (b[o+1] << 8) | (b[o+2] << 16) | (b[o+3] << 24)

def parse_dir(f, lba, length):
    data = read_lba(f, lba, (length + USER - 1) // USER)
    entries, p = [], 0
    while p < length:
        rlen = data[p]
        if rlen == 0:
            p = ((p // USER) + 1) * USER            # skip to next sector
            if p >= length: break
            continue
        ext   = u32le(data, p + 2)
        dlen  = u32le(data, p + 10)
        flags = data[p + 25]
        nlen  = data[p + 32]
        name  = data[p + 33:p + 33 + nlen].decode("ascii", "replace")
        entries.append((name, ext, dlen, flags))
        p += rlen
    return entries

def main():
    if len(sys.argv) < 3:
        print("usage: extract_iso.py <image.bin> <out_dir> [NAME]"); return 1
    img, out = sys.argv[1], sys.argv[2]
    only = sys.argv[3].upper() if len(sys.argv) > 3 else None
    os.makedirs(out, exist_ok=True)
    with open(img, "rb") as f:
        pvd = read_lba(f, 16)
        assert pvd[1:6] == b"CD001", "not an ISO9660 image"
        root = pvd[156:156+34]
        root_lba, root_len = u32le(root, 2), u32le(root, 10)
        for name, ext, dlen, flags in parse_dir(f, root_lba, root_len):
            if flags & 0x02:            # directory
                continue
            base = name.split(";")[0]   # strip ISO ;version
            if only and base.upper() != only:
                continue
            path = os.path.join(out, base)
            with open(path, "wb") as g:
                remaining = dlen; lba = ext
                while remaining > 0:
                    chunk = read_lba(f, lba); n = min(USER, remaining)
                    g.write(chunk[:n]); remaining -= n; lba += 1
            print("extracted %-16s %10d bytes" % (base, dlen))
    return 0

sys.exit(main())
