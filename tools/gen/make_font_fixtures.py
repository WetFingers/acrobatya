# -*- coding: utf-8 -*-
"""make_font_fixtures.py — фикстуры шрифтов редактора (сессия 3.2) в tests/fixtures/edit/fonts/.

Что:
  fonts.pdf          — одна страница, строка на шрифт (F1–F9): Arial-BoldMT — подмножество Type0/CIDFontType2;
                       Times New Roman Italic — подмножество простого TrueType (/ItalicAngle, /Flags Italic);
                       Courier — base14 без встраивания; «Arial,BoldItalic» — TrueType без встраивания в стиле Word
                       (/Widths, /FontDescriptor с ForceBold и наклоном); Georgia — встроенная Type0 без префикса
                       подмножества; Type3 «Squares»; «FantasySans» — встроенное подмножество (Verdana) под именем,
                       которого нет в системе; «FantasySerif-Bold» — TrueType без встраивания, засечки, жирный, имени
                       нет в системе; «Tahoma-Italic» — TrueType без встраивания (у Tahoma нет курсива);
  mixed-latin.pdf    — абзац латиницы простым TrueType Arial (подмножество только ASCII, /WinAnsiEncoding, как у
                       Word): в него набирают «Привет, world! — 2026 €» (план 3.2, п. 5);
  mixed-expected.pdf — тот же абзац и строка-эталон «Привет, world! — 2026 €», которую генератор набрал сам:
                       «, world! » — шрифтом абзаца, остальное — Type0 Arial с /W как у pdf_add_cid_font MuPDF
                       (ширина = trunc(1000 · advance / unitsPerEm)) — позиции глифов совпадают с набором программы;
  fonts.json         — ожидания font_info_test (тип, подмножество, кодировка, флаги, наклон, StemV, стиль, какие
                       символы есть и каких нет) и место набора строки для font_mixed_test.
Откуда: свои тексты; шрифты Arial, Times New Roman, Georgia, Verdana, Tahoma из C:\\Windows\\Fonts (встраивание
разрешено fsType); подмножества — _genlib (GID сохраняются).
Детерминизм: даты/XMP/ID фиксированы (_genlib.save_pdf), теги подмножеств — из хеша набора глифов.
Запуск: "C:\\Tools\\venv-test\\Scripts\\python.exe" -X utf8 -I tools/gen/make_font_fixtures.py [--out <корень>]
"""
import json
import struct
import sys
from pathlib import Path

sys.dont_write_bytecode = True                     # без __pycache__ в tools/gen
sys.path.insert(0, str(Path(__file__).resolve().parent))

from pikepdf import Array, Dictionary, Name, Stream, String  # noqa: E402

import _genlib as g  # noqa: E402

LIMIT = 50 * 1024
W, H = g.A4

PARAGRAPH = ("Latin subset paragraph: hello, world! The quick brown fox jumps over the lazy dog, and this "
             "line wraps onto the next one so that the paragraph has several lines.")
MIXED = "Привет, world! — 2026 €"
MIXED_RUNS = [["Привет", "new"], [", world! ", "original"], ["— 2026 €", "new"]]


def w1000(tt, ch, trunc=False):
    adv = tt.advance(tt.gid(ch))
    return int(adv * 1000.0 / tt.units) if trunc else round(adv * 1000.0 / tt.units)


class TruncCidFont(g.PdfFont):
    """Type0 Identity-H, у которого /W — как пишет pdf_add_cid_font MuPDF: trunc(1000 · advance / unitsPerEm)."""

    def finish(self):
        if self.finished:
            return
        super().finish()
        tt = self.tt
        w = Array()
        for gid in sorted(self.used):
            w.append(gid)
            w.append(Array([int(tt.advance(gid) * 1000.0 / tt.units)]))
        self.ref.DescendantFonts[0].W = w


def simple_widths(tt, text):
    """/FirstChar, /LastChar, /Widths для WinAnsi по символам text (как у Word без встраивания)."""
    codes = sorted({ch.encode("cp1252")[0]: ch for ch in text}.items())
    first, last = codes[0][0], codes[-1][0]
    by_code = dict(codes)
    widths = [w1000(tt, by_code[c]) if c in by_code else 0 for c in range(first, last + 1)]
    return first, last, Array(widths)


def descriptor(pdf, name, tt, flags, italic_angle, stem_v):
    s = 1000.0 / tt.units
    return pdf.make_indirect(Dictionary(
        Type=Name.FontDescriptor, FontName=Name("/" + name), Flags=flags,
        FontBBox=Array([round(v * s) for v in tt.bbox]), ItalicAngle=italic_angle,
        Ascent=round(tt.ascent * s), Descent=round(tt.descent * s), CapHeight=round(tt.cap_height * s),
        StemV=stem_v))


def truetype_unembedded(pdf, name, file, text, flags, italic_angle, stem_v):
    tt = g.TrueType(g.FONTS_DIR / file)
    first, last, widths = simple_widths(tt, text)
    font = pdf.make_indirect(Dictionary(
        Type=Name.Font, Subtype=Name.TrueType, BaseFont=Name("/" + name), Encoding=Name.WinAnsiEncoding,
        FirstChar=first, LastChar=last, Widths=widths,
        FontDescriptor=descriptor(pdf, name, tt, flags, italic_angle, stem_v)))
    return font, tt


class Ref:
    """Готовый словарь шрифта для Canvas: .ref, encode (cp1252 — 1 байт), width."""

    def __init__(self, ref, tt=None, encode=None):
        self.ref = ref
        self.tt = tt
        self._encode = encode

    def encode(self, s):
        return self._encode(s) if self._encode else s.encode("cp1252")

    def width(self, s, size):
        return sum(w1000(self.tt, ch) for ch in s) * size / 1000.0


def type3_squares(pdf):
    procs = Dictionary()
    procs.a = pdf.make_indirect(Stream(pdf, b"600 0 50 0 550 500 d1\n50 0 500 500 re f\n"))
    procs.b = pdf.make_indirect(Stream(pdf, b"600 0 50 0 550 500 d1\n50 0 500 500 re S\n"))
    return pdf.make_indirect(Dictionary(
        Type=Name.Font, Subtype=Name.Type3, Name=Name("/Squares"), FontBBox=Array([0, 0, 600, 500]),
        FontMatrix=Array([0.001, 0, 0, 0.001, 0, 0]), CharProcs=procs,
        Encoding=Dictionary(Type=Name.Encoding, Differences=Array([97, Name("/a"), Name("/b")])),
        FirstChar=97, LastChar=98, Widths=Array([600, 600]), Resources=Dictionary()))


def chars(s):
    return sorted({ch for ch in s if not ch.isspace()})


def make_fonts(dst):
    doc = g.Doc("Edit fixture: fonts", creator="tools/gen/make_font_fixtures.py")
    pdf = doc.pdf
    cv = doc.canvas()
    expect = {}
    y = H - 72

    def line(key, font, text, size=14):
        nonlocal y
        cv.fonts[key] = font
        cv.ops.append("BT /%s %s Tf 72 %s Td %s Tj ET" % (key, size, g._num(y), g._pdfstr(font.encode(text))))
        y -= 28
        return text

    # F1 — подмножество Type0 Arial Bold.
    f1 = doc.font("arialbd")
    t1 = line("F1", f1, "Arial Bold subset: CID Identity-H")
    expect["F1"] = dict(baseFont="tag+Arial-BoldMT", name="Arial-BoldMT", family="Arial", subtype="Type0",
                        cidSubtype="CIDFontType2", encoding="Identity-H", file="TrueType", embedded=True,
                        subset=True, base14=False, toUnicode=True, flags=32, italicAngle=0, stemV=120,
                        weight=700, bold=True, italic=False, serif=False, fixedPitch=False,
                        has=chars(t1), missing=["ё", "Ж", "€", "z", "7"])
    # F2 — подмножество простого TrueType Times New Roman Italic.
    f2 = doc.simple_font("timesi")
    t2 = line("F2", f2, "Times Italic simple TrueType")
    expect["F2"] = dict(name="TimesNewRomanPS-ItalicMT", family="Times New Roman", subtype="TrueType",
                        cidSubtype="", file="TrueType", embedded=True, subset=True, base14=False, toUnicode=False,
                        flags=32 | 2 | 64, italicAngle=round(g.TrueType(g.FONTS_DIR / "timesi.ttf").italic_angle, 2),
                        stemV=80, weight=400, bold=False, italic=True, serif=True, fixedPitch=False,
                        has=chars(t2), missing=["ё", "€", "z", "W"])
    # F3 — Courier base14 без встраивания.
    cour = g.TrueType(g.FONTS_DIR / "cour.ttf")
    f3 = Ref(pdf.make_indirect(Dictionary(Type=Name.Font, Subtype=Name.Type1, BaseFont=Name.Courier,
                                          Encoding=Name.WinAnsiEncoding)), cour)
    line("F3", f3, "Courier base 14 font")
    expect["F3"] = dict(name="Courier", family="Courier", subtype="Type1", cidSubtype="", encoding="WinAnsiEncoding",
                        file="", embedded=False, subset=False, base14=True, toUnicode=False, weight=400, bold=False,
                        italic=False, fixedPitch=True, has=["C", "z", "7", "€"], missing=["Ж"])
    # F4 — «Arial,BoldItalic» без встраивания (Word): ForceBold, наклон, StemV.
    t4 = "Arial bold italic, not embedded"
    ref4, tt4 = truetype_unembedded(pdf, "Arial,BoldItalic", "arialbi.ttf", t4, 32 | 64 | (1 << 18), -12, 153)
    line("F4", Ref(ref4, tt4), t4)
    expect["F4"] = dict(name="Arial,BoldItalic", family="Arial", subtype="TrueType", cidSubtype="",
                        encoding="WinAnsiEncoding", file="", embedded=False, subset=False, base14=False,
                        toUnicode=False, flags=32 | 64 | (1 << 18), italicAngle=-12, stemV=153, weight=700,
                        bold=True, italic=True, serif=False, fixedPitch=False, has=chars(t4), missing=["Ж"])
    # F5 — Georgia Type0 без префикса подмножества.
    f5 = doc.font("georgia")
    t5 = line("F5", f5, "Georgia without subset tag")
    expect["F5"] = dict(name="Georgia", family="Georgia", subtype="Type0", cidSubtype="CIDFontType2",
                        encoding="Identity-H", file="TrueType", embedded=True, subset=False, base14=False,
                        toUnicode=True, flags=34, weight=400, bold=False, italic=False, serif=True,
                        has=chars(t5), missing=["ё", "q", "€"])
    # F6 — Type3.
    f6 = Ref(type3_squares(pdf), None, lambda s: s.encode("ascii"))
    cv.fonts["F6"] = f6
    cv.ops.append("BT /F6 14 Tf 72 %s Td <61626162> Tj ET" % g._num(y))
    y -= 28
    expect["F6"] = dict(name="", family="", subtype="Type3", cidSubtype="", file="", embedded=False, subset=False,
                        base14=False, type3=True, has=[], missing=["a"])
    # F7 — встроенное подмножество Verdana под чужим именем «FantasySans».
    f7 = doc.font("verdana.ttf")
    t7 = line("F7", f7, "Fantasy sans subset")
    expect["F7"] = dict(name="FantasySans", family="Fantasy Sans", subtype="Type0", cidSubtype="CIDFontType2",
                        file="TrueType", embedded=True, subset=True, base14=False, toUnicode=True, weight=400,
                        bold=False, italic=False, serif=False, has=chars(t7), missing=["ё", "€"])
    # F8 — «FantasySerif-Bold» без встраивания: засечки, жирный.
    t8 = "Fantasy serif bold"
    ref8, tt8 = truetype_unembedded(pdf, "FantasySerif-Bold", "timesbd.ttf", t8, 32 | 2, 0, 136)
    line("F8", Ref(ref8, tt8), t8)
    expect["F8"] = dict(name="FantasySerif-Bold", family="Fantasy Serif", subtype="TrueType", file="",
                        embedded=False, subset=False, flags=34, stemV=136, weight=700, bold=True, italic=False,
                        serif=True)
    # F9 — «Tahoma-Italic» без встраивания (курсива у Tahoma нет).
    t9 = "Tahoma italic"
    ref9, tt9 = truetype_unembedded(pdf, "Tahoma-Italic", "tahoma.ttf", t9, 32 | 64, -10, 80)
    line("F9", Ref(ref9, tt9), t9)
    expect["F9"] = dict(name="Tahoma-Italic", family="Tahoma", subtype="TrueType", file="", embedded=False,
                        subset=False, flags=96, italicAngle=-10, weight=400, bold=False, italic=True)

    doc.add_page(cv)
    doc.finish_fonts()
    # Правки словарей после сборки подмножеств: наклон и флаги курсива F2, Georgia без префикса, FantasySans.
    fd2 = f2.ref.FontDescriptor
    fd2.Flags = 32 | 2 | 64
    fd2.ItalicAngle = round(f2.tt.italic_angle, 2)
    base5 = str(f5.ref.BaseFont)[1:].split("+", 1)[1]
    f5.ref.BaseFont = Name("/" + base5)
    f5.ref.DescendantFonts[0].BaseFont = Name("/" + base5)
    f5.ref.DescendantFonts[0].FontDescriptor.FontName = Name("/" + base5)
    tag7 = str(f7.ref.BaseFont)[1:].split("+", 1)[0]
    base7 = tag7 + "+FantasySans"
    f7.ref.BaseFont = Name("/" + base7)
    f7.ref.DescendantFonts[0].BaseFont = Name("/" + base7)
    f7.ref.DescendantFonts[0].FontDescriptor.FontName = Name("/" + base7)
    expect["F1"]["baseFont"] = str(f1.ref.BaseFont)[1:]
    expect["F5"]["baseFont"] = base5
    expect["F7"]["baseFont"] = base7
    g.save_pdf(pdf, dst / "fonts.pdf", title=doc.title, creator=doc.creator)
    g.check_size(dst / "fonts.pdf", LIMIT)
    return expect


def paragraph_lines(font, size, width):
    return list(g.wrap(PARAGRAPH, font, size, width))


def make_mixed(dst, with_line):
    name = "mixed-expected.pdf" if with_line else "mixed-latin.pdf"
    doc = g.Doc("Edit fixture: Latin subset paragraph" + (" with mixed line" if with_line else ""),
                creator="tools/gen/make_font_fixtures.py")
    cv = doc.canvas()
    latin = doc.simple_font("arial")
    size, leading, x, top = 12, 14.4, 72, H - 100
    y = top
    lines = paragraph_lines(latin, size, 440)
    for text in lines:
        cv.text(x, y, text, latin, size)
        y -= leading
    place = {"x": x, "y": round(y, 4), "size": size, "font": cv._fkey(latin), "text": MIXED, "runs": MIXED_RUNS,
             "lines": len(lines)}
    if with_line:
        arial = TruncCidFont(doc.pdf, "arial")
        doc._fonts["arial-trunc"] = arial
        kl, ka = cv._fkey(latin), cv._fkey(arial)
        ops = ["BT", "/%s %s Tf %s %s Td" % (ka, size, g._num(x), g._num(y))]
        for text, kind in MIXED_RUNS:
            if kind == "original":
                ops.append("/%s %s Tf %s Tj" % (kl, size, g._pdfstr(latin.encode(text))))
            else:
                ops.append("/%s %s Tf %s Tj" % (ka, size, g._pdfstr(arial.encode(text))))
        ops.append("ET")
        cv.ops.extend(ops)
    doc.add_page(cv)
    doc.finish_fonts()
    latin.ref.Encoding = Name.WinAnsiEncoding         # как у Word: без /Differences (только ASCII)
    g.save_pdf(doc.pdf, dst / name, title=doc.title, creator=doc.creator)
    g.check_size(dst / name, LIMIT)
    return place


def main():
    args = g.common_args("фикстуры шрифтов редактора (tests/fixtures/edit/fonts)")
    dst = Path(args.out) / "tests" / "fixtures" / "edit" / "fonts"
    dst.mkdir(parents=True, exist_ok=True)
    meta = {"fonts.pdf": make_fonts(dst)}
    place = make_mixed(dst, False)
    make_mixed(dst, True)
    meta["mixed"] = place
    g.write_json(dst / "fonts.json", meta)
    text = g.pdf_text(dst / "mixed-expected.pdf")
    if MIXED not in text:
        raise SystemExit("mixed-expected.pdf: mutool не извлёк строку «%s»: %r" % (MIXED, text))
    for name in ("fonts.pdf", "mixed-latin.pdf", "mixed-expected.pdf"):
        g.say("%s: %d байт" % (name, (dst / name).stat().st_size))


if __name__ == "__main__":
    main()
