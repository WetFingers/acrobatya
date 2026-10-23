# -*- coding: utf-8 -*-
"""Общая библиотека генераторов фикстур «Оттиска» (tools/gen/*.py).

Что: подмножество TrueType с сохранением GID (шрифт Type0/Identity-H с ToUnicode
или простой TrueType с /Differences), сборка страниц (текст, линии, картинки),
детерминированное сохранение PDF через pikepdf (фиксированные /Info, XMP, /ID).
Откуда: шрифты C:\\Windows\\Fonts (Arial, Times New Roman — fsType 8, встраивание
разрешено), pikepdf/Pillow/numpy из venv C:\\Tools\\venv-test.
Детерминизм: даты — FIXED_DATE, XMP собирается из констант, /ID — deterministic_id
qpdf (MD5 содержимого), тег подмножества шрифта — из хеша набора глифов,
случайность — только random.Random/numpy.default_rng с явным seed.

Запуск генераторов: "C:\\Tools\\venv-test\\Scripts\\python.exe" -X utf8 -I <скрипт>.
Под -I каталог скрипта не попадает в sys.path, поэтому каждый генератор сам
добавляет tools/gen в sys.path перед `import _genlib`.
"""
from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
import struct
import subprocess
import sys
import uuid
import zlib
from datetime import datetime, timezone
from pathlib import Path

import pikepdf
from pikepdf import Array, Dictionary, Name, Stream, String

# --- Константы детерминизма ---------------------------------------------------

FIXED_DT = datetime(2026, 1, 1, 12, 0, 0, tzinfo=timezone.utc)
FIXED_DATE = "D:20260101120000Z"           # /CreationDate, /ModDate, /M
FIXED_XMP_DATE = "2026-01-01T12:00:00Z"
AUTHOR = "Оттиск: генератор фикстур"
PRODUCER = "Ottisk tools/gen (pikepdf)"
SEED = 1

A4 = (595.2756, 841.8898)                 # 210 × 297 мм в пунктах
A3 = (841.8898, 1190.5512)
A5 = (419.5276, 595.2756)
LETTER = (612.0, 792.0)

REPO = Path(__file__).resolve().parents[2]
FONTS_DIR = Path(os.environ.get("WINDIR", "C:/Windows")) / "Fonts"


def mutool_path() -> str:
    """mutool из Release-сборки MuPDF (MUPDF_ROOT или C:\\src\\mupdf)."""
    env = os.environ.get("OTTISK_MUTOOL")
    if env:
        return env
    root = os.environ.get("MUPDF_ROOT", "C:/src/mupdf")
    return str(Path(root) / "platform/win32/x64/Release/mutool.exe")


def qpdf_path() -> str:
    tools = os.environ.get("OTTISK_TOOLS", "C:/Tools")
    return str(Path(tools) / "qpdf/bin/qpdf.exe")


def corpus_gen_dir() -> Path:
    base = os.environ.get("OTTISK_CORPUS")
    if not base:
        base = r"C:\Tools\ottisk\corpus"  # не %LOCALAPPDATA%: её перенаправляет приложение Claude
    return Path(base) / "gen"


def common_args(desc: str, corpus: bool = False, extra=None) -> argparse.Namespace:
    """Общие параметры: --out <корень> (куда класть tests/…; по умолчанию репозиторий),
    --corpus <каталог> (для больших файлов вне Git); extra(ap) — свои параметры скрипта."""
    ap = argparse.ArgumentParser(description=desc)
    ap.add_argument("--out", default=str(REPO), help="корень вывода (по умолчанию — репозиторий)")
    if corpus:
        ap.add_argument("--corpus", default=str(corpus_gen_dir()), help="каталог corpus\\gen")
    ap.add_argument("--tmp", default=None, help="временный каталог (по умолчанию %%TEMP%%\\ottisk-gen)")
    if extra:
        extra(ap)
    return ap.parse_args()


def tmp_dir(args, name: str) -> Path:
    base = Path(args.tmp) if args.tmp else Path(os.environ.get("TEMP", "C:/Temp")) / "ottisk-gen"
    d = base / name
    d.mkdir(parents=True, exist_ok=True)
    return d


def run(cmd, check=True, capture=True, env=None) -> subprocess.CompletedProcess:
    cmd = [str(c) for c in cmd]
    r = subprocess.run(cmd, capture_output=capture, env=env)
    if check and r.returncode != 0:
        err = (r.stderr or b"").decode("utf-8", "replace")
        raise RuntimeError(f"команда упала ({r.returncode}): {' '.join(cmd)}\n{err}")
    return r


def sha256_file(p: Path) -> str:
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def write_json(path: Path, obj) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    data = json.dumps(obj, ensure_ascii=False, indent=2) + "\n"
    path.write_bytes(data.encode("utf-8"))


def write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text.replace("\r\n", "\n").encode("utf-8"))


def say(msg: str) -> None:
    print(msg, flush=True)


# --- TrueType: разбор и подмножество с сохранением GID -----------------------

def _u16(b, o):
    return struct.unpack_from(">H", b, o)[0]


def _i16(b, o):
    return struct.unpack_from(">h", b, o)[0]


def _u32(b, o):
    return struct.unpack_from(">I", b, o)[0]


def _checksum(data: bytes) -> int:
    data = data + b"\0" * ((4 - len(data) % 4) % 4)
    return sum(struct.unpack(">%dI" % (len(data) // 4), data)) & 0xFFFFFFFF


class TrueType:
    """Минимальный разбор TrueType (glyf) — ровно то, что нужно для подмножества."""

    def __init__(self, path: Path):
        self.path = Path(path)
        d = self.data = self.path.read_bytes()
        if d[:4] not in (b"\x00\x01\x00\x00", b"true"):
            raise ValueError(f"{path}: не TrueType (glyf)")
        n = _u16(d, 4)
        self.tables = {}
        for i in range(n):
            tag, _cs, off, ln = struct.unpack_from(">4sIII", d, 12 + 16 * i)
            self.tables[tag.decode("latin-1")] = (off, ln)
        head = self.table("head")
        self.units = _u16(head, 18)
        self.bbox = [_i16(head, 36), _i16(head, 38), _i16(head, 40), _i16(head, 42)]
        self.loc_format = _i16(head, 50)
        hhea = self.table("hhea")
        self.ascent = _i16(hhea, 4)
        self.descent = _i16(hhea, 6)
        self.num_hmetrics = _u16(hhea, 34)
        self.num_glyphs = _u16(self.table("maxp"), 4)
        os2 = self.table("OS/2")
        self.weight = _u16(os2, 4)
        self.cap_height = _i16(os2, 88) if _u16(os2, 0) >= 2 and len(os2) >= 90 else int(self.ascent * 0.7)
        post = self.table("post")
        self.italic_angle = struct.unpack_from(">i", post, 4)[0] / 65536.0
        self.fixed_pitch = _u32(post, 12) != 0
        self._cmap = self._parse_cmap()
        self._loca = self._parse_loca()
        self.ps_name = self._name(6) or self.path.stem
        self.flags_bold = self.weight >= 600

    def table(self, tag: str) -> bytes:
        off, ln = self.tables[tag]
        return self.data[off:off + ln]

    def _name(self, name_id: int):
        if "name" not in self.tables:
            return None
        t = self.table("name")
        count, str_off = _u16(t, 2), _u16(t, 4)
        for i in range(count):
            pid, eid, lid, nid, ln, off = struct.unpack_from(">HHHHHH", t, 6 + 12 * i)
            if nid == name_id and pid == 3 and eid == 1 and lid == 0x409:
                return t[str_off + off:str_off + off + ln].decode("utf-16-be")
        return None

    def _parse_cmap(self) -> dict:
        t = self.table("cmap")
        n = _u16(t, 2)
        subs = {}
        for i in range(n):
            pid, eid, off = struct.unpack_from(">HHI", t, 4 + 8 * i)
            subs[(pid, eid)] = off
        res = {}
        if (3, 10) in subs and _u16(t, subs[(3, 10)]) == 12:
            o = subs[(3, 10)]
            ngroups = _u32(t, o + 12)
            for g in range(ngroups):
                s, e, gid = struct.unpack_from(">III", t, o + 16 + 12 * g)
                for c in range(s, e + 1):
                    res[c] = gid + (c - s)
            return res
        o = subs.get((3, 1))
        if o is None or _u16(t, o) != 4:
            raise ValueError(f"{self.path}: нет cmap (3,1) формата 4")
        segx2 = _u16(t, o + 6)
        seg = segx2 // 2
        ends = o + 14
        starts = ends + segx2 + 2
        deltas = starts + segx2
        ranges = deltas + segx2
        for s in range(seg):
            end = _u16(t, ends + 2 * s)
            start = _u16(t, starts + 2 * s)
            delta = _i16(t, deltas + 2 * s)
            ro = _u16(t, ranges + 2 * s)
            for c in range(start, end + 1):
                if c == 0xFFFF:
                    continue
                if ro == 0:
                    gid = (c + delta) & 0xFFFF
                else:
                    gpos = ranges + 2 * s + ro + 2 * (c - start)
                    gid = _u16(t, gpos)
                    if gid:
                        gid = (gid + delta) & 0xFFFF
                if gid:
                    res[c] = gid
        return res

    def _parse_loca(self):
        t = self.table("loca")
        n = self.num_glyphs + 1
        if self.loc_format == 0:
            return [2 * v for v in struct.unpack_from(">%dH" % n, t, 0)]
        return list(struct.unpack_from(">%dI" % n, t, 0))

    def gid(self, ch: str) -> int:
        return self._cmap.get(ord(ch), 0)

    def has(self, ch: str) -> bool:
        return ord(ch) in self._cmap

    def advance(self, gid: int) -> int:
        hm = self.table("hmtx")
        if gid < self.num_hmetrics:
            return _u16(hm, 4 * gid)
        return _u16(hm, 4 * (self.num_hmetrics - 1))

    def lsb(self, gid: int) -> int:
        hm = self.table("hmtx")
        if gid < self.num_hmetrics:
            return _i16(hm, 4 * gid + 2)
        return _i16(hm, 4 * self.num_hmetrics + 2 * (gid - self.num_hmetrics))

    def glyph(self, gid: int) -> bytes:
        g = self.table("glyf")
        return g[self._loca[gid]:self._loca[gid + 1]]

    def components(self, gid: int):
        g = self.glyph(gid)
        if len(g) < 10 or _i16(g, 0) >= 0:
            return []
        out, o = [], 10
        while True:
            flags, cg = struct.unpack_from(">HH", g, o)
            out.append(cg)
            o += 4 + (4 if flags & 0x0001 else 2)
            if flags & 0x0008:
                o += 2
            elif flags & 0x0040:
                o += 4
            elif flags & 0x0080:
                o += 8
            if not flags & 0x0020:
                break
        return out

    def closure(self, gids) -> list:
        keep = {0}
        stack = list(gids)
        while stack:
            g = stack.pop()
            if g in keep and g != 0:
                continue
            keep.add(g)
            stack.extend(c for c in self.components(g) if c not in keep)
        return sorted(keep)

    def subset(self, gids, cmap_chars: dict) -> bytes:
        """Подмножество с сохранением GID: глифы вне набора пустые, метрики нулевые.
        cmap_chars — {unicode: gid} для нового cmap (3,1) формата 4."""
        keep = set(self.closure(gids))
        glyf = bytearray()
        loca = []
        hmtx = bytearray()
        for g in range(self.num_glyphs):
            loca.append(len(glyf))
            if g in keep:
                gd = self.glyph(g)
                glyf += gd + b"\0" * ((4 - len(gd) % 4) % 4)
                hmtx += struct.pack(">Hh", self.advance(g), self.lsb(g))
            else:
                hmtx += b"\0\0\0\0"
        loca.append(len(glyf))
        head = bytearray(self.table("head"))
        struct.pack_into(">I", head, 8, 0)          # checkSumAdjustment
        struct.pack_into(">h", head, 50, 1)         # indexToLocFormat = long
        hhea = bytearray(self.table("hhea"))
        struct.pack_into(">H", hhea, 34, self.num_glyphs)
        maxp = bytearray(self.table("maxp"))
        post = bytearray(self.table("post")[:32])
        struct.pack_into(">I", post, 0, 0x00030000)
        struct.pack_into(">IIII", post, 16, 0, 0, 0, 0)
        tables = {
            "OS/2": self.table("OS/2"),
            "cmap": _build_cmap4(cmap_chars),
            "glyf": bytes(glyf),
            "head": bytes(head),
            "hhea": bytes(hhea),
            "hmtx": bytes(hmtx),
            "loca": struct.pack(">%dI" % len(loca), *loca),
            "maxp": bytes(maxp),
            "name": self._build_name(),
            "post": bytes(post),
        }
        for tag in ("cvt ", "fpgm", "prep"):
            if tag in self.tables:
                tables[tag] = self.table(tag)
        return _assemble_sfnt(tables)

    def _build_name(self) -> bytes:
        recs = []
        for nid in (1, 2, 3, 4, 6):
            s = self._name(nid)
            if s:
                recs.append((nid, s.encode("utf-16-be")))
        strings = b""
        rec_bytes = b""
        for nid, s in recs:
            rec_bytes += struct.pack(">HHHHHH", 3, 1, 0x409, nid, len(s), len(strings))
            strings += s
        return struct.pack(">HHH", 0, len(recs), 6 + 12 * len(recs)) + rec_bytes + strings


def _build_cmap4(chars: dict) -> bytes:
    cps = sorted(c for c in chars if c < 0xFFFF)
    segs = [(c, c, (chars[c] - c) & 0xFFFF) for c in cps] + [(0xFFFF, 0xFFFF, 1)]
    n = len(segs)
    sr = 2
    es = 0
    while sr * 2 <= 2 * n:
        sr *= 2
        es += 1
    rs = 2 * n - sr
    body = struct.pack(">HHHH", 2 * n, sr, es, rs)
    body += b"".join(struct.pack(">H", e) for _s, e, _d in segs) + b"\0\0"
    body += b"".join(struct.pack(">H", s) for s, _e, _d in segs)
    body += b"".join(struct.pack(">H", d) for _s, _e, d in segs)
    body += b"\0\0" * n
    sub = struct.pack(">HHH", 4, 6 + len(body), 0) + body
    return struct.pack(">HHHHI", 0, 1, 3, 1, 12) + sub


def _assemble_sfnt(tables: dict) -> bytes:
    tags = sorted(tables)
    n = len(tags)
    sr = 1
    es = 0
    while sr * 2 <= n:
        sr *= 2
        es += 1
    hdr = struct.pack(">IHHHH", 0x00010000, n, sr * 16, es, n * 16 - sr * 16)
    off = 12 + 16 * n
    dir_bytes = b""
    body = b""
    for t in tags:
        data = tables[t]
        dir_bytes += struct.pack(">4sIII", t.encode("latin-1"), _checksum(data), off + len(body), len(data))
        body += data + b"\0" * ((4 - len(data) % 4) % 4)
    font = bytearray(hdr + dir_bytes + body)
    adj = (0xB1B0AFBA - _checksum(bytes(font))) & 0xFFFFFFFF
    # head — найти смещение в собранном файле
    hoff = 12 + 16 * tags.index("head")
    head_off = _u32(font, hoff + 8)
    struct.pack_into(">I", font, head_off + 8, adj)
    return bytes(font)


# --- AGL-имена (только то, что нужно генераторам) ------------------------------

_AGL_ASCII = {
    " ": "space", "!": "exclam", '"': "quotedbl", "#": "numbersign", "$": "dollar",
    "%": "percent", "&": "ampersand", "'": "quotesingle", "(": "parenleft", ")": "parenright",
    "*": "asterisk", "+": "plus", ",": "comma", "-": "hyphen", ".": "period", "/": "slash",
    "0": "zero", "1": "one", "2": "two", "3": "three", "4": "four", "5": "five", "6": "six",
    "7": "seven", "8": "eight", "9": "nine", ":": "colon", ";": "semicolon", "<": "less",
    "=": "equal", ">": "greater", "?": "question", "@": "at", "[": "bracketleft",
    "\\": "backslash", "]": "bracketright", "^": "asciicircum", "_": "underscore",
    "`": "grave", "{": "braceleft", "|": "bar", "}": "braceright", "~": "asciitilde",
    "«": "guillemotleft", "»": "guillemotright", "—": "emdash", "–": "endash", "№": "afii61352",
}


def agl_name(ch: str) -> str:
    c = ord(ch)
    if ch.isascii() and ch.isalpha():
        return ch
    if ch in _AGL_ASCII:
        return _AGL_ASCII[ch]
    if 0x0410 <= c <= 0x042F:                      # А..Я
        i = c - 0x0410
        return "afii%d" % (10017 + i if i <= 5 else 10018 + i)
    if 0x0430 <= c <= 0x044F:                      # а..я
        i = c - 0x0430
        return "afii%d" % (10065 + i if i <= 5 else 10066 + i)
    if c == 0x0401:
        return "afii10023"                         # Ё
    if c == 0x0451:
        return "afii10071"                         # ё
    return "uni%04X" % c


# --- Шрифты в PDF -----------------------------------------------------------------

FONT_FILES = {
    "arial": "arial.ttf", "arialbd": "arialbd.ttf", "ariali": "ariali.ttf",
    "times": "times.ttf", "timesbd": "timesbd.ttf", "timesi": "timesi.ttf",
    "cour": "cour.ttf", "georgia": "georgia.ttf",
}


class PdfFont:
    """Шрифт Type0 (Identity-H, CIDFontType2, ToUnicode) — подмножество строится при finish()."""

    def __init__(self, pdf: pikepdf.Pdf, name: str):
        self.pdf = pdf
        self.name = name
        self.tt = TrueType(FONTS_DIR / FONT_FILES.get(name, name))
        self.ref = pdf.make_indirect(Dictionary(Type=Name.Font))
        self.used = {}           # gid -> unicode str
        self.finished = False

    def encode(self, text: str) -> bytes:
        out = bytearray()
        for ch in text:
            g = self.tt.gid(ch)
            if g == 0:
                raise ValueError(f"в шрифте {self.name} нет символа {ch!r} (U+{ord(ch):04X})")
            prev = self.used.get(g)
            if prev is not None and prev != ch:
                raise ValueError(f"глиф {g} шрифта {self.name} уже занят {prev!r}, нужен {ch!r}")
            self.used[g] = ch
            out += struct.pack(">H", g)
        return bytes(out)

    def width(self, text: str, size: float) -> float:
        return sum(self.tt.advance(self.tt.gid(ch)) for ch in text) * size / self.tt.units

    def ascent(self, size):
        return self.tt.ascent * size / self.tt.units

    def descent(self, size):
        return self.tt.descent * size / self.tt.units

    def tag(self) -> str:
        h = hashlib.sha256((self.name + ":" + ",".join(map(str, sorted(self.used)))).encode()).digest()
        return "".join(chr(ord("A") + b % 26) for b in h[:6])

    def finish(self):
        if self.finished:
            return
        self.finished = True
        tt = self.tt
        gids = sorted(self.used)
        cmap_chars = {ord(ch): g for g, ch in self.used.items()}
        prog = tt.subset(gids, cmap_chars)
        base = self.tag() + "+" + tt.ps_name
        s = 1000.0 / tt.units
        fontfile = Stream(self.pdf, prog)
        fontfile.Length1 = len(prog)
        flags = 32 | (1 if tt.fixed_pitch else 0) | (2 if "times" in self.name or "georgia" in self.name else 0)
        if tt.italic_angle:
            flags |= 64
        fd = self.pdf.make_indirect(Dictionary(
            Type=Name.FontDescriptor, FontName=Name("/" + base), Flags=flags,
            FontBBox=Array([round(v * s) for v in tt.bbox]), ItalicAngle=round(tt.italic_angle, 2),
            Ascent=round(tt.ascent * s), Descent=round(tt.descent * s),
            CapHeight=round(tt.cap_height * s), StemV=120 if tt.flags_bold else 80,
            FontFile2=self.pdf.make_indirect(fontfile)))
        w = Array()
        run_start, run = None, []
        for g in gids + [None]:
            if g is not None and run_start is not None and g == run_start + len(run):
                run.append(round(tt.advance(g) * s))
                continue
            if run_start is not None:
                w.append(run_start)
                w.append(Array(run))
            if g is not None:
                run_start, run = g, [round(tt.advance(g) * s)]
        cid = self.pdf.make_indirect(Dictionary(
            Type=Name.Font, Subtype=Name.CIDFontType2, BaseFont=Name("/" + base),
            CIDSystemInfo=Dictionary(Registry=String("Adobe"), Ordering=String("Identity"), Supplement=0),
            FontDescriptor=fd, DW=1000, W=w, CIDToGIDMap=Name.Identity))
        tu = self.pdf.make_indirect(Stream(self.pdf, tounicode_cmap(self.used)))
        self.ref.Subtype = Name.Type0
        self.ref.BaseFont = Name("/" + base)
        self.ref.Encoding = Name("/Identity-H")
        self.ref.DescendantFonts = Array([cid])
        self.ref.ToUnicode = tu


def tounicode_cmap(used: dict, width: int = 4) -> bytes:
    lines = [
        "/CIDInit /ProcSet findresource begin", "12 dict begin", "begincmap",
        "/CIDSystemInfo << /Registry (Adobe) /Ordering (UCS) /Supplement 0 >> def",
        "/CMapName /Adobe-Identity-UCS def", "/CMapType 2 def", "1 begincodespacerange",
        "<%s> <%s>" % ("0" * width, "F" * width), "endcodespacerange",
    ]
    items = sorted(used.items())
    for i in range(0, len(items), 100):
        chunk = items[i:i + 100]
        lines.append("%d beginbfchar" % len(chunk))
        for g, ch in chunk:
            lines.append("<%0*X> <%s>" % (width, g, ch.encode("utf-16-be").hex().upper()))
        lines.append("endbfchar")
    lines += ["endcmap", "CMapName currentdict /CMap defineresource pop", "end", "end"]
    return ("\n".join(lines) + "\n").encode("ascii")


class SimpleTrueTypeFont:
    """Простой TrueType (8-битный) с /BaseEncoding /WinAnsiEncoding и /Differences
    для кириллицы (коды как в cp1251), без ToUnicode — текст извлекается по именам глифов."""

    def __init__(self, pdf: pikepdf.Pdf, name: str):
        self.pdf = pdf
        self.name = name
        self.tt = TrueType(FONTS_DIR / FONT_FILES.get(name, name))
        self.ref = pdf.make_indirect(Dictionary(Type=Name.Font))
        self.used = {}           # code -> ch

    def encode(self, text: str) -> bytes:
        out = bytearray()
        for ch in text:
            b = ch.encode("cp1251")
            self.used[b[0]] = ch
            out += b
        return bytes(out)

    def finish(self):
        tt = self.tt
        codes = sorted(self.used)
        gids = [tt.gid(self.used[c]) for c in codes]
        prog = tt.subset(gids, {ord(self.used[c]): tt.gid(self.used[c]) for c in codes})
        tag = "".join(chr(ord("A") + b % 26) for b in hashlib.sha256(bytes(codes)).digest()[:6])
        base = tag + "+" + tt.ps_name
        s = 1000.0 / tt.units
        first, last = codes[0], codes[-1]
        widths = []
        for c in range(first, last + 1):
            ch = self.used.get(c)
            widths.append(round(tt.advance(tt.gid(ch)) * s) if ch else 0)
        diffs = Array()
        prev = None
        for c in codes:
            if c >= 0x80:
                if prev is None or c != prev + 1:
                    diffs.append(c)
                diffs.append(Name("/" + agl_name(self.used[c])))
                prev = c
        ff = Stream(self.pdf, prog)
        ff.Length1 = len(prog)
        fd = self.pdf.make_indirect(Dictionary(
            Type=Name.FontDescriptor, FontName=Name("/" + base), Flags=32,
            FontBBox=Array([round(v * s) for v in tt.bbox]), ItalicAngle=0,
            Ascent=round(tt.ascent * s), Descent=round(tt.descent * s),
            CapHeight=round(tt.cap_height * s), StemV=80, FontFile2=self.pdf.make_indirect(ff)))
        self.ref.Subtype = Name.TrueType
        self.ref.BaseFont = Name("/" + base)
        self.ref.FirstChar = first
        self.ref.LastChar = last
        self.ref.Widths = Array(widths)
        self.ref.FontDescriptor = fd
        self.ref.Encoding = Dictionary(Type=Name.Encoding, BaseEncoding=Name.WinAnsiEncoding, Differences=diffs)

    def width(self, text, size):
        return sum(self.tt.advance(self.tt.gid(ch)) for ch in text) * size / self.tt.units


# --- Страница и холст -----------------------------------------------------------

def _num(v: float) -> str:
    s = ("%.3f" % v).rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


def _pdfstr(b: bytes) -> str:
    return "<" + b.hex().upper() + ">"


class Canvas:
    """Накопитель операторов содержимого страницы и её ресурсов."""

    def __init__(self, doc: "Doc", w: float, h: float):
        self.doc = doc
        self.w, self.h = w, h
        self.ops = []
        self.fonts = {}          # key -> font obj
        self.xobjects = {}
        self.extg = {}

    def _fkey(self, font) -> str:
        for k, v in self.fonts.items():
            if v is font:
                return k
        k = "F%d" % (len(self.fonts) + 1)
        self.fonts[k] = font
        return k

    def raw(self, s: str):
        self.ops.append(s)

    def color(self, rgb, stroke=False):
        op = "RG" if stroke else "rg"
        self.ops.append("%s %s %s %s" % (_num(rgb[0]), _num(rgb[1]), _num(rgb[2]), op))

    def text(self, x, y, s, font, size, rgb=(0, 0, 0), render=None):
        if not s:
            return
        k = self._fkey(font)
        self.ops.append("BT")
        self.color(rgb)
        if render is not None:
            self.ops.append("%d Tr" % render)
        self.ops.append("/%s %s Tf %s %s Td %s Tj" % (k, _num(size), _num(x), _num(y), _pdfstr(font.encode(s))))
        self.ops.append("ET")

    def text_center(self, cx, y, s, font, size, rgb=(0, 0, 0)):
        self.text(cx - font.width(s, size) / 2, y, s, font, size, rgb)

    def paragraph(self, x, y, width, text, font, size, leading=None, rgb=(0, 0, 0), bottom=40):
        """Выводит абзац с переносом по словам; возвращает y после абзаца (или None, если не влез)."""
        leading = leading or size * 1.3
        for line in wrap(text, font, size, width):
            if y < bottom:
                return None
            self.text(x, y, line, font, size, rgb)
            y -= leading
        return y

    def rect(self, x, y, w, h, stroke=None, fill=None, lw=1.0):
        self.ops.append("q")
        if fill is not None:
            self.color(fill)
        if stroke is not None:
            self.color(stroke, stroke=True)
            self.ops.append("%s w" % _num(lw))
        op = "B" if (fill is not None and stroke is not None) else ("f" if fill is not None else "S")
        self.ops.append("%s %s %s %s re %s" % (_num(x), _num(y), _num(w), _num(h), op))
        self.ops.append("Q")

    def line(self, x1, y1, x2, y2, rgb=(0, 0, 0), lw=1.0, dash=None):
        self.ops.append("q")
        self.color(rgb, stroke=True)
        self.ops.append("%s w" % _num(lw))
        if dash:
            self.ops.append("[%s] 0 d" % " ".join(_num(v) for v in dash))
        self.ops.append("%s %s m %s %s l S" % (_num(x1), _num(y1), _num(x2), _num(y2)))
        self.ops.append("Q")

    def image(self, xobj, x, y, w, h):
        k = None
        for kk, v in self.xobjects.items():
            if v is xobj:
                k = kk
        if k is None:
            k = "Im%d" % (len(self.xobjects) + 1)
            self.xobjects[k] = xobj
        self.ops.append("q %s 0 0 %s %s %s cm /%s Do Q" % (_num(w), _num(h), _num(x), _num(y), k))

    def content(self) -> bytes:
        return ("\n".join(self.ops) + "\n").encode("latin-1")

    def resources(self) -> Dictionary:
        res = Dictionary()
        if self.fonts:
            res.Font = Dictionary({"/" + k: v.ref for k, v in self.fonts.items()})
        if self.xobjects:
            res.XObject = Dictionary({"/" + k: v for k, v in self.xobjects.items()})
        if self.extg:
            res.ExtGState = Dictionary({"/" + k: v for k, v in self.extg.items()})
        return res


def wrap(text: str, font, size: float, width: float):
    words = text.split()
    line = ""
    for w in words:
        cand = (line + " " + w) if line else w
        if font.width(cand, size) <= width or not line:
            line = cand
        else:
            yield line
            line = w
    if line:
        yield line


class Doc:
    """PDF-документ генератора: шрифты по имени, страницы-холсты, детерминированное сохранение."""

    def __init__(self, title: str, subject: str = "", lang: str = "ru-RU", creator: str = ""):
        self.pdf = pikepdf.new()
        self.title = title
        self.subject = subject
        self.lang = lang
        self.creator = creator or Path(sys.argv[0]).name
        self._fonts = {}
        self._simple = []

    def font(self, name: str = "arial") -> PdfFont:
        if name not in self._fonts:
            self._fonts[name] = PdfFont(self.pdf, name)
        return self._fonts[name]

    def simple_font(self, name: str = "arial") -> SimpleTrueTypeFont:
        f = SimpleTrueTypeFont(self.pdf, name)
        self._simple.append(f)
        return f

    def canvas(self, size=A4) -> Canvas:
        return Canvas(self, size[0], size[1])

    def add_page(self, cv: Canvas, rotate: int = 0, extra: dict | None = None) -> pikepdf.Page:
        content = Stream(self.pdf, cv.content())
        d = Dictionary(Type=Name.Page, MediaBox=Array([0, 0, round(cv.w, 4), round(cv.h, 4)]),
                       Resources=cv.resources(), Contents=self.pdf.make_indirect(content))
        if rotate:
            d.Rotate = rotate
        for k, v in (extra or {}).items():
            d[k] = v
        self.pdf.pages.append(pikepdf.Page(d))
        return self.pdf.pages[-1]

    def jpeg(self, img, quality=85, **kw) -> pikepdf.Object:
        return jpeg_xobject(self.pdf, img, quality=quality, **kw)

    def finish_fonts(self):
        for f in self._fonts.values():
            if f.used:
                f.finish()
        for f in self._simple:
            if f.used:
                f.finish()

    def save(self, path: Path, **kw):
        self.finish_fonts()
        save_pdf(self.pdf, path, title=self.title, subject=self.subject, lang=self.lang,
                 creator=self.creator, **kw)


# --- Картинки -----------------------------------------------------------------------

def jpeg_xobject(pdf, img, quality=85, subsampling=None):
    """Pillow Image → XObject DCTDecode (L, RGB). Детерминированно: один и тот же Pillow/libjpeg."""
    buf = io.BytesIO()
    kw = {"quality": quality, "optimize": False, "progressive": False}
    if subsampling is not None:
        kw["subsampling"] = subsampling
    img.save(buf, "JPEG", **kw)
    cs = {"L": Name.DeviceGray, "RGB": Name.DeviceRGB, "CMYK": Name.DeviceCMYK}[img.mode]
    st = Stream(pdf, buf.getvalue())
    st.Type = Name.XObject
    st.Subtype = Name.Image
    st.Width, st.Height = img.size
    st.ColorSpace = cs
    st.BitsPerComponent = 8
    st.Filter = Name.DCTDecode
    if img.mode == "CMYK":
        st.Decode = Array([1, 0, 1, 0, 1, 0, 1, 0])
    return pdf.make_indirect(st)


def flate_xobject(pdf, img, smask=None):
    """Pillow Image (L/RGB/1) → XObject FlateDecode; smask — Image 'L' (альфа)."""
    if img.mode == "1":
        data = img.tobytes()
        bpc, cs = 1, Name.DeviceGray
    else:
        data = img.tobytes()
        bpc = 8
        cs = {"L": Name.DeviceGray, "RGB": Name.DeviceRGB}[img.mode]
    st = Stream(pdf, zlib.compress(data, 9))
    st.Type = Name.XObject
    st.Subtype = Name.Image
    st.Width, st.Height = img.size
    st.ColorSpace = cs
    st.BitsPerComponent = bpc
    st.Filter = Name.FlateDecode
    if smask is not None:
        sm = Stream(pdf, zlib.compress(smask.tobytes(), 9))
        sm.Type = Name.XObject
        sm.Subtype = Name.Image
        sm.Width, sm.Height = smask.size
        sm.ColorSpace = Name.DeviceGray
        sm.BitsPerComponent = 8
        sm.Filter = Name.FlateDecode
        st.SMask = pdf.make_indirect(sm)
    return pdf.make_indirect(st)


# --- Метаданные и сохранение ---------------------------------------------------------

def _xml_escape(s: str) -> str:
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;").replace('"', "&quot;")


def xmp_packet(title: str, creator_tool: str, doc_key: str, author: str = AUTHOR,
               producer: str = PRODUCER, lang: str = "ru-RU") -> bytes:
    did = uuid.uuid5(uuid.NAMESPACE_URL, "ottisk:" + doc_key)
    iid = uuid.uuid5(uuid.NAMESPACE_URL, "ottisk:instance:" + doc_key)
    x = f"""<?xpacket begin="\ufeff" id="W5M0MpCehiHzreSzNTczkc9d"?>
<x:xmpmeta xmlns:x="adobe:ns:meta/">
 <rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">
  <rdf:Description rdf:about=""
    xmlns:dc="http://purl.org/dc/elements/1.1/"
    xmlns:xmp="http://ns.adobe.com/xap/1.0/"
    xmlns:pdf="http://ns.adobe.com/pdf/1.3/"
    xmlns:xmpMM="http://ns.adobe.com/xap/1.0/mm/">
   <dc:format>application/pdf</dc:format>
   <dc:title><rdf:Alt><rdf:li xml:lang="x-default">{_xml_escape(title)}</rdf:li></rdf:Alt></dc:title>
   <dc:creator><rdf:Seq><rdf:li>{_xml_escape(author)}</rdf:li></rdf:Seq></dc:creator>
   <dc:language><rdf:Bag><rdf:li>{_xml_escape(lang)}</rdf:li></rdf:Bag></dc:language>
   <xmp:CreateDate>{FIXED_XMP_DATE}</xmp:CreateDate>
   <xmp:ModifyDate>{FIXED_XMP_DATE}</xmp:ModifyDate>
   <xmp:MetadataDate>{FIXED_XMP_DATE}</xmp:MetadataDate>
   <xmp:CreatorTool>{_xml_escape(creator_tool)}</xmp:CreatorTool>
   <pdf:Producer>{_xml_escape(producer)}</pdf:Producer>
   <xmpMM:DocumentID>uuid:{did}</xmpMM:DocumentID>
   <xmpMM:InstanceID>uuid:{iid}</xmpMM:InstanceID>
  </rdf:Description>
 </rdf:RDF>
</x:xmpmeta>
<?xpacket end="w"?>"""
    return x.encode("utf-8")


def normalize_dates(pdf: pikepdf.Pdf) -> None:
    """Все /M и /CreationDate аннотаций и файловых спецификаций — FIXED_DATE."""
    for page in pdf.pages:
        annots = page.obj.get("/Annots")
        if annots is None:
            continue
        for a in annots:
            for key in ("/M", "/CreationDate"):
                if key in a:
                    a[key] = String(FIXED_DATE)


def set_metadata(pdf: pikepdf.Pdf, title: str, subject: str = "", lang: str = "ru-RU",
                 creator: str = "", doc_key: str = "", author: str = AUTHOR,
                 producer: str = PRODUCER, xmp: bool = True) -> None:
    info = Dictionary(Title=String(title), Author=String(author), Creator=String(creator),
                      Producer=String(producer), CreationDate=String(FIXED_DATE), ModDate=String(FIXED_DATE))
    if subject:
        info.Subject = String(subject)
    pdf.trailer.Info = pdf.make_indirect(info)
    if lang:
        pdf.Root.Lang = String(lang)
    if xmp:
        st = Stream(pdf, xmp_packet(title, creator, doc_key or title, author=author, producer=producer, lang=lang))
        st.Type = Name.Metadata
        st.Subtype = Name.XML
        pdf.Root.Metadata = pdf.make_indirect(st)
    elif "/Metadata" in pdf.Root:
        del pdf.Root["/Metadata"]


def save_pdf(pdf: pikepdf.Pdf, path: Path, title: str, subject: str = "", lang: str = "ru-RU",
             creator: str = "", objstm: bool = False, version: str = "1.7", xmp: bool = True,
             author: str = AUTHOR, producer: str = PRODUCER, keep_info: bool = False) -> Path:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    normalize_dates(pdf)
    if not keep_info:
        set_metadata(pdf, title, subject, lang, creator, doc_key=path.name, author=author,
                     producer=producer, xmp=xmp)
    if "/ID" in pdf.trailer:                   # иначе qpdf сохранит чужой ID[0]
        del pdf.trailer["/ID"]
    mode = pikepdf.ObjectStreamMode.generate if objstm else pikepdf.ObjectStreamMode.disable
    pdf.save(path, deterministic_id=True, compress_streams=True, object_stream_mode=mode,
             min_version=version, force_version=None, fix_metadata_version=False,
             recompress_flate=False, linearize=False)
    return path


def pdf_text(path: Path, password: str | None = None) -> str:
    """Текст через mutool draw -F txt (для самопроверок генераторов)."""
    cmd = [mutool_path(), "draw", "-q", "-F", "txt", "-o", "-"]
    if password is not None:
        cmd += ["-p", password]
    cmd.append(str(path))
    r = run(cmd)
    return r.stdout.decode("utf-8", "replace")


def check_size(path: Path, limit: int) -> None:
    n = Path(path).stat().st_size
    if n > limit:
        raise RuntimeError(f"{path}: {n} байт > предела {limit}")


# --- Детерминированная «случайность» для pyHanko (соли, IV, /ID) --------------------

class DetRandom:
    """Поток байтов SHA-256(seed ‖ счётчик) — подменяет secrets.token_bytes и os.urandom."""

    def __init__(self, seed: int):
        self.key = hashlib.sha256(b"ottisk-gen-seed:%d" % seed).digest()
        self.counter = 0

    def bytes(self, n: int) -> bytes:
        out = b""
        while len(out) < n:
            out += hashlib.sha256(self.key + struct.pack(">Q", self.counter)).digest()
            self.counter += 1
        return out[:n]


def patch_randomness(seed: int) -> DetRandom:
    """Только для процессов-генераторов: делает соли/IV/ID pyHanko воспроизводимыми.
    Криптографически это ослабляет файлы — они тестовые, пароли известны."""
    import random
    import secrets
    r = DetRandom(seed)
    secrets.token_bytes = lambda nbytes=None: r.bytes(32 if nbytes is None else nbytes)
    os.urandom = lambda n: r.bytes(n)
    random.seed(seed)
    return r


class FixedDatetime(datetime):
    """datetime, у которого now() — всегда FIXED_DT (UTC, без местного пояса машины)."""

    @classmethod
    def now(cls, tz=None):
        return FIXED_DT


def patch_pyhanko_time() -> None:
    """pyHanko ставит текущее время в /ModDate, xmp:ModifyDate, /M подписи, штамп и signingTime —
    подменяем datetime во всех загруженных модулях pyhanko.*."""
    import pyhanko.pdf_utils.metadata.info  # noqa: F401
    import pyhanko.pdf_utils.metadata.xmp_xml  # noqa: F401
    import pyhanko.pdf_utils.writer  # noqa: F401
    for mname, mod in list(sys.modules.items()):
        if mname.startswith("pyhanko") and getattr(mod, "datetime", None) is datetime:
            mod.datetime = FixedDatetime


def encrypt_aes256_r6(src: Path, dst: Path, owner: str, user: str, seed: int) -> None:
    """AES-256 (V5/R6, ISO 32000-2) через pyHanko без PDF MAC, с детерминированными солями.
    qpdf для R6 берёт соли и ключ файла из системного ГСЧ — два прогона дали бы разные байты."""
    patch_randomness(seed)
    from pyhanko.pdf_utils.crypt import _util as crypt_util
    from pyhanko.pdf_utils.crypt import filter_mixins
    from pyhanko.pdf_utils.reader import PdfFileReader
    from pyhanko.pdf_utils.writer import copy_into_new_writer
    patch_pyhanko_time()
    # IV каждой строки/потока выводим из её содержимого: не зависим от порядка шифрования.
    salt = hashlib.sha256(b"ottisk-iv:%d" % seed).digest()

    def det_aes_cbc_encrypt(key, data, iv, use_padding=True):
        iv = hashlib.sha256(salt + bytes(key) + bytes(data)).digest()[:16]
        return crypt_util.aes_cbc_encrypt(key, data, iv, use_padding=use_padding)
    filter_mixins.aes_cbc_encrypt = det_aes_cbc_encrypt
    with open(src, "rb") as f:
        r = PdfFileReader(f)
        w = copy_into_new_writer(r)
        w.encrypt(owner_pass=owner, user_pass=user, pdf_mac=False)
        Path(dst).parent.mkdir(parents=True, exist_ok=True)
        with open(dst, "wb") as o:
            w.write(o)
