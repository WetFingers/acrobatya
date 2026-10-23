# -*- coding: utf-8 -*-
"""make_view.py — фикстуры виджета просмотра (сессия 2.2) в tests/fixtures/view/.

Что:
  layout-mixed.pdf  — 5 страниц разного размера и поворота для раскладки, зума и рендера PageView:
                      1) Letter 612 × 792; 2) A4 с /Rotate 90 (на экране 842 × 595); 3) A4 595 × 842;
                      4) 300 × 400; 5) Letter с /Rotate 100 (MuPDF округляет до 90 — на экране 792 × 612).
                      На каждой — крупная надпись «Страница N», рамка у полей и цветной квадрат в левом
                      верхнем углу (по нему видно поворот). /PageLayout /TwoColumnRight — «разворот, титул
                      отдельно» при открытии (эталон 3.1);
  layout-expected.json — что нарисовано: размеры MediaBox, /Rotate и ожидаемый размер на экране (пункты).
Ожидания раскладки (layout_expected.json рядом) посчитаны вручную по правилам ui/view/layout.h — не этим
скриптом.
Откуда: свои тексты; шрифт Arial из C:\\Windows\\Fonts (подмножество).
Детерминизм: даты/XMP/ID фиксированы (_genlib.save_pdf), случайности нет.
Запуск: "C:\\Tools\\venv-test\\Scripts\\python.exe" -X utf8 -I tools/gen/make_view.py [--out <корень>]
"""
import sys
from pathlib import Path

sys.dont_write_bytecode = True                     # без __pycache__ в tools/gen
sys.path.insert(0, str(Path(__file__).resolve().parent))

from pikepdf import Name  # noqa: E402

import _genlib as g  # noqa: E402

LIMIT = 50 * 1024
A4 = (595.2756, 841.8898)
LETTER = (612.0, 792.0)

PAGES = [
    {"size": LETTER, "rotate": 0, "screen": [612, 792], "rgb": (0.80, 0.20, 0.15)},
    {"size": A4, "rotate": 90, "screen": [841.8898, 595.2756], "rgb": (0.15, 0.45, 0.80)},
    {"size": A4, "rotate": 0, "screen": [595.2756, 841.8898], "rgb": (0.20, 0.60, 0.25)},
    {"size": (300.0, 400.0), "rotate": 0, "screen": [300, 400], "rgb": (0.55, 0.25, 0.70)},
    {"size": LETTER, "rotate": 100, "screen": [792, 612], "rgb": (0.90, 0.60, 0.10)},
]


def layout_mixed(out: Path) -> list:
    doc = g.Doc("layout-mixed", "Страницы разного размера и поворота для PageView",
                creator="tools/gen/make_view.py")
    font = doc.font("arial")
    meta = []
    for i, p in enumerate(PAGES):
        w, h = p["size"]
        cv = doc.canvas((w, h))
        cv.rect(12, 12, w - 24, h - 24, stroke=(0.2, 0.2, 0.2), lw=1.5)
        cv.rect(24, h - 24 - 60, 60, 60, fill=p["rgb"])
        size = min(w, h) / 9
        cv.text_center(w / 2, h / 2, "Страница %d" % (i + 1), font, size)
        cv.text_center(w / 2, h / 2 - size * 1.4, "%g × %g pt, /Rotate %d" % (round(w, 2), round(h, 2), p["rotate"]),
                       font, size / 3, rgb=(0.35, 0.35, 0.35))
        doc.add_page(cv, rotate=p["rotate"])
        meta.append({"page": i, "mediabox": [round(w, 4), round(h, 4)], "rotate": p["rotate"],
                     "screen_pt": p["screen"]})
    doc.pdf.Root.PageLayout = Name.TwoColumnRight
    doc.save(out)
    return meta


def main():
    args = g.common_args(__doc__)
    dst = Path(args.out) / "tests" / "fixtures" / "view"
    dst.mkdir(parents=True, exist_ok=True)
    pages = layout_mixed(dst / "layout-mixed.pdf")
    g.check_size(dst / "layout-mixed.pdf", LIMIT)
    g.write_json(dst / "layout-mixed.json", {
        "generator": "tools/gen/make_view.py", "pdf": "layout-mixed.pdf", "page_layout": "TwoColumnRight",
        "pages": pages,
    })
    g.say("view: layout-mixed.pdf")


if __name__ == "__main__":
    main()
