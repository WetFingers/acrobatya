# -*- coding: utf-8 -*-
"""make_mutants.py — 100 битых PDF (seed 1) вне Git: <corpus>\\gen\\mutants\\m###-<вид>-<источник>.pdf + index.json.

Что: мутации своих фикстур (tests/data/mini, tests/fixtures/{annots,forms,demo,sig,enc}):
  trunc ×15      — усечение до 10…99,9 % длины;
  startxref ×10  — неверное число после startxref (0, за концом, мусор, сдвиг, огромное);
  length ×15     — неверный /Length потока (той же ширины — смещения xref целы);
  kids ×8        — рекурсия /Kids (дерево страниц ссылается на себя/предка), xref исправлен fix-qdf;
  prev ×8        — петля /Prev в добавленных секциях xref (на себя и цикл из двух);
  bomb ×4        — flate-бомбы малого размера (16 и 64 МБ пробелов, двойной Flate, картинка 1 бит 20000²);
  bitflip ×20    — 1…32 случайных байта;
  size ×5        — /Size в trailer: 0, 1, огромный, отрицательный;
  count ×5       — /Count страниц: 0, −1, 10⁹, не число;
  nesting ×4     — вложенность массивов/словарей 10 000…100 000 в каталоге;
  xref ×6        — порча таблицы xref (мусор, сдвиг смещений, неверный подраздел).
Ожидание для всех: процесс жив, нет ASan-ошибок; MuPDF открывает с ремонтом или бросает
FZ_ERROR_FORMAT/SYNTAX/LIMIT/UNSUPPORTED (index.json: "expect").
Каталог по умолчанию — %OTTISK_CORPUS%\\gen или %LOCALAPPDATA%\\ottisk\\corpus\\gen (--corpus).
Откуда: pikepdf (QDF-копии источников), C:\\Tools\\qpdf\\bin\\fix-qdf.exe (пересчёт xref после правки
структуры), random.Random(seed=1).
Детерминизм: только seed=1 и детерминированные источники (сначала make_mini/annots/forms/demo/sig/enc).
Запуск: "C:\\Tools\\venv-test\\Scripts\\python.exe" -X utf8 -I tools/gen/make_mutants.py [--out <корень>] [--corpus <каталог>]
"""
import io
import random
import re
import shutil
import sys
import zlib
from pathlib import Path

sys.dont_write_bytecode = True                     # без __pycache__ в tools/gen
sys.path.insert(0, str(Path(__file__).resolve().parent))

import pikepdf  # noqa: E402
from pikepdf import Array, Dictionary, Name, Stream  # noqa: E402

import _genlib as g  # noqa: E402

SOURCES = [
    "tests/data/mini/blank-a4.pdf", "tests/data/mini/cyr-cid.pdf", "tests/data/mini/type1-diff.pdf",
    "tests/data/mini/truncated.pdf", "tests/data/mini/enc-r6.pdf",
    "tests/fixtures/annots/matrix-28.pdf", "tests/fixtures/forms/fields-all.pdf",
    "tests/fixtures/forms/xfa-stub.pdf", "tests/fixtures/demo/comments.pdf",
    "tests/fixtures/demo/mixed-sizes.pdf", "tests/fixtures/sig/signed-bb.pdf",
    "tests/fixtures/enc/enc-rc4-40.pdf",
]
# структурные мутации — только по открываемым без пароля и без ремонта
STRUCT_SOURCES = [s for s in SOURCES if "enc" not in s and "truncated" not in s]

EXPECT = "no-crash: открывается с ремонтом или FZ_ERROR_FORMAT|SYNTAX|LIMIT|UNSUPPORTED"


def fix_qdf_path() -> str:
    return str(Path(g.qpdf_path()).with_name("fix-qdf.exe"))


class Ctx:
    def __init__(self, root: Path, tmp: Path):
        self.root = root
        self.tmp = tmp
        self._raw = {}
        self._qdf = {}

    def raw(self, src: str) -> bytes:
        if src not in self._raw:
            self._raw[src] = (self.root / src).read_bytes()
        return self._raw[src]

    def qdf(self, src: str) -> bytes:
        """QDF-копия: без сжатия, классический xref, объекты по одному — удобно править текстом."""
        if src not in self._qdf:
            pdf = pikepdf.open(self.root / src)
            buf = io.BytesIO()
            pdf.save(buf, qdf=True, deterministic_id=True, object_stream_mode=pikepdf.ObjectStreamMode.disable)
            self._qdf[src] = buf.getvalue()
        return self._qdf[src]

    def fix(self, data: bytes) -> bytes:
        a = self.tmp / "in.qdf"
        b = self.tmp / "out.pdf"
        a.write_bytes(data)
        g.run([fix_qdf_path(), a, b])
        return b.read_bytes()


def obj_span(data: bytes, num: int):
    m = re.search(rb"(?m)^%d 0 obj\b" % num, data)
    if not m:
        return None
    end = data.find(b"endobj", m.end())
    return m.start(), end


def pages_obj_num(data: bytes) -> int:
    m = re.search(rb"/Pages (\d+) 0 R", data)
    return int(m.group(1))


# --- виды мутаций ----------------------------------------------------------------

def m_trunc(ctx, rng, src, k):
    data = ctx.raw(src)
    frac = [0.1, 0.25, 0.5, 0.75, 0.9, 0.97, 0.999][k % 7]
    n = max(16, int(len(data) * frac))
    return data[:n], {"fraction": frac, "bytes": n}


def m_startxref(ctx, rng, src, k):
    data = ctx.raw(src)
    i = data.rfind(b"startxref")
    m = re.match(rb"startxref\s+(\d+)", data[i:]) if i >= 0 else None
    old = int(m.group(1)) if m else -1
    new = [b"0", str(len(data) + 1000).encode(), b"abc", str(max(0, old - 7)).encode(), b"99999999999"][k % 5]
    if m:
        out = data[:i] + b"startxref\n" + new + data[i + m.end():]
    else:                                              # у источника хвоста нет — дописываем ложный
        out = data + b"\nstartxref\n" + new + b"\n%%EOF\n"
    return out, {"old": old, "new": new.decode()}


def m_length(ctx, rng, src, k):
    data = ctx.qdf(src)
    lens = [(m.start(1), m.end(1)) for m in re.finditer(rb"(?m)^(\d+)\nendobj", data)]
    # в QDF длины потоков — отдельные объекты «N 0 obj\n<число>\nendobj»; берём случайный
    s, e = lens[rng.randrange(len(lens))]
    old = data[s:e]
    w = len(old)
    variants = [b"0" * w, b"9" * w, str(max(0, int(old) - 1)).zfill(w).encode()[:w],
                str(int(old) + 1).encode()[-w:].zfill(w), (b"1" + b"0" * (w - 1)) if w > 1 else b"7"]
    new = variants[k % len(variants)]
    return data[:s] + new + data[e:], {"old": old.decode(), "new": new.decode()}


def m_kids(ctx, rng, src, k):
    data = ctx.qdf(src)
    pn = pages_obj_num(data)
    s, e = obj_span(data, pn)
    body = data[s:e]
    if k % 2 == 0:
        new_body = body.replace(b"/Kids [", b"/Kids [ %d 0 R" % pn, 1)          # /Kids содержит сам узел
        note = "Pages.Kids включает сам Pages"
    else:
        # первая страница становится узлом /Pages с детьми — снова корневой /Pages
        m = re.search(rb"/Kids \[\s*(\d+) 0 R", body)
        first = int(m.group(1))
        fs, fe = obj_span(data, first)
        page = data[fs:fe]
        page2 = page.replace(b"/Type /Page", b"/Type /Pages /Kids [ %d 0 R ] /Count 1" % pn, 1)
        data = data[:fs] + page2 + data[fe:]
        s, e = obj_span(data, pn)
        new_body = data[s:e]
        note = "первая страница — узел /Pages с ребёнком «корень»"
    out = data[:s] + new_body + data[e:]
    return ctx.fix(out), {"pages_obj": pn, "note": note}


def m_prev(ctx, rng, src, k):
    data = ctx.fix(ctx.qdf(src))                     # классический xref без комментариев QDF
    i = data.rfind(b"startxref")
    old_xref = int(re.match(rb"startxref\s+(\d+)", data[i:]).group(1))
    tm = re.search(rb"trailer\s*<<(.*?)>>\s*startxref", data[old_xref:], re.S)
    tdict = tm.group(1)
    size = int(re.search(rb"/Size (\d+)", tdict).group(1))
    root = re.search(rb"/Root (\d+ \d+ R)", tdict).group(1)
    out = bytearray(data)
    if not out.endswith(b"\n"):
        out += b"\n"
    # новая версия объекта Info-подобного словаря
    obj_off = len(out)
    out += b"%d 0 obj\n<< /Producer (ottisk mutant) >>\nendobj\n" % size
    x1 = len(out)
    if k % 2 == 0:
        prev = x1                                     # /Prev указывает на эту же секцию
        out += b"xref\n%d 1\n%010d 00000 n \ntrailer\n<< /Size %d /Root %s /Prev %d >>\nstartxref\n%d\n%%%%EOF\n" % (
            size, obj_off, size + 1, root, prev, x1)
        note = "петля /Prev на себя"
    else:
        # две секции: вторая → первая → вторая
        out += b"xref\n%d 1\n%010d 00000 n \ntrailer\n<< /Size %d /Root %s /Prev %s >>\nstartxref\n%d\n%%%%EOF\n" % (
            size, obj_off, size + 1, root, b"XXXXXXXXXX", x1)
        x2 = len(out)
        out += b"xref\n%d 1\n%010d 00000 n \ntrailer\n<< /Size %d /Root %s /Prev %d >>\nstartxref\n%d\n%%%%EOF\n" % (
            size, obj_off, size + 1, root, x1, x2)
        out = bytes(out).replace(b"XXXXXXXXXX", b"%010d" % x2, 1)
        note = "цикл /Prev из двух секций"
    return bytes(out), {"note": note, "old_startxref": old_xref}


def m_bomb(ctx, rng, src, k):
    pdf = pikepdf.new()
    if k == 3:
        side = 20000
        raw = zlib.compress(b"\0" * (side * side // 8), 9)
        st = Stream(pdf, raw)
        st.Type = Name.XObject
        st.Subtype = Name.Image
        st.Width = side
        st.Height = side
        st.ColorSpace = Name.DeviceGray
        st.BitsPerComponent = 1
        st.Filter = Name.FlateDecode
        content = Stream(pdf, b"q 500 0 0 700 50 70 cm /Im1 Do Q\n")
        res = Dictionary(XObject=Dictionary(Im1=pdf.make_indirect(st)))
        note = "картинка 1 бит 20000×20000 (50 МБ после распаковки)"
        decoded = side * side // 8
    else:
        size = [16, 64, 64][k] << 20
        body = b" " * size
        if k == 2:
            raw = zlib.compress(zlib.compress(body, 9), 9)
            filt = Array([Name.FlateDecode, Name.FlateDecode])
            note = "двойной Flate, 64 МБ пробелов"
        else:
            raw = zlib.compress(body, 9)
            filt = Name.FlateDecode
            note = "Flate, %d МБ пробелов в содержимом" % (size >> 20)
        content = Stream(pdf, raw)
        content.Filter = filt
        res = Dictionary()
        decoded = size
    pdf.pages.append(pikepdf.Page(Dictionary(Type=Name.Page, MediaBox=Array([0, 0, 595, 842]), Resources=res,
                                             Contents=pdf.make_indirect(content))))
    buf = io.BytesIO()
    pdf.save(buf, deterministic_id=True, compress_streams=False, stream_decode_level=pikepdf.StreamDecodeLevel.none,
             object_stream_mode=pikepdf.ObjectStreamMode.disable)
    return buf.getvalue(), {"note": note, "decoded_bytes": decoded}


def m_bitflip(ctx, rng, src, k):
    data = bytearray(ctx.raw(src))
    n = [1, 2, 4, 8, 16, 32][k % 6]
    pos = []
    for _ in range(n):
        p = rng.randrange(len(data))
        data[p] ^= 1 << rng.randrange(8)
        pos.append(p)
    return bytes(data), {"flips": n, "positions": sorted(pos)}


def m_size(ctx, rng, src, k):
    data = ctx.fix(ctx.qdf(src))
    new = [b"0", b"1", b"2147483647", b"-5", b"99999999999999"][k % 5]
    i = data.rfind(b"trailer")
    m = re.search(rb"/Size (\d+)", data[i:])
    out = data[:i + m.start(1)] + new + data[i + m.end(1):]
    return out, {"new_size": new.decode()}


def m_count(ctx, rng, src, k):
    data = ctx.qdf(src)
    pn = pages_obj_num(data)
    s, e = obj_span(data, pn)
    body = data[s:e]
    new = [b"0", b"-1", b"1000000000", b"/Many", b"3.5"][k % 5]
    body2 = re.sub(rb"/Count \d+", b"/Count " + new, body, count=1)
    return ctx.fix(data[:s] + body2 + data[e:]), {"new_count": new.decode()}


def m_nesting(ctx, rng, src, k):
    data = ctx.qdf(src)
    depth = [10000, 30000, 100000, 50000][k % 4]
    inner = (b"[" * depth + b"]" * depth) if k % 2 == 0 else (b"<</A " * depth + b"1" + b">>" * depth)
    m = re.search(rb"/Type /Catalog", data)
    out = data[:m.end()] + b"\n  /OttiskDeep " + inner + data[m.end():]
    return ctx.fix(out), {"depth": depth, "kind": "array" if k % 2 == 0 else "dict"}


def m_xref(ctx, rng, src, k):
    data = ctx.fix(ctx.qdf(src))
    i = data.rfind(b"\nxref")
    j = data.find(b"trailer", i)
    table = bytearray(data[i:j])
    kind = k % 3
    if kind == 0:
        for _ in range(8):
            p = rng.randrange(6, len(table))
            table[p] = rng.choice(b"#@!xyz")
        note = "мусор в таблице xref"
    elif kind == 1:
        def shift(m):
            return b"%010d" % (int(m.group(1)) + 3) + m.group(2)
        table = bytearray(re.sub(rb"(\d{10})( \d{5} n)", shift, bytes(table)))
        note = "все смещения сдвинуты на +3"
    else:
        table = bytearray(re.sub(rb"\nxref\n0 (\d+)", lambda m: b"\nxref\n0 %d" % (int(m.group(1)) + 50),
                                 bytes(table), count=1))
        note = "подраздел xref длиннее таблицы"
    return data[:i] + bytes(table) + data[j:], {"note": note}


KINDS = [("trunc", 15, m_trunc, SOURCES), ("startxref", 10, m_startxref, SOURCES),
         ("length", 15, m_length, STRUCT_SOURCES), ("kids", 8, m_kids, STRUCT_SOURCES),
         ("prev", 8, m_prev, STRUCT_SOURCES), ("bomb", 4, m_bomb, ["(synthetic)"]),
         ("bitflip", 20, m_bitflip, SOURCES), ("size", 5, m_size, STRUCT_SOURCES),
         ("count", 5, m_count, STRUCT_SOURCES), ("nesting", 4, m_nesting, STRUCT_SOURCES),
         ("xref", 6, m_xref, STRUCT_SOURCES)]


def main():
    args = g.common_args(__doc__, corpus=True)
    root = Path(args.out)
    dst = Path(args.corpus) / "mutants"
    if dst.exists():
        shutil.rmtree(dst)                             # каталог целиком наш — пересоздаём
    dst.mkdir(parents=True)
    tmp = g.tmp_dir(args, "mutants")
    ctx = Ctx(root, tmp)
    rng = random.Random(g.SEED)
    index = []
    n = 0
    for kind, count, fn, pool in KINDS:
        for k in range(count):
            src = pool[rng.randrange(len(pool))] if pool[0] != "(synthetic)" else "(synthetic)"
            data, params = fn(ctx, rng, src, k)
            n += 1
            stem = Path(src).stem if src != "(synthetic)" else "synthetic"
            name = "m%03d-%s-%s.pdf" % (n, kind, stem)
            (dst / name).write_bytes(data)
            index.append({"file": name, "kind": kind, "source": src, "params": params, "bytes": len(data),
                          "expect": EXPECT})
    if n != 100:
        raise RuntimeError("мутантов %d, а не 100" % n)
    g.write_json(dst / "index.json", {"generator": "tools/gen/make_mutants.py", "seed": g.SEED, "count": n,
                                      "mutants": index})
    total = sum(x["bytes"] for x in index)
    g.say("mutants: %d файлов, %d байт → %s" % (n, total, dst))


if __name__ == "__main__":
    main()
