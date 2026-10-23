# -*- coding: utf-8 -*-
"""make_text.py — фикстуры текстового слоя, поиска и выделения (сессия 1.4) в tests/fixtures/text/.

Что:
  search.pdf                 — 3 страницы A4, Type0/CIDFontType2 Identity-H + ToUnicode (подмножества Arial,
                               Arial Bold, Arial Italic): «Оттиск» в заголовке, «отти́ск» с комбинируемым ударением
                               (U+0301), «оттиск», «оттиска»; годы 1999, 2026 и число 12345; лигатуры U+FB01/U+FB02;
                               жирная и курсивная строки; две строки для выделения; цветная строка; на второй
                               странице «оттиск, ОТТИСК и Оттиск» и год 2027, третья — без искомого слова;
  find.pdf                   — 3 страницы для панели «Найти» (сессия 2.5): слово «печать» 12 раз (5 + 4 + 3; одно —
                               «ПЕЧАТЬ», одно — «Печать») и для «целого слова» — «печати», «впечатление»;
  vectors.pdf                — строки с подчёркиваниями отрезками (m l S), рамка (re S) и закрашенный
                               прямоугольник (re f) — для TextPage::Options{collectVectors};
  truetype-nonembedded.pdf   — простой TrueType /Arial **без встроенной программы шрифта** (подмена в читалке),
                               /WinAnsiEncoding + /Differences (кириллица на кодах cp1251 → afii100xx), без ToUnicode;
  text.json                  — что нарисовано: строки с координатами (базовая линия, начало и конец в пунктах
                               от левого верхнего угла), число отрезков и прямоугольников vectors.pdf.
Слова и координаты независимым источником (pypdfium2) пишет tools/gen/stext_expected.py → stext_expected.json.
Откуда: свои тексты; шрифты Arial/Arial Bold/Arial Italic из C:\\Windows\\Fonts.
Детерминизм: даты/XMP/ID фиксированы (_genlib.save_pdf), случайности нет.
Запуск: "C:\\Tools\\venv-test\\Scripts\\python.exe" -X utf8 -I tools/gen/make_text.py [--out <корень>]
"""
import sys
from pathlib import Path

sys.dont_write_bytecode = True                     # без __pycache__ в tools/gen
sys.path.insert(0, str(Path(__file__).resolve().parent))

import pikepdf  # noqa: E402
from pikepdf import Array, Dictionary, Name, Stream  # noqa: E402

import _genlib as g  # noqa: E402

LIMIT = 50 * 1024
A4W, A4H = g.A4


class Lines:
    """Строки, выведенные на страницу: для text.json (координаты MuPDF — y вниз от верхнего края)."""

    def __init__(self):
        self.items = []

    def add(self, page, x, y, text, font, size, name=None):
        self.items.append({
            "page": page, "name": name, "text": text, "size": size,
            "x0": round(x, 3), "x1": round(x + font.width(text, size), 3),
            "baseline": round(A4H - y, 3),
        })


def search_pdf(out: Path, lines: Lines):
    doc = g.Doc("search", "Поиск, выделение и стили текста", creator="tools/gen/make_text.py")
    reg = doc.font("arial")
    bold = doc.font("arialbd")
    ital = doc.font("ariali")

    def put(cv, page, x, y, text, font, size, name=None, rgb=(0, 0, 0)):
        cv.text(x, y, text, font, size, rgb=rgb)
        lines.add(page, x, y, text, font, size, name)

    cv = doc.canvas()
    put(cv, 0, 72, 760, "Оттиск: поиск и выделение текста", bold, 20, "title")
    put(cv, 0, 72, 720, "Слово отти\u0301ск стоит здесь с ударением.", reg, 12, "accent")
    put(cv, 0, 72, 700, "Без ударения: оттиск, а в родительном падеже — оттиска.", reg, 12, "plain")
    put(cv, 0, 72, 680, "Годы 1999 и 2026, номер 12345.", reg, 12, "years")
    put(cv, 0, 72, 660, "Лигатуры: \ufb01нал, \ufb02аг и обычные fi, fl.", reg, 12, "ligatures")
    put(cv, 0, 72, 630, "Жирная строка", bold, 14, "bold")
    put(cv, 0, 72, 610, "Курсивная строка", ital, 14, "italic")
    put(cv, 0, 72, 580, "Первая строка для выделения.", reg, 12, "select1")
    put(cv, 0, 72, 564, "Вторая строка для выделения.", reg, 12, "select2")
    put(cv, 0, 72, 530, "Красная строка", reg, 12, "red", rgb=(1, 0, 0))
    doc.add_page(cv)

    cv = doc.canvas()
    put(cv, 1, 72, 760, "Вторая страница: оттиск, ОТТИСК и Оттиск.", reg, 12, "page2")
    put(cv, 1, 72, 740, "Год 2027.", reg, 12, "page2year")
    doc.add_page(cv)

    cv = doc.canvas()
    put(cv, 2, 72, 760, "Третья страница без искомого слова.", reg, 12, "page3")
    doc.add_page(cv)
    doc.save(out)


def find_pdf(out: Path):
    """12 вхождений «печать» на трёх страницах (5 + 4 + 3) — панель «Найти» подсвечивает все, F3 обходит по кругу."""
    doc = g.Doc("find", "Панель «Найти»: двенадцать вхождений", creator="tools/gen/make_text.py")
    reg = doc.font("arial")
    pages = [
        ["Печать и подпись: печать ставится внизу.",
         "Вторая печать — на обороте, третья печать — в углу.",
         "Без печати документ недействителен; впечатление важно.",
         "Последняя печать первой страницы."],
        ["ПЕЧАТЬ крупно и печать мелко.",
         "Ещё одна печать и снова печать."],
        ["Третья страница: печать, печать и печать."],
    ]
    for lines in pages:
        cv = doc.canvas()
        y = 760
        for text in lines:
            cv.text(72, y, text, reg, 12)
            y -= 24
        doc.add_page(cv)
    doc.save(out)
    return {"word": "печать", "hits": 12, "per_page": [5, 4, 3], "case_sensitive_lower": 10,
            "not_whole_word": ["печати", "впечатление"]}


def vectors_pdf(out: Path, lines: Lines):
    doc = g.Doc("vectors", "Подчёркивания, рамка и заливка для collectVectors", creator="tools/gen/make_text.py")
    f = doc.font("arial")
    cv = doc.canvas()
    underlined = ["Фамилия", "Имя", "Подпись"]
    y = 740
    for word in underlined:
        cv.text(72, y, word + ":", f, 12)
        lines.add(3, 72, y, word + ":", f, 12, "label")
        cv.line(160, y - 2, 400, y - 2, lw=0.75)       # поле-подчёркивание: отрезок m l S
        y -= 30
    cv.rect(72, 560, 300, 60, stroke=(0, 0, 0), lw=1.0)    # рамка: re S
    cv.rect(400, 560, 40, 40, fill=(0.2, 0.4, 0.8))        # заливка: re f
    cv.text(80, 595, "Текст в рамке", f, 12)
    lines.add(3, 80, 595, "Текст в рамке", f, 12, "framed")
    doc.add_page(cv)
    doc.save(out)
    return {"underlines": len(underlined), "stroked_rects": 1, "filled_rects": 1,
            "underline_width": 0.75}


def truetype_nonembedded(out: Path, lines: Lines):
    pdf = pikepdf.new()
    arial = g.TrueType(g.FONTS_DIR / "arial.ttf")
    text = ["Невстроенный TrueType: Arial", "Привет, мир — Ёлка, ёж, щука.", "Hello, world! 2026"]
    used = {}
    for s in text:
        for code, ch in zip(s.encode("cp1251"), s):
            used[code] = ch
    codes = sorted(used)
    first, last = codes[0], codes[-1]
    s1000 = 1000.0 / arial.units
    widths = Array([round(arial.advance(arial.gid(used[c])) * s1000) if c in used else 0
                    for c in range(first, last + 1)])
    diffs = Array()
    prev = None
    for c in codes:
        if c >= 0x80:
            if prev is None or c != prev + 1:
                diffs.append(c)
            diffs.append(Name("/" + g.agl_name(used[c])))
            prev = c
    fd = pdf.make_indirect(Dictionary(
        Type=Name.FontDescriptor, FontName=Name.Arial, Flags=32,
        FontBBox=Array([round(v * s1000) for v in arial.bbox]), ItalicAngle=0,
        Ascent=round(arial.ascent * s1000), Descent=round(arial.descent * s1000),
        CapHeight=round(arial.cap_height * s1000), StemV=80))           # без FontFile2 — не встроен
    font = pdf.make_indirect(Dictionary(
        Type=Name.Font, Subtype=Name.TrueType, BaseFont=Name.Arial, FirstChar=first, LastChar=last,
        Widths=widths, FontDescriptor=fd,
        Encoding=Dictionary(Type=Name.Encoding, BaseEncoding=Name.WinAnsiEncoding, Differences=diffs)))

    class Metric:                                       # ширина строки для text.json
        @staticmethod
        def width(s, size):
            return sum(arial.advance(arial.gid(ch)) for ch in s) * size / arial.units

    ops = []
    y = 740
    for i, s in enumerate(text):
        size = 18 if i == 0 else 14
        ops.append("BT /F1 %s Tf 72 %s Td <%s> Tj ET" % (size, y, s.encode("cp1251").hex().upper()))
        lines.add(4, 72, y, s, Metric, size, "truetype")
        y -= 36
    page = Dictionary(Type=Name.Page, MediaBox=Array([0, 0, 595.2756, 841.8898]),
                      Resources=Dictionary(Font=Dictionary(F1=font)),
                      Contents=pdf.make_indirect(Stream(pdf, ("\n".join(ops) + "\n").encode("latin-1"))))
    pdf.pages.append(pikepdf.Page(page))
    g.save_pdf(pdf, out, title="truetype-nonembedded", subject="Невстроенный TrueType с /Differences",
               creator="tools/gen/make_text.py")


def main():
    args = g.common_args(__doc__)
    dst = Path(args.out) / "tests" / "fixtures" / "text"
    dst.mkdir(parents=True, exist_ok=True)
    lines = Lines()
    search_pdf(dst / "search.pdf", lines)
    vec = vectors_pdf(dst / "vectors.pdf", lines)
    truetype_nonembedded(dst / "truetype-nonembedded.pdf", lines)
    find = find_pdf(dst / "find.pdf")
    files = ["search.pdf", "vectors.pdf", "truetype-nonembedded.pdf", "find.pdf"]
    for name in files:
        g.check_size(dst / name, LIMIT)
    by_file = {"search.pdf": [0, 1, 2], "vectors.pdf": [3], "truetype-nonembedded.pdf": [4]}
    meta = {"generator": "tools/gen/make_text.py", "coordinates": "points, origin top-left (MuPDF page space)",
            "files": {}}
    for name, keys in by_file.items():
        items = []
        for it in lines.items:
            if it["page"] in keys:
                rec = dict(it)
                rec["page"] = keys.index(it["page"]) if name == "search.pdf" else 0
                items.append(rec)
        meta["files"][name] = {"lines": items}
    meta["files"]["search.pdf"]["pages"] = 3
    meta["files"]["vectors.pdf"]["vectors"] = vec
    meta["files"]["find.pdf"] = find
    meta["files"]["truetype-nonembedded.pdf"]["font"] = "TrueType /Arial не встроен, WinAnsi + /Differences cp1251"
    g.write_json(dst / "text.json", meta)
    g.say("text: %s" % ", ".join(files))


if __name__ == "__main__":
    main()
