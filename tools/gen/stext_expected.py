# -*- coding: utf-8 -*-
"""stext_expected.py — эталон слов и координат для текстового слоя (сессия 1.4) независимым источником.

Что: для кириллических фикстур (CID Identity-H с ToUnicode, Type1 с /Differences, невстроенный TrueType,
поиск) извлекает символы через pypdfium2 (PDFium — не MuPDF), режет на слова по пробельным символам и
пишет tests/fixtures/text/stext_expected.json:
  files → {<файл>: {embedded, pages: [{width, height, words: [{text, size, origin: [x, y], bbox: [x0, y0, x1, y1]}]}]}}
Координаты — пункты страницы MuPDF: начало в левом верхнем углу, y вниз (pdfium даёт y вверх — переводится).
origin — точка пера первого символа (FPDFText_GetCharOrigin; у MuPDF — fz_stext_char.origin), bbox — объединение
тесных рамок глифов (FPDFText_GetCharBox: чернила; у MuPDF с ACCURATE_BBOXES по y — тоже чернила, по x —
от пера до пера, поэтому тест сравнивает x1 с допуском от кегля). У невстроенных шрифтов глифы подменяют обе
программы по-своему — чернила различаются, сравнивать можно только текст и точки пера (embedded = false).
Откуда: <--out>/tests/data/mini/{cyr-cid,type1-diff}.pdf (make_mini.py), <--out>/tests/fixtures/text/*.pdf
(make_text.py) — запускать после них.
Детерминизм: результат зависит только от входных файлов и версии pypdfium2 (5.14.0; округление до 0,01 pt).
Запуск: "C:\\Tools\\venv-test\\Scripts\\python.exe" -X utf8 -I tools/gen/stext_expected.py [--out <корень>]
"""
import ctypes
import sys
from pathlib import Path

sys.dont_write_bytecode = True                     # без __pycache__ в tools/gen
sys.path.insert(0, str(Path(__file__).resolve().parent))

import pypdfium2 as pdfium  # noqa: E402
import pypdfium2.raw as raw  # noqa: E402

import _genlib as g  # noqa: E402

# (путь от корня, встроены ли все шрифты)
SOURCES = [
    ("tests/data/mini/cyr-cid.pdf", True),
    ("tests/data/mini/type1-diff.pdf", False),
    ("tests/fixtures/text/truetype-nonembedded.pdf", False),
    ("tests/fixtures/text/search.pdf", True),
]


def _r(v: float) -> float:
    return round(v + 0.0, 2)


def page_words(page) -> dict:
    width, height = page.get_size()
    tp = page.get_textpage()
    words = []
    cur = None
    for i in range(tp.count_chars()):
        u = raw.FPDFText_GetUnicode(tp.raw, i)
        ch = chr(u) if u else ""
        generated = raw.FPDFText_IsGenerated(tp.raw, i) == 1
        if not ch or ch.isspace() or generated:
            if cur is not None:
                words.append(cur)
                cur = None
            continue
        left, bottom, right, top = tp.get_charbox(i)
        if cur is None:
            x = ctypes.c_double()
            y = ctypes.c_double()
            raw.FPDFText_GetCharOrigin(tp.raw, i, x, y)
            cur = {"text": "", "size": _r(raw.FPDFText_GetFontSize(tp.raw, i)),
                   "origin": [_r(x.value), _r(height - y.value)],
                   "bbox": [left, height - top, right, height - bottom]}
        cur["text"] += ch
        b = cur["bbox"]
        if right > left or top > bottom:            # у нулевых рамок (метки без чернил) нечего объединять
            b[0] = min(b[0], left)
            b[1] = min(b[1], height - top)
            b[2] = max(b[2], right)
            b[3] = max(b[3], height - bottom)
    if cur is not None:
        words.append(cur)
    for w in words:
        w["bbox"] = [_r(v) for v in w["bbox"]]
    return {"width": _r(width), "height": _r(height), "words": words}


def main():
    args = g.common_args(__doc__)
    root = Path(args.out)
    files = {}
    for rel, embedded in SOURCES:
        pdf = pdfium.PdfDocument(str(root / rel))
        pages = [page_words(pdf[i]) for i in range(len(pdf))]
        files[rel] = {"embedded": embedded, "pages": pages}
        pdf.close()
    meta = {
        "generator": "tools/gen/stext_expected.py",
        "source": "pypdfium2 %s (PDFium)" % pdfium.version.PYPDFIUM_INFO,
        "coordinates": "points, origin top-left (MuPDF page space); bbox — ink",
        "files": files,
    }
    g.write_json(root / "tests" / "fixtures" / "text" / "stext_expected.json", meta)
    g.say("stext_expected: %d файлов, %d слов" % (
        len(files), sum(len(p["words"]) for f in files.values() for p in f["pages"])))


if __name__ == "__main__":
    main()
