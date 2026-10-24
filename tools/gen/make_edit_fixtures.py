# -*- coding: utf-8 -*-
"""make_edit_fixtures.py — фикстуры редактора текста (сессия 3.1) в tests/fixtures/edit/.

Что (свои PDF с точной «истиной»: генератор сам считает, где стоит каждый глиф — как ISO 32000-1 9.4.4 и
MuPDF pdf_show_char/pdf_show_space):
  latin.pdf         — три абзаца латиницы, Type0/CIDFontType2 Identity-H (Arial), каждая строка — свой BT…ET
                      (абзац из нескольких текстовых объектов);
  cyr-cid.pdf       — два абзаца кириллицы (Times New Roman CID) одним BT: TL, T*, «'», отступ первой строки
                      без интервала между абзацами;
  type1-diff.pdf    — Type1 /Helvetica без встраивания: WinAnsi + /Differences (кириллица на кодах cp1251),
                      /Widths по метрике Arial (= Helvetica), без /ToUnicode;
  base14.pdf        — Type1 /Times-Roman base14 без встраивания и без /Widths — MuPDF writer (mutool create);
  tj-kerning.pdf    — TJ с кернингом пар и большим пробелом-отступом внутри массива, литеральные строки с
                      экранированием;
  tz-tc-tw.pdf      — простой TrueType (1 байт на код): Tc, Tw, Tz, Ts (индекс в той же строке), Tw у Type0
                      (2 байта — Tw не действует), Tr 1 и Tr 7;
  rotated.pdf       — абзац, строка под 90° и строка под 30° (Tm), страница без поворота;
  two-streams.pdf   — /Contents из двух потоков: цвет и BT в первом, операнды Td — в конце первого, сам Td и
                      строка — во втором;
  xobject-form.pdf  — абзац страницы и текст в XObject-форме (/Matrix, cm перед Do) — «только чтение»;
  columns-2.pdf     — две колонки по два абзаца, колонка за колонкой;
  columns-3.pdf     — три колонки по одному абзацу, строки в потоке идут рядами (1-я строка каждой колонки…);
  align-left.pdf, align-right.pdf, align-center.pdf, align-justify.pdf — по два абзаца с выравниванием
                      (по ширине: Tw у простого шрифта и TJ-пробелы у Type0; последняя строка — влево);
  inline-image.pdf  — комментарии, \\r\\n, BI…ID…EI (в данных есть байты « EI »), /AHx, BX/EX с незнакомым
                      оператором, BDC со словарём MCID, литеральная строка с \\( \\) \\\\ и \\101;
  expected.json     — ожидания: спаны (текст, шрифт, кегль, x0/x1/базовая линия или начало и угол, цвет,
                      «только чтение»), блоки (текст, x0/x1, первая и последняя базовая линия, число строк,
                      выравнивание, «только чтение»). Файлы Word (make_edit_fixtures.ps1) получают ожидания
                      абзацев отсюда же (--word-dir): текст и выравнивание — из задания Word, x0/x1 — по
                      рамкам символов pypdfium2 (независимый источник).
Координаты — пункты страницы MuPDF (начало в левом верхнем углу, y вниз).
Откуда: свои тексты; шрифты Arial, Times New Roman из C:\\Windows\\Fonts; mutool create (base14.pdf).
Детерминизм: даты/XMP/ID фиксированы (_genlib.save_pdf), случайности нет; base14.pdf пересохраняется pikepdf.
Запуск: "C:\\Tools\\venv-test\\Scripts\\python.exe" -X utf8 -I tools/gen/make_edit_fixtures.py [--out <корень>]
        [--word-spec <файл.json>] — только записать задание для Word и выйти;
        [--word-dir <каталог>] — где лежат word-*.pdf (по умолчанию tests/fixtures/edit).
"""
import json
import math
import sys
from pathlib import Path

sys.dont_write_bytecode = True                     # без __pycache__ в tools/gen
sys.path.insert(0, str(Path(__file__).resolve().parent))

import pikepdf  # noqa: E402
from pikepdf import Array, Dictionary, Name, Stream  # noqa: E402

import _genlib as g  # noqa: E402

LIMIT = 50 * 1024
W, H = g.A4
IDENT = (1.0, 0.0, 0.0, 1.0, 0.0, 0.0)

LATIN = [
    "The editor builds a model of every text object on the page: glyphs with positions, fonts and the "
    "text matrix. Lines are clustered by baseline and angle, then grouped into paragraphs.",
    "A paragraph is a block with a bounding rectangle and style runs. Text shown by several BT and ET "
    "pairs that belong to one paragraph is merged into a single block.",
    "Untouched operators are written back byte for byte, so an empty edit leaves the page unchanged.",
]
CYR = [
    "Редактор строит модель каждого текстового объекта страницы: глифы с позициями, шрифтами и "
    "матрицей текста. Строки собираются по базовой линии, а затем — в абзацы.",
    "Новый абзац начинается с отступа первой строки, даже если интервала между абзацами нет. Съешь же "
    "ещё этих мягких французских булок, да выпей чаю.",
]
MIXED = [
    "Выравнивание по левому краю: правые концы строк неровные, а левые совпадают. Mixed text: Привет, "
    "world! Всё на своих местах.",
    "Второй абзац того же блока проверяет, что интервал больше полутора кеглей отделяет абзацы друг от "
    "друга, а строки внутри абзаца — нет.",
]
JUSTIFY_LATIN = ("Justified text stretches every line except the last one to the full width of the column, "
                 "so both edges are straight. Word spacing comes from the Tw operator in this paragraph.")
JUSTIFY_CYR = ("Во втором абзаце ширина набирается сдвигами внутри массива TJ после каждого пробела: так "
               "делают многие программы вёрстки, когда шрифт двухбайтовый и оператор Tw не действует.")


# --- матрицы (как fz_matrix: [a b c d e f], строка-вектор) ---------------------------------------------

def mul(m, n):
    a, b, c, d, e, f = m
    A, B, C, D, E, F = n
    return (a * A + b * C, a * B + b * D, c * A + d * C, c * B + d * D, e * A + f * C + E, e * B + f * D + F)


def pre_translate(m, tx, ty):
    return mul((1.0, 0.0, 0.0, 1.0, tx, ty), m)


def apply(m, x, y):
    a, b, c, d, e, f = m
    return (x * a + y * c + e, x * b + y * d + f)


def lit(data: bytes) -> str:
    """Литеральная строка PDF: ( ) \\ экранируются, непечатные — восьмеричными."""
    out = []
    for b in data:
        ch = chr(b)
        if ch in "()\\":
            out.append("\\" + ch)
        elif 32 <= b < 127:
            out.append(ch)
        else:
            out.append("\\%03o" % b)
    return "(" + "".join(out) + ")"


def hexstr(color) -> str:
    return "#%02x%02x%02x" % tuple(round(v * 255) for v in color)


# --- шрифты ---------------------------------------------------------------------------------------------

class CidFont:
    """Type0 Identity-H (_genlib.PdfFont): 2 байта на код; ширины /W — округлённые до 1/1000 em."""

    def __init__(self, doc, name):
        self.f = doc.font(name)
        self.ref = self.f.ref
        self.single = False
        self.ps = self.f.tt.ps_name

    def code(self, ch):
        return self.f.encode(ch)

    def w1000(self, ch):
        tt = self.f.tt
        return round(tt.advance(tt.gid(ch)) * 1000.0 / tt.units)

    def width(self, text, size):
        return sum(self.w1000(ch) for ch in text) * size / 1000.0


class SimpleFont:
    """Простой TrueType (_genlib.SimpleTrueTypeFont): WinAnsi + /Differences cp1251, 1 байт на код."""

    def __init__(self, doc, name):
        self.f = doc.simple_font(name)
        self.ref = self.f.ref
        self.single = True
        self.ps = self.f.tt.ps_name

    def code(self, ch):
        return self.f.encode(ch)

    def w1000(self, ch):
        tt = self.f.tt
        return round(tt.advance(tt.gid(ch)) * 1000.0 / tt.units)

    def width(self, text, size):
        return sum(self.w1000(ch) for ch in text) * size / 1000.0


class Type1Diff:
    """Type1 base14 без встраивания с /Differences (кириллица — коды cp1251, имена afii) и /Widths по
    метрике совместимого TrueType (Arial = Helvetica, Times New Roman = Times-Roman)."""

    def __init__(self, doc, base="Helvetica", metrics="arial"):
        self.pdf = doc.pdf
        self.base = base
        self.ps = base
        self.tt = g.TrueType(g.FONTS_DIR / g.FONT_FILES[metrics])
        self.ref = self.pdf.make_indirect(Dictionary(Type=Name.Font))
        self.used = {}
        self.single = True
        doc.extra_fonts.append(self)

    def code(self, ch):
        b = ch.encode("ascii") if ord(ch) < 128 else ch.encode("cp1251")
        self.used[b[0]] = ch
        return b

    def w1000(self, ch):
        return round(self.tt.advance(self.tt.gid(ch)) * 1000.0 / self.tt.units)

    def width(self, text, size):
        return sum(self.w1000(ch) for ch in text) * size / 1000.0

    def finish(self):
        codes = sorted(self.used)
        first, last = codes[0], codes[-1]
        widths = [self.w1000(self.used[c]) if c in self.used else 0 for c in range(first, last + 1)]
        diffs = Array()
        prev = None
        for c in codes:
            if c >= 0x80:
                if prev is None or c != prev + 1:
                    diffs.append(c)
                diffs.append(Name("/" + g.agl_name(self.used[c])))
                prev = c
        self.ref.Subtype = Name.Type1
        self.ref.BaseFont = Name("/" + self.base)
        self.ref.FirstChar = first
        self.ref.LastChar = last
        self.ref.Widths = Array(widths)
        self.ref.Encoding = Dictionary(Type=Name.Encoding, BaseEncoding=Name.WinAnsiEncoding, Differences=diffs)


class EditDoc(g.Doc):
    def __init__(self, title, subject=""):
        super().__init__(title, subject, creator="tools/gen/make_edit_fixtures.py")
        self.extra_fonts = []

    def finish_fonts(self):
        super().finish_fonts()
        for f in self.extra_fonts:
            if f.used:
                f.finish()


# --- текстовые операторы с расчётом геометрии ------------------------------------------------------------

class Text:
    """Пишет операторы текста в список ops и записывает спаны (один на оператор показа) с геометрией в
    координатах страницы MuPDF. ctm — матрица пользователя (cm, /Matrix формы)."""

    def __init__(self, cv, fx, ctm=IDENT, page_h=H, read_only=""):
        self.cv = cv
        self.fx = fx
        self.ops = []
        self.ctm = ctm
        self.h = page_h
        self.read_only = read_only
        self.font = None
        self.size = 0.0
        self.tc = self.tw = self.ts = 0.0
        self.th = 1.0
        self.tl = 0.0
        self.tr = 0
        self.tm = self.tlm = IDENT
        self.fill = (0.0, 0.0, 0.0)

    # состояние
    def raw(self, s):
        self.ops.append(s)

    def bt(self):
        self.tm = self.tlm = IDENT
        self.ops.append("BT")

    def et(self):
        self.ops.append("ET")

    def rg(self, r, gg, b):
        self.fill = (r, gg, b)
        self.ops.append("%s %s %s rg" % (g._num(r), g._num(gg), g._num(b)))

    def tf(self, font, size, key=None):
        self.font, self.size = font, float(size)
        k = key or self.cv._fkey(font)
        self.ops.append("/%s %s Tf" % (k, g._num(size)))

    def set(self, op, v):
        if op == "Tc":
            self.tc = v
        elif op == "Tw":
            self.tw = v
        elif op == "Tz":
            self.th = v / 100.0
        elif op == "Ts":
            self.ts = v
        elif op == "TL":
            self.tl = v
        elif op == "Tr":
            self.tr = int(v)
        self.ops.append("%s %s" % (g._num(v), op))

    def td(self, x, y, op="Td"):
        if op == "TD":
            self.tl = -y
        self.tlm = pre_translate(self.tlm, x, y)
        self.tm = self.tlm
        self.ops.append("%s %s %s" % (g._num(x), g._num(y), op))

    def set_tm(self, *m):
        self.tm = self.tlm = tuple(float(v) for v in m)
        self.ops.append("%s Tm" % " ".join(g._num(v) for v in m))

    def tstar(self):
        self.tlm = pre_translate(self.tlm, 0, -self.tl)
        self.tm = self.tlm
        self.ops.append("T*")

    # показ
    def _str(self, data: bytes, literal: bool) -> str:
        return lit(data) if literal else g._pdfstr(data)

    def _show(self, items):
        """items — строки и числа (как массив TJ); возвращает запись спана и байты операндов."""
        f = self.font
        glyphs = []
        texts = []
        enc = []
        for it in items:
            if isinstance(it, str):
                data = b""
                for ch in it:
                    code = f.code(ch)
                    data += code
                    w0 = f.w1000(ch) / 1000.0
                    trm = mul((self.size * self.th, 0.0, 0.0, self.size, 0.0, self.ts), mul(self.tm, self.ctm))
                    glyphs.append((ch, trm, w0))
                    tx = (w0 * self.size + self.tc) * self.th
                    if len(code) == 1 and code[0] == 32:
                        tx += self.tw * self.th
                    self.tm = pre_translate(self.tm, tx, 0.0)
                texts.append(it)
                enc.append(("s", data))
            else:
                self.tm = pre_translate(self.tm, -float(it) / 1000.0 * self.size * self.th, 0.0)
                enc.append(("n", it))
        first = glyphs[0][1]
        last_ch, last, w0 = glyphs[-1]
        x0, y0 = first[4], first[5]
        x1, y1 = last[4] + w0 * last[0], last[5] + w0 * last[1]
        angle = math.degrees(math.atan2(first[1], first[0]))
        span = {"text": "".join(texts), "font": f.ps, "size": round(self.size, 3), "color": hexstr(self.fill)}
        if abs(angle) < 0.01:
            span.update({"x0": round(x0, 3), "x1": round(x1, 3), "baseline": round(self.h - y0, 3)})
        else:
            span.update({"origin": [round(x0, 3), round(self.h - y0, 3)], "angle": round(angle, 3)})
        span["readOnly"] = self.read_only
        self.fx.spans.append(span)
        return span, enc

    def tj(self, text, literal=False):
        _, enc = self._show([text])
        self.ops.append("%s Tj" % self._str(enc[0][1], literal))
        return len(self.fx.spans) - 1

    def tj_array(self, items, literal=False):
        _, enc = self._show(items)
        parts = [self._str(v, literal) if k == "s" else g._num(v) for k, v in enc]
        self.ops.append("[%s] TJ" % " ".join(parts))
        return len(self.fx.spans) - 1

    def quote(self, text, literal=False):
        self.tlm = pre_translate(self.tlm, 0, -self.tl)
        self.tm = self.tlm
        _, enc = self._show([text])
        self.ops.append("%s '" % self._str(enc[0][1], literal))
        return len(self.fx.spans) - 1

    def dquote(self, aw, ac, text, literal=False):
        self.tw, self.tc = aw, ac
        self.tlm = pre_translate(self.tlm, 0, -self.tl)
        self.tm = self.tlm
        _, enc = self._show([text])
        self.ops.append('%s %s %s "' % (g._num(aw), g._num(ac), self._str(enc[0][1], literal)))
        return len(self.fx.spans) - 1


class Fixture:
    def __init__(self, name, about, kinds):
        self.name = name
        self.about = about
        self.kinds = kinds
        self.spans = []
        self.blocks = []

    def block(self, spans, align, read_only="", text=None):
        """Абзац из спанов (индексы); строки — по базовой линии без Ts (по baseline спанов без подъёма)."""
        recs = [self.spans[i] for i in spans]
        lines = []
        for r in recs:
            base = r.get("lineBaseline", r.get("baseline"))
            if not lines or abs(lines[-1] - base) > 0.5:
                lines.append(base)
        b = {"text": text if text is not None else join_lines(recs), "align": align, "readOnly": read_only,
             "lines": len(lines), "spans": list(spans)}
        if "x0" in recs[0]:
            b.update({"x0": round(min(r["x0"] for r in recs), 3), "x1": round(max(r["x1"] for r in recs), 3),
                      "firstBaseline": lines[0], "lastBaseline": lines[-1]})
        self.blocks.append(b)

    def to_json(self):
        return {"about": self.about, "kinds": self.kinds, "source": "tools/gen/make_edit_fixtures.py",
                "pages": 1, "spans": [dict(s, page=0) for s in self.spans],
                "blocks": [dict(b, page=0) for b in self.blocks]}


def join_lines(recs):
    """Текст абзаца: спаны одной строки — подряд, строки — через пробел (как TextBlock::text)."""
    out = ""
    prev = None
    for r in recs:
        base = r.get("lineBaseline", r.get("baseline"))
        if prev is not None and abs(prev - base) > 0.5 and not out.endswith(" "):
            out += " "
        out += r["text"]
        prev = base
    return out.strip()


def lay_lines(text, font, size, width, indent=0.0):
    """Перенос по словам: [(строка, ширина)], первая строка короче на indent."""
    words = text.split()
    lines, cur = [], ""
    avail = width - indent
    for w in words:
        cand = (cur + " " + w) if cur else w
        if font.width(cand, size) <= avail or not cur:
            cur = cand
        else:
            lines.append(cur)
            cur = w
            avail = width
    if cur:
        lines.append(cur)
    return [(ln, font.width(ln, size)) for ln in lines]


def paragraph(t, font, size, text, left, top, width, leading, align="left", indent=0.0, mode="bt-per-line",
              key=None):
    """Абзац текстом t; возвращает (индексы спанов, базовая линия после абзаца). top — базовая линия первой
    строки (PDF y вверх). mode: bt-per-line — каждая строка своим BT…ET; single-bt — один BT с TL/T*/'."""
    lines = lay_lines(text, font, size, width, indent)
    spans = []
    y = top
    lastx = left
    if mode == "single-bt":
        t.bt()
        t.tf(font, size, key)
        t.set("TL", leading)
    for i, (ln, w) in enumerate(lines):
        last = i == len(lines) - 1
        x = left + (indent if i == 0 else 0.0)
        extra = 0.0
        if align == "right":
            x = left + width - w
        elif align == "center":
            x = left + (width - w) / 2.0
        elif align == "justify" and not last:
            extra = width - (indent if i == 0 else 0.0) - w
        nsp = ln.count(" ")
        via_quote = False  # строка показывается «'» или «"» (они сами делают T*)
        if mode == "bt-per-line":
            t.bt()
            t.tf(font, size, key)
            t.td(x, y)
        elif i == 0:
            t.td(x, y)
        elif abs(x - lastx) > 1e-6:
            t.td(x - lastx, -leading)
        elif i % 3 == 2:
            via_quote = True
        else:
            t.tstar()
        lastx = x
        if align == "justify" and font.single:
            # Tw округлён до 4 знаков — геометрия считается от записанного числа
            tw = round(extra / nsp, 4) if (nsp and not last) else 0.0
            if via_quote:
                spans.append(t.dquote(tw, t.tc, ln))
            else:
                t.set("Tw", tw)
                spans.append(t.tj(ln))
        elif align == "justify" and not last and nsp:
            if via_quote:
                t.tstar()
            adj = -round(extra / nsp * 1000.0 / size, 2)
            items = []
            for k, word in enumerate(ln.split(" ")):
                if k:
                    items.append(" ")
                    items.append(adj)
                items.append(word)
            merged = []
            for it in items:
                if isinstance(it, str) and merged and isinstance(merged[-1], str):
                    merged[-1] += it
                else:
                    merged.append(it)
            spans.append(t.tj_array(merged))
        else:
            spans.append(t.quote(ln) if via_quote else t.tj(ln))
        if mode == "bt-per-line":
            t.et()
        y -= leading
    if mode == "single-bt":
        if align == "justify" and font.single:
            t.set("Tw", 0)
        t.et()
    return spans, y


# --- фикстуры ----------------------------------------------------------------------------------------------

def save(doc, cv, dst, name, fx, contents=None, extra=None):
    page = doc.add_page(cv, extra=extra)
    if contents is not None:
        page.obj.Contents = contents
    doc.save(dst / name)
    g.check_size(dst / name, LIMIT)
    return fx


def make_latin(dst):
    fx = Fixture("latin.pdf", "три абзаца латиницы, Type0 Arial, каждая строка — свой BT…ET", ["latin", "cid"])
    doc = EditDoc("Edit fixture: latin paragraphs")
    cv = doc.canvas()
    t = Text(cv, fx)
    font = CidFont(doc, "arial")
    y = H - 90
    for text in LATIN:
        spans, y = paragraph(t, font, 11, text, 72, y, 450, 14.5)
        fx.block(spans, "left")
        y -= 14.5
    cv.ops.extend(t.ops)
    return save(doc, cv, dst, fx.name, fx)


def make_cyr_cid(dst):
    fx = Fixture("cyr-cid.pdf", "два абзаца кириллицы одним BT (TL, T*, '), отступ первой строки без интервала",
                 ["cyrillic", "cid"])
    doc = EditDoc("Кириллица CID: абзацы одним текстовым объектом")
    cv = doc.canvas()
    t = Text(cv, fx)
    font = CidFont(doc, "times")
    size, lead = 12, 15.0
    t.bt()
    t.tf(font, size)
    t.set("TL", lead)
    y = H - 100
    first = True
    for text in CYR:
        lines = lay_lines(text, font, size, 440, indent=24)
        spans = []
        for i, (ln, _w) in enumerate(lines):
            if first:
                t.td(72 + 24, y)
                first = False
            elif i == 0:
                t.td(24, -lead)
            elif i == 1:
                t.td(-24, -lead)
            elif i % 2 == 0:
                spans.append(t.quote(ln))
                continue
            else:
                t.tstar()
            spans.append(t.tj(ln))
        fx.block(spans, "left")
    t.et()
    cv.ops.extend(t.ops)
    return save(doc, cv, dst, fx.name, fx)


def make_type1_diff(dst):
    fx = Fixture("type1-diff.pdf", "Type1 /Helvetica без встраивания, WinAnsi + /Differences (кириллица cp1251)",
                 ["type1", "differences", "cyrillic", "non-embedded"])
    doc = EditDoc("Type1 с /Differences")
    cv = doc.canvas()
    t = Text(cv, fx)
    font = Type1Diff(doc)
    spans, _ = paragraph(t, font, 12, "Type1 Helvetica with Differences: Привет, мир! Съешь ещё этих "
                         "мягких булок. Текст без ToUnicode извлекается по именам глифов afii.", 72, H - 100,
                         440, 15, mode="single-bt")
    fx.block(spans, "left")
    cv.ops.extend(t.ops)
    return save(doc, cv, dst, fx.name, fx)


def make_base14(dst, tmp):
    """mutool create: /Times-Roman base14 без встраивания и без /Widths; затем pikepdf — метаданные и /ID."""
    fx = Fixture("base14.pdf", "Type1 /Times-Roman base14 без встраивания и без /Widths (mutool create)",
                 ["type1", "base14", "non-embedded", "no-widths"])
    doc = EditDoc("tmp")
    cv = doc.canvas()
    t = Text(cv, fx, page_h=842.0)               # mutool create округляет /MediaBox до целых
    font = Type1Diff(doc, "Times-Roman", "times")  # только метрика: шрифт в файле пишет mutool
    y = 842.0 - 100
    texts = ["Base fourteen fonts are not embedded: the viewer substitutes a metric compatible face. "
             "Widths come from the font program, not from the file.",
             "A second paragraph after an empty line checks the paragraph distance rule."]
    for text in texts:
        spans, y = paragraph(t, font, 12, text, 72, y, 440, 15, key="F1")
        fx.block(spans, "left")
        y -= 15
    src = tmp / "base14.txt"
    src.write_bytes(b"%%MediaBox 0 0 595 842\n%%Font F1 Times-Roman\n"
                    + ("\n".join(t.ops) + "\n").encode("latin-1"))
    raw = tmp / "base14.raw.pdf"
    g.run([g.mutool_path(), "create", "-o", str(raw), str(src)])
    pdf = pikepdf.open(raw)
    g.save_pdf(pdf, dst / fx.name, title="Type1 base14 без встраивания", subject="tests/fixtures/edit/base14.pdf",
               creator="tools/gen/make_edit_fixtures.py (mutool create)")
    g.check_size(dst / fx.name, LIMIT)
    return fx


def make_tj_kerning(dst):
    fx = Fixture("tj-kerning.pdf", "TJ: кернинг пар, отступ внутри массива, литеральные строки", ["tj", "kerning"])
    doc = EditDoc("TJ-кернинг")
    cv = doc.canvas()
    t = Text(cv, fx)
    font = CidFont(doc, "arial")
    simple = SimpleFont(doc, "arial")
    spans = []
    t.bt()
    t.tf(font, 14)
    t.td(72, H - 100)
    spans.append(t.tj_array(["A", 70, "V", 80, "A", 60, "T", 40, "A", 30, "R ", "W", 60, "A", 50, "V", 40, "E"]))
    t.td(0, -18)
    spans.append(t.tj_array(["T", 70, "ypography: ", "k", 15, "e", 20, "r", 10, "ning ", "\u0414", 30, "\u0435",
                             "\u0434 \u0410", 40, "\u0423"]))
    t.et()
    fx.block(spans, "left")
    t.bt()
    t.tf(simple, 12)
    t.td(72, H - 160)
    s1 = t.tj_array(["Name:", -2400, "Value (with parens) and back\\slash"], literal=True)
    t.et()
    fx.block([s1], "left")
    cv.ops.extend(t.ops)
    return save(doc, cv, dst, fx.name, fx)


def make_tz_tc_tw(dst):
    fx = Fixture("tz-tc-tw.pdf", "Tc, Tw, Tz, Ts, Tw у двухбайтового шрифта, Tr 1 и Tr 7", ["tc", "tw", "tz", "ts"])
    doc = EditDoc("Tz, Tc, Tw, Ts")
    cv = doc.canvas()
    t = Text(cv, fx)
    simple = SimpleFont(doc, "arial")
    cid = CidFont(doc, "arial")
    y = H - 100

    def line(setup, text, font=simple, size=12):
        nonlocal y
        t.bt()
        t.tf(font, size)
        for op, v in setup:
            t.set(op, v)
        t.td(72, y)
        s = t.tj(text)
        t.et()
        y -= 30
        return s

    fx.block([line([("Tc", 1.5)], "Letter spacing with Tc 1.5")], "left")
    fx.block([line([("Tc", 0), ("Tw", 6)], "Word spacing applies to every space")], "left")
    fx.block([line([("Tw", 0), ("Tz", 75)], "Horizontal scale Tz 75 percent")], "left")
    t.bt()
    t.tf(simple, 12)
    t.set("Tz", 100)
    t.td(72, y)
    a = t.tj("E = mc")
    t.set("Ts", 5)
    t.tf(simple, 8)
    b = t.tj("2")
    t.set("Ts", 0)
    t.tf(simple, 12)
    c = t.tj(" and H")
    t.set("Ts", -3)
    t.tf(simple, 8)
    d = t.tj("2")
    t.set("Ts", 0)
    t.tf(simple, 12)
    e = t.tj("O in one line")
    t.et()
    base = round(H - y, 3)
    for i in (a, b, c, d, e):
        fx.spans[i]["lineBaseline"] = base
    fx.block([a, b, c, d, e], "left")
    y -= 30
    fx.block([line([("Tw", 10)], "Tw does not apply to two byte codes", font=cid)], "left")
    t.raw("0 0 1 RG 0.5 w")
    fx.block([line([("Tr", 1)], "Outline text with Tr 1")], "left")
    s7 = line([("Tr", 7)], "Clip only text with Tr 7")
    fx.spans[s7]["readOnly"] = "clip"
    fx.block([s7], "left", read_only="clip")
    t.raw("0 0 m 0 0 l n")
    cv.ops.extend(t.ops)
    return save(doc, cv, dst, fx.name, fx)


def make_rotated(dst):
    fx = Fixture("rotated.pdf", "абзац, строка под 90° и строка под 30° (Tm)", ["rotated"])
    doc = EditDoc("Повёрнутый текст")
    cv = doc.canvas()
    t = Text(cv, fx)
    font = CidFont(doc, "arial")
    spans, _ = paragraph(t, font, 11, "Horizontal text stays editable, rotated lines become read only blocks.",
                         72, H - 100, 440, 14)
    fx.block(spans, "left")
    t.bt()
    t.tf(font, 12)
    t.set_tm(0, 1, -1, 0, 60, 300)
    s90 = t.tj("Vertical line rotated by 90 degrees")
    t.et()
    t.bt()
    t.tf(font, 12)
    a = math.radians(30)
    t.set_tm(round(math.cos(a), 6), round(math.sin(a), 6), round(-math.sin(a), 6), round(math.cos(a), 6), 200, 400)
    s30 = t.tj("Line at thirty degrees")
    t.et()
    for s in (s90, s30):
        fx.spans[s]["readOnly"] = "rotated"
        fx.block([s], "left", read_only="rotated")
    cv.ops.extend(t.ops)
    return save(doc, cv, dst, fx.name, fx)


def make_two_streams(dst):
    fx = Fixture("two-streams.pdf", "/Contents из двух потоков; операнды Td — в конце первого, Td — во втором",
                 ["contents-array"])
    doc = EditDoc("Два потока /Contents")
    cv = doc.canvas()
    t = Text(cv, fx)
    font = CidFont(doc, "arial")
    t.raw("q")
    t.rg(0, 0, 0.6)
    t.bt()
    t.tf(font, 12)
    t.td(72, H - 100)
    a = t.tj("Line one is shown by the first content stream")
    t.td(0, -16)
    b = t.tj("and the second line too, in the same text object;")
    t.et()
    t.bt()
    t.tf(font, 12)
    t.td(72, H - 132)
    td_op = t.ops.pop()                      # «72 y Td»: операнды в первом потоке, оператор — во втором
    nums = td_op.rsplit(" ", 1)[0]
    first = t.ops + [nums]
    t.ops = ["Td"]
    c = t.tj("the third line starts in stream two.")
    t.et()
    t.raw("Q")
    fx.block([a, b, c], "left")
    s1 = Stream(doc.pdf, ("\n".join(first) + "\n").encode("latin-1"))
    s2 = Stream(doc.pdf, ("\n".join(t.ops) + "\n").encode("latin-1"))
    cv.ops = []
    return save(doc, cv, dst, fx.name, fx,
                contents=Array([doc.pdf.make_indirect(s1), doc.pdf.make_indirect(s2)]))


def make_xobject_form(dst):
    fx = Fixture("xobject-form.pdf", "абзац страницы и текст XObject-формы (/Matrix, cm) — только чтение", ["form"])
    doc = EditDoc("XObject-форма")
    cv = doc.canvas()
    t = Text(cv, fx)
    font = CidFont(doc, "arial")
    spans, _ = paragraph(t, font, 11, "Page text before the form. Text inside a form XObject is read only: "
                         "its stream may be shared by other pages.", 72, H - 100, 440, 14)
    fx.block(spans, "left")
    cv.ops.extend(t.ops)
    cm = (1.0, 0.0, 0.0, 1.0, 72.0, 500.0)
    matrix = (0.8, 0.0, 0.0, 0.8, 10.0, 20.0)
    ft = Text(cv, fx, ctm=mul(matrix, cm), read_only="form")
    ft.rg(0.6, 0, 0)
    ft.bt()
    ft.tf(font, 14, key="F1")
    ft.td(0, 30)
    a = ft.tj("Text inside a form XObject")
    ft.td(0, -18)
    b = ft.tj("scaled by its /Matrix")
    ft.et()
    fx.block([a, b], "left", read_only="form")
    form = Stream(doc.pdf, ("\n".join(ft.ops) + "\n").encode("latin-1"))
    form.Type = Name.XObject
    form.Subtype = Name.Form
    form.BBox = Array([0, 0, 400, 80])
    form.Matrix = Array(list(matrix))
    form.Resources = Dictionary(Font=Dictionary(F1=font.ref))
    xo = doc.pdf.make_indirect(form)
    cv.xobjects["Fm1"] = xo
    cv.raw("q %s cm /Fm1 Do Q" % " ".join(g._num(v) for v in cm))
    return save(doc, cv, dst, fx.name, fx)


def make_columns2(dst):
    fx = Fixture("columns-2.pdf", "две колонки по два абзаца, колонка за колонкой", ["columns"])
    doc = EditDoc("Две колонки")
    cv = doc.canvas()
    t = Text(cv, fx)
    font = CidFont(doc, "arial")
    texts = [LATIN[0], LATIN[1], CYR[0], CYR[1]]
    for col in range(2):
        y = H - 90
        for k in range(2):
            spans, y = paragraph(t, font, 10, texts[col * 2 + k], 60 + col * 250, y, 220, 13)
            fx.block(spans, "left")
            y -= 13
    cv.ops.extend(t.ops)
    return save(doc, cv, dst, fx.name, fx)


def make_columns3(dst):
    fx = Fixture("columns-3.pdf", "три колонки по абзацу, строки в потоке — рядами через колонки", ["columns"])
    doc = EditDoc("Три колонки")
    cv = doc.canvas()
    t = Text(cv, fx)
    font = CidFont(doc, "arial")
    texts = [LATIN[0] + " " + LATIN[2], CYR[0], MIXED[1]]
    cols = [lay_lines(tx, font, 10, 150) for tx in texts]
    per = [[] for _ in cols]
    rows = max(len(c) for c in cols)
    for r in range(rows):
        for ci, col in enumerate(cols):
            if r < len(col):
                t.bt()
                t.tf(font, 10)
                t.td(50 + ci * 175, H - 90 - r * 13)
                per[ci].append(t.tj(col[r][0]))
                t.et()
    for spans in per:
        fx.block(spans, "left")
    cv.ops.extend(t.ops)
    return save(doc, cv, dst, fx.name, fx)


def make_align(dst, align):
    fx = Fixture("align-%s.pdf" % align, "два абзаца, выравнивание %s" % align, ["align", align])
    doc = EditDoc("Выравнивание: %s" % align)
    cv = doc.canvas()
    t = Text(cv, fx)
    if align == "justify":
        simple = SimpleFont(doc, "arial")
        cid = CidFont(doc, "times")
        s1, y = paragraph(t, simple, 11, JUSTIFY_LATIN, 72, H - 100, 400, 14, align="justify", mode="single-bt")
        fx.block(s1, "justify")
        s2, _ = paragraph(t, cid, 12, JUSTIFY_CYR, 72, y - 16, 400, 15, align="justify")
        fx.block(s2, "justify")
    else:
        font = CidFont(doc, "arial")
        y = H - 100
        for text in MIXED:
            spans, y = paragraph(t, font, 11, text, 72, y, 400, 14, align=align)
            fx.block(spans, align)
            y -= 14
    cv.ops.extend(t.ops)
    return save(doc, cv, dst, fx.name, fx)


def make_inline_image(dst):
    fx = Fixture("inline-image.pdf", "комментарии, BI…ID…EI с « EI » в данных, /AHx, BX/EX, BDC, экранирование",
                 ["inline-image", "bx-ex", "marked-content"])
    doc = EditDoc("Inline image, BX/EX")
    cv = doc.canvas()
    t = Text(cv, fx)
    font = SimpleFont(doc, "arial")
    t.raw("% comment: the first line of the stream\r")
    t.raw("q 0.2 g\t")
    t.fill = (0.2, 0.2, 0.2)
    t.bt()
    t.tf(font, 14)
    t.td(72, H - 100)
    a = t.tj("Inline image (BI) and BX/EX", literal=True)
    t.et()
    t.raw("/P <</MCID 0>> BDC")
    t.bt()
    t.tf(font, 11)
    t.td(72, H - 130)
    b = t.tj_array(["Marked", -250, "content with (parens) \\ A"], literal=True)
    t.et()
    t.raw("EMC")
    t.raw("BX /Foo 1 2 3 xyzzy EX")
    fx.block([a], "left")
    fx.block([b], "left")
    data = bytes((i * 37 + 11) % 256 for i in range(64))
    data = data[:20] + b" EI " + data[24:40] + b"EI\n" + data[43:]
    body = ("\n".join(t.ops) + "\nq 40 0 0 40 72 680 cm\nBI /W 8 /H 8 /CS /G /BPC 8 ID\n").encode("latin-1")
    body += data + b"\nEI Q\n"
    body += b"q 20 0 0 20 130 680 cm BI /W 2 /H 2 /CS /RGB /BPC 8 /F /AHx ID\nFF0000 00FF00 0000FF FFFF00>\nEI\nQ\n"
    body += b"% trailing comment without newline"
    cv.ops = []
    content = doc.pdf.make_indirect(Stream(doc.pdf, body))
    return save(doc, cv, dst, fx.name, fx, contents=content)


# --- Word (make_edit_fixtures.ps1) -------------------------------------------------------------------------

WORD_SPEC = {
    # один шрифт на файл (Arial): подмножество Word с хинтингом — 20–30 КБ на шрифт,
    # файл укладывается в 50 КБ
    "word-paragraphs.pdf": {
        "about": "Word: абзацы латиницы, кириллицы и смешанные, четыре выравнивания, отступ первой строки",
        "kinds": ["word", "align"],
        "paragraphs": [
            {"text": "Word paragraphs for the editor", "font": "Arial", "size": 16, "align": "left"},
            {"text": LATIN[0], "font": "Arial", "size": 11, "align": "left"},
            {"text": CYR[0], "font": "Arial", "size": 11, "align": "justify"},
            {"text": "Абзац по центру: строки симметричны относительно середины колонки. Centered text, "
                     "смешанный текст.", "font": "Arial", "size": 11, "align": "center"},
            {"text": "Правый край ровный, левый — нет: так выравнивают подписи и даты. Right aligned text.",
             "font": "Arial", "size": 11, "align": "right"},
            {"text": CYR[1], "font": "Arial", "size": 11, "align": "justify", "indent": 28},
        ],
    },
    "word-lists.pdf": {
        "about": "Word: маркированный и нумерованный списки",
        "kinds": ["word", "lists"],
        "paragraphs": [
            {"text": "Списки в Word", "font": "Arial", "size": 14, "align": "left"},
            {"text": "Первый пункт маркированного списка", "font": "Arial", "size": 11, "list": "bullet"},
            {"text": "Second bulleted item with Latin text", "font": "Arial", "size": 11, "list": "bullet"},
            {"text": "Третий пункт", "font": "Arial", "size": 11, "list": "bullet"},
            {"text": "Нумерованный пункт один", "font": "Arial", "size": 11, "list": "number"},
            {"text": "Numbered item two", "font": "Arial", "size": 11, "list": "number"},
        ],
    },
    "word-columns.pdf": {
        "about": "Word: раздел в две колонки и раздел в три колонки (колонки выровнены разрывами «на текущей странице»)",
        "kinds": ["word", "columns"],
        "sections": [
            {"columns": 2, "paragraphs": [LATIN[0], CYR[0], LATIN[1], CYR[1]]},
            {"columns": 3, "paragraphs": [MIXED[0], MIXED[1], LATIN[2]]},
        ],
    },
    "word-table.pdf": {
        "about": "Word: таблица 3 × 3 — ячейка рядом с ячейкой не склеивается в строку",
        "kinds": ["word", "table"],
        "table": [["Город", "Население", "Регион"], ["Казань", "1 300 000", "Татарстан"],
                  ["Sochi", "470 000", "Krasnodar"]],
    },
    "word-rotated.pdf": {
        "about": "Word: надпись с текстом, повёрнутым на 90°, — только чтение",
        "kinds": ["word", "rotated"],
        "paragraphs": [{"text": "Обычный абзац над надписью остаётся редактируемым.", "font": "Arial",
                        "size": 11, "align": "left"}],
        "rotatedText": "Повёрнутый текст надписи",
    },
}


def word_expectations(dst, name, spec):
    """Ожидания блоков файла Word: текст и выравнивание — из задания, x0/x1 — рамки символов PDFium."""
    import pypdfium2 as pdfium
    path = dst / name
    if not path.exists():
        return None
    pdf = pdfium.PdfDocument(str(path))
    page = pdf[0]
    tp = page.get_textpage()
    n = tp.count_chars()
    chars = []
    for i in range(n):
        ch = tp.get_text_range(i, 1)
        box = tp.get_charbox(i, loose=False)
        chars.append((ch, box))
    ph = page.get_height()
    flat = "".join(c for c, _ in chars)
    norm_idx = []
    norm = []
    for i, c in enumerate(flat):
        if c in "\r\n\x02 \u00a0\t":
            if norm and norm[-1] != " ":
                norm.append(" ")
                norm_idx.append(i)
            continue
        norm.append(c)
        norm_idx.append(i)
    text = "".join(norm)
    blocks = []

    def locate(ptext, align, read_only=""):
        target = " ".join(ptext.split())
        pos = text.find(target)
        b = {"text": target, "align": align, "readOnly": read_only, "page": 0}
        if pos >= 0:
            idx = [norm_idx[k] for k in range(pos, pos + len(target)) if norm[k] != " "]
            boxes = [chars[i][1] for i in idx if chars[i][1][2] > chars[i][1][0]]
            if boxes:
                b["x0"] = round(min(bx[0] for bx in boxes), 2)
                b["x1"] = round(max(bx[2] for bx in boxes), 2)
                b["tolerance"] = 2.0
        blocks.append(b)

    for p in spec.get("paragraphs", []):
        align = p.get("align", "left")
        locate(p["text"], align)
    if "table" in spec:
        for row in spec["table"]:
            for cell in row:
                locate(cell, "left")
    out = {"about": spec["about"], "kinds": spec["kinds"], "source": "tools/gen/make_edit_fixtures.ps1 (Word)",
           "pages": len(pdf), "blocks": blocks, "blocksMatch": "contains"}
    if "sections" in spec:
        out["columns"] = [s["columns"] for s in spec["sections"]]
        out["paragraphTexts"] = [p for s in spec["sections"] for p in s["paragraphs"]]
    if "rotatedText" in spec:
        out["rotatedText"] = spec["rotatedText"]
    pdf.close()
    return out


def main():
    def extra(ap):
        ap.add_argument("--word-spec", default="", help="записать задание Word в этот JSON и выйти")
        ap.add_argument("--word-dir", default="", help="каталог с word-*.pdf (по умолчанию — tests/fixtures/edit)")

    args = g.common_args(__doc__, extra=extra)
    if args.word_spec:
        Path(args.word_spec).write_text(json.dumps(WORD_SPEC, ensure_ascii=False, indent=2), encoding="utf-8")
        g.say("edit: задание Word → %s" % args.word_spec)
        return
    dst = Path(args.out) / "tests" / "fixtures" / "edit"
    dst.mkdir(parents=True, exist_ok=True)
    tmp = g.tmp_dir(args, "edit")
    fixtures = [
        make_latin(dst), make_cyr_cid(dst), make_type1_diff(dst), make_base14(dst, tmp), make_tj_kerning(dst),
        make_tz_tc_tw(dst), make_rotated(dst), make_two_streams(dst), make_xobject_form(dst),
        make_columns2(dst), make_columns3(dst),
        make_align(dst, "left"), make_align(dst, "right"), make_align(dst, "center"), make_align(dst, "justify"),
        make_inline_image(dst),
    ]
    meta = {
        "generator": "tools/gen/make_edit_fixtures.py",
        "coordinates": "points, origin top-left (MuPDF page space); baseline — y of the glyph origin",
        "span": "one text-showing operator (Tj, TJ, ', \"); x0 — origin of the first glyph, x1 — origin of the "
                "last glyph plus its advance (w0 × size × Tz)",
        "block": "paragraph: spans in reading order, lines joined by one space; x0/x1 — extent of its spans; "
                 "firstBaseline/lastBaseline — baselines of the first and last line (without Ts)",
        "readOnly": "empty — editable; otherwise the reason code (rotated, form, clip, type3, pattern, …)",
        "fixtures": {fx.name: fx.to_json() for fx in fixtures},
    }
    word_dir = Path(args.word_dir) if args.word_dir else dst
    specs = dict(WORD_SPEC)
    lo = dict(WORD_SPEC["word-paragraphs.pdf"], about="LibreOffice: тот же текст, что word-paragraphs.docx",
              kinds=["libreoffice", "align"])
    specs["lo-paragraphs.pdf"] = lo           # есть, только если make_edit_fixtures.ps1 нашёл LibreOffice
    for name, spec in specs.items():
        exp = word_expectations(word_dir, name, spec)
        if exp is not None:
            if name.startswith("lo-"):
                exp["source"] = "tools/gen/make_edit_fixtures.ps1 (LibreOffice)"
            meta["fixtures"][name] = exp
        elif not name.startswith("lo-"):
            g.say("edit: нет %s — ожидания Word пропущены (make_edit_fixtures.ps1)" % name)
    g.write_json(dst / "expected.json", meta)
    g.say("edit: %d фикстур, ожидания — expected.json" % len(meta["fixtures"]))


if __name__ == "__main__":
    main()
