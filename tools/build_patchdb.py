#!/usr/bin/env python3
"""Convert GoldHEN-format XML game patches into the fps60 patch database.

Usage: build_patchdb.py <patch-repo-dir> <output-file>

Reads patches/xml (PS4 titles) and patches/xml_prospero (PS5 titles) from a
checkout of https://github.com/illusion0001/PS4-PS5-Game-Patch and keeps only
frame-rate unlock patches. All value encoding happens here so the payload only
has to deal with raw bytes:

  G <title> <name> <author> <note> <appver> <appelf> <imagebase> <tids> <ok>
  A <address> <hex bytes>              absolute write
  M <pattern> <offset> <hex bytes>     pattern scan, write at match+offset

Fields are tab-separated. <ok> is 0 when the patch uses a feature the payload
does not implement; such patches are listed in the UI but cannot be enabled.
"""

import glob
import os
import re
import struct
import sys
import xml.etree.ElementTree as ET

PS4_IMAGE_BASE = 0x400000

NAME_RE = re.compile(r"60\s*fps|fps\s*unlock|unlock\s*fps|uncap|remove\s*30\s*fps\s*cap",
                     re.IGNORECASE)


class Unsupported(Exception):
    pass


def clean(s):
    # Notes sometimes contain a literal "\n" escape; the UI shows one line.
    return " ".join((s or "").replace("\\n", " ").split())


def parse_int(v):
    v = v.strip()
    return int(v, 0)


def unescape(s):
    # Patch strings use C-style escapes such as \x00 and \n.
    out = bytearray()
    i = 0
    raw = s.encode("utf-8")
    while i < len(raw):
        c = raw[i]
        if c == 0x5C and i + 1 < len(raw):
            n = chr(raw[i + 1])
            if n == "x" and i + 3 < len(raw):
                out.append(int(raw[i + 2:i + 4], 16))
                i += 4
                continue
            out.extend({"n": b"\n", "t": b"\t", "r": b"\r", "0": b"\0",
                        "\\": b"\\"}.get(n, b"\\" + n.encode()))
            i += 2
            continue
        out.append(c)
        i += 1
    return bytes(out)


def encode_value(typ, value):
    if typ == "byte":
        return struct.pack("<B", parse_int(value) & 0xFF)
    if typ == "bytes16":
        return struct.pack("<H", parse_int(value) & 0xFFFF)
    if typ == "bytes32":
        return struct.pack("<I", parse_int(value) & 0xFFFFFFFF)
    if typ == "bytes64":
        return struct.pack("<Q", parse_int(value) & 0xFFFFFFFFFFFFFFFF)
    if typ in ("bytes", "mask"):
        h = re.sub(r"\s+", "", value)
        if not h or len(h) % 2 or not re.fullmatch(r"[0-9a-fA-F]+", h):
            raise Unsupported("bad hex value %r" % value)
        return bytes.fromhex(h)
    if typ == "float32":
        return struct.pack("<f", float(value))
    if typ == "float64":
        return struct.pack("<d", float(value))
    if typ == "utf8":
        return unescape(value)
    if typ == "utf16":
        return unescape(value).decode("utf-8").encode("utf-16-le")
    raise Unsupported("line type %s" % typ)


def check_pattern(p):
    toks = p.split()
    if not toks or len(toks) > 255:
        raise Unsupported("bad pattern")
    toks = ["??" if t == "?" else t for t in toks]
    for t in toks:
        if t != "??" and not re.fullmatch(r"[0-9a-fA-F]{2}", t):
            raise Unsupported("bad pattern token %r" % t)
    if all(t == "??" for t in toks):
        raise Unsupported("wildcard-only pattern")
    return " ".join(t.lower() for t in toks)


def convert_meta(meta, tids, ps5):
    lines = []
    imagebase = meta.get("ImageBase")
    if imagebase is not None:
        imagebase = parse_int(imagebase)
    elif ps5:
        raise Unsupported("PS5 patch without ImageBase")
    else:
        imagebase = PS4_IMAGE_BASE

    for line in meta.iter("Line"):
        typ = line.get("Type", "")
        addr = line.get("Address", "")
        value = line.get("Value", "")
        if typ == "mask":
            data = encode_value("mask", value)
            off = parse_int(line.get("Offset", "0").replace("+", ""))
            lines.append("M\t%s\t%d\t%s" % (check_pattern(addr), off, data.hex()))
            continue
        if typ.startswith("mask"):
            raise Unsupported("line type %s" % typ)
        a = parse_int(addr)
        if a < imagebase:
            raise Unsupported("address below image base")
        data = encode_value(typ, value)
        if not data:
            raise Unsupported("empty value")
        lines.append("A\t0x%x\t%s" % (a, data.hex()))

    if not lines:
        raise Unsupported("no patch lines")
    return imagebase, lines


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__)
    repo, out_path = sys.argv[1], sys.argv[2]
    files = [(f, False) for f in sorted(glob.glob(os.path.join(repo, "patches/xml/*.xml")))]
    files += [(f, True) for f in sorted(glob.glob(os.path.join(repo, "patches/xml_prospero/*.xml")))]
    if not files:
        sys.exit("no patch files found under %s/patches" % repo)

    out = ["# fps60 patch database v1"]
    total = unsupported = 0
    for path, ps5 in files:
        try:
            root = ET.parse(path).getroot()
        except ET.ParseError as e:
            print("skip %s: %s" % (path, e), file=sys.stderr)
            continue
        tids = [clean(t.text) for t in root.iter("ID") if clean(t.text)]
        if not tids:
            continue
        for meta in root.findall("Metadata"):
            name = clean(meta.get("Name"))
            if not NAME_RE.search(name):
                continue
            ok = 1
            try:
                imagebase, lines = convert_meta(meta, tids, ps5)
            except (Unsupported, ValueError, struct.error) as e:
                print("unsupported %s / %s: %s" % (os.path.basename(path), name, e),
                      file=sys.stderr)
                imagebase, lines, ok = 0, [], 0
                unsupported += 1
            total += 1
            out.append("\t".join([
                "G", clean(meta.get("Title")), name, clean(meta.get("Author")),
                clean(meta.get("Note")), clean(meta.get("AppVer")),
                clean(meta.get("AppElf")) or "eboot.bin", "0x%x" % imagebase,
                ",".join(tids), str(ok)]))
            out.extend(lines)

    with open(out_path, "w", encoding="utf-8") as f:
        f.write("\n".join(out) + "\n")
    print("%d fps patches (%d unsupported) -> %s" % (total, unsupported, out_path),
          file=sys.stderr)


if __name__ == "__main__":
    main()
