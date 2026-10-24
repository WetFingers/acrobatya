# -*- coding: utf-8 -*-
"""make_pages.py — фикстуры печати (сессия 2.6) в tests/fixtures/pages/.

Что:
  mixed-sizes.pdf  — 4 страницы для спуска полос и печати в PdfFormat: 1) A4 книжная; 2) A5 книжная;
                     3) A4 альбомная; 4) плакат A1 (594 × 841 мм). На каждой — рамка у полей (10 pt от края),
                     крупная надпись «A4», «A5»… по центру, цветной квадрат 60 × 60 pt в левом верхнем углу
                     (по нему видно поворот на листе) и номер страницы внизу;
  print-layers.pdf — 1 страница A4 со слоями (view-42 «скрытые слои не печатаются»): «Только экран»
                     (/Usage /Print /PrintState /OFF — синий квадрат 200 × 200 слева вверху: на экране есть, на
                     печати нет) и «Общий» (красный квадрат 200 × 200 справа внизу — и на экране, и на печати;
                     выключенный в панели «Слои» — не печатается);
  pages.json       — размеры страниц и где что нарисовано (пункты, y вниз от левого верхнего угла).
Откуда: свои тексты; шрифт Arial из C:\\Windows\\Fonts (подмножество).
Детерминизм: даты/XMP/ID фиксированы (_genlib.save_pdf), случайности нет.
Запуск: "C:\\Tools\\venv-test\\Scripts\\python.exe" -X utf8 -I tools/gen/make_pages.py [--out <корень>]
"""
import sys
from pathlib import Path

sys.dont_write_bytecode = True                     # без __pycache__ в tools/gen
sys.path.insert(0, str(Path(__file__).resolve().parent))

from pikepdf import Array, Dictionary, Name, String  # noqa: E402

import _genlib as g  # noqa: E402

LIMIT = 50 * 1024
MM = 72 / 25.4
A4 = (595.2756, 841.8898)
A5 = (419.5276, 595.2756)
A4L = (841.8898, 595.2756)
A1 = (round(594 * MM, 4), round(841 * MM, 4))

PAGES = [
    {"name": "A4", "size": A4, "rgb": (0.80, 0.20, 0.15)},
    {"name": "A5", "size": A5, "rgb": (0.15, 0.45, 0.80)},
    {"name": "A4 альбомная", "size": A4L, "rgb": (0.20, 0.60, 0.25)},
    {"name": "A1", "size": A1, "rgb": (0.55, 0.25, 0.70)},
]


def mixed(out: Path) -> list:
    doc = g.Doc("Страницы для печати: A4, A5, альбомная, A1", "Спуск полос печати АкроБати",
                creator="tools/gen/make_pages.py")
    font = doc.font("arial")
    meta = []
    for i, p in enumerate(PAGES):
        w, h = p["size"]
        cv = doc.canvas((w, h))
        cv.rect(10, 10, w - 20, h - 20, stroke=(0.2, 0.2, 0.2), lw=2)
        cv.rect(20, h - 20 - 60, 60, 60, fill=p["rgb"])
        size = min(w, h) / 6
        cv.text_center(w / 2, h / 2 - size / 3, p["name"], font, size)
        cv.text_center(w / 2, 30, "Страница %d из %d" % (i + 1, len(PAGES)), font, 12, rgb=(0.35, 0.35, 0.35))
        doc.add_page(cv)
        meta.append({"page": i, "name": p["name"], "size": [round(w, 4), round(h, 4)],
                     "square": [20, 20, 80, 80], "square_rgb": list(p["rgb"])})
    doc.save(out)
    return meta


def layers(out: Path) -> dict:
    doc = g.Doc("Слои при печати", "Слой «Только экран» не печатается", creator="tools/gen/make_pages.py")
    pdf = doc.pdf
    font = doc.font("arial")
    w, h = A4
    cv = doc.canvas(A4)
    cv.text(72, h - 40, "Слои при печати", font, 18)
    cv.raw("/OC /ocScreen BDC")
    cv.rect(50, h - 50 - 200, 200, 200, fill=(0.1, 0.3, 0.9))
    cv.raw("EMC")
    cv.raw("/OC /ocCommon BDC")
    cv.rect(w - 250, 50, 200, 200, fill=(0.9, 0.1, 0.1))
    cv.raw("EMC")
    doc.add_page(cv)
    screen = pdf.make_indirect(Dictionary(Type=Name.OCG, Name=String("Только экран"),
                                          Usage=Dictionary(Print=Dictionary(PrintState=Name.OFF),
                                                           View=Dictionary(ViewState=Name.ON))))
    common = pdf.make_indirect(Dictionary(Type=Name.OCG, Name=String("Общий")))
    pdf.pages[0].obj.Resources.Properties = Dictionary(ocScreen=screen, ocCommon=common)
    pdf.Root.OCProperties = Dictionary(
        OCGs=Array([screen, common]),
        D=Dictionary(Name=String("Исходный"), BaseState=Name.ON, Order=Array([screen, common]),
                     AS=Array([Dictionary(Event=Name.Print, Category=Array([Name.Print]), OCGs=Array([screen]))])))
    doc.save(out)
    return {"size": [w, h], "screen_only": {"layer": "Только экран", "rect": [50, 50, 250, 250]},
            "common": {"layer": "Общий", "rect": [round(w - 250, 4), round(h - 250, 4), round(w - 50, 4),
                                                  round(h - 50, 4)]}}


def main():
    args = g.common_args(__doc__)
    dst = Path(args.out) / "tests" / "fixtures" / "pages"
    dst.mkdir(parents=True, exist_ok=True)
    meta = mixed(dst / "mixed-sizes.pdf")
    g.check_size(dst / "mixed-sizes.pdf", LIMIT)
    g.say("pages: mixed-sizes.pdf")
    layer_meta = layers(dst / "print-layers.pdf")
    g.check_size(dst / "print-layers.pdf", LIMIT)
    g.say("pages: print-layers.pdf")
    g.write_json(dst / "pages.json", {"generator": "tools/gen/make_pages.py",
                                      "mixed-sizes.pdf": {"pages": meta},
                                      "print-layers.pdf": layer_meta})


if __name__ == "__main__":
    main()
