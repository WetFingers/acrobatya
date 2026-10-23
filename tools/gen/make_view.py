# -*- coding: utf-8 -*-
"""make_view.py — фикстуры виджета просмотра (сессия 2.2) в tests/fixtures/view/.

Что:
  layout-mixed.pdf  — 5 страниц разного размера и поворота для раскладки, зума и рендера PageView:
                      1) Letter 612 × 792; 2) A4 с /Rotate 90 (на экране 842 × 595); 3) A4 595 × 842;
                      4) 300 × 400; 5) Letter с /Rotate 100 (MuPDF округляет до 90 — на экране 792 × 612).
                      На каждой — крупная надпись «Страница N», рамка у полей и цветной квадрат в левом
                      верхнем углу (по нему видно поворот). /PageLayout /TwoColumnRight — «разворот, титул
                      отдельно» при открытии (эталон 3.1);
  layout-expected.json — что нарисовано: размеры MediaBox, /Rotate и ожидаемый размер на экране (пункты);
  links.pdf         — 3 страницы A4 со ссылками всех видов (сессия 2.2b): на стр. 1 столбец строк, над каждой —
                      /Link: явное назначение /Dest (стр. 3, /XYZ), /GoTo по имени из /Names /Dests, /Named
                      /NextPage, /URI, /GoToR (other.pdf, стр. 2), /Launch (calc.exe), /JavaScript; на стр. 2 —
                      /Dest /Fit на стр. 1; на стр. 3 — «Итоги»;
  links.json        — ссылки: страница, вид, рамка в координатах MuPDF (y вниз), куда ведёт.
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

from pikepdf import Array, Dictionary, Name, String  # noqa: E402

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


# Ссылки links.pdf: (подпись, вид, действие или назначение — функция от страниц).
LINK_ROWS = [
    ("К странице 3 (явное назначение)", "GoTo"),
    ("К разделу «Итоги» (именованное)", "GoTo"),
    ("Следующая страница (/Named)", "Named"),
    ("Сайт example.org (/URI)", "URI"),
    ("Другой файл other.pdf (/GoToR)", "GoToR"),
    ("Запустить calc.exe (/Launch)", "Launch"),
    ("Сценарий (/JavaScript)", "JavaScript"),
]


def links(out: Path) -> list:
    doc = g.Doc("links", "Ссылки всех видов для PageView", creator="tools/gen/make_view.py")
    pdf = doc.pdf
    font = doc.font("arial")
    w, h = A4
    rows = []
    cv = doc.canvas(A4)
    cv.text(72, h - 60, "Ссылки", font, 20)
    for i, (label, _kind) in enumerate(LINK_ROWS):
        base = h - 110 - 34 * i
        cv.text(80, base, label, font, 12, rgb=(0.0, 0.25, 0.75))
        rows.append(base)
    doc.add_page(cv)
    cv = doc.canvas(A4)
    cv.text(72, h - 60, "Страница 2", font, 20)
    cv.text(80, h - 110, "Назад к началу (/Fit)", font, 12, rgb=(0.0, 0.25, 0.75))
    doc.add_page(cv)
    cv = doc.canvas(A4)
    cv.text(72, h - 60, "Страница 3", font, 20)
    cv.text(72, 700, "Итоги", font, 16)
    doc.add_page(cv)
    p1, p2, p3 = (pdf.pages[i].obj for i in range(3))
    pdf.Root.Names = Dictionary(Dests=Dictionary(Names=Array([String("itogi"),
                                                               Array([p3, Name.XYZ, 72, 720, None])])))

    def rect(base):
        return [76, round(base - 5, 2), 330, round(base + 14, 2)]

    actions = [
        ("Dest", Array([p3, Name.XYZ, 72, 700, 0])),
        ("A", Dictionary(S=Name.GoTo, D=String("itogi"))),
        ("A", Dictionary(S=Name.Named, N=Name.NextPage)),
        ("A", Dictionary(S=Name.URI, URI=String("https://example.org/akrobatya"))),
        ("A", Dictionary(S=Name.GoToR, F=String("other.pdf"), D=Array([1, Name.Fit]))),
        ("A", Dictionary(S=Name.Launch, F=String("calc.exe"))),
        ("A", Dictionary(S=Name.JavaScript, JS=String("app.alert('АкроБатя');"))),
    ]
    p1.Annots = Array()
    meta = []
    for base, (key, value), (label, kind) in zip(rows, actions, LINK_ROWS):
        r = rect(base)
        annot = Dictionary(Type=Name.Annot, Subtype=Name.Link, Rect=Array(r), Border=Array([0, 0, 0]), F=4)
        annot[Name("/" + key)] = value
        p1.Annots.append(pdf.make_indirect(annot))
        meta.append({"page": 0, "label": label, "kind": kind,
                     "rect_mupdf": [r[0], round(h - r[3], 2), r[2], round(h - r[1], 2)]})
    back = rect(h - 110)
    p2.Annots = Array([pdf.make_indirect(Dictionary(
        Type=Name.Annot, Subtype=Name.Link, Rect=Array(back), Border=Array([0, 0, 0]), F=4,
        Dest=Array([p1, Name.Fit])))])
    meta.append({"page": 1, "label": "Назад к началу (/Fit)", "kind": "GoTo",
                 "rect_mupdf": [back[0], round(h - back[3], 2), back[2], round(h - back[1], 2)]})
    targets = [{"page": 2, "point": [72, round(h - 700, 2)]}, {"page": 2, "point": [72, round(h - 720, 2)]},
               {"page": 1}, {"uri": "https://example.org/akrobatya"}, {"file": "other.pdf", "page": 1},
               {"file": "calc.exe"}, {"name": "JavaScript"}, {"page": 0}]
    for m, t in zip(meta, targets):
        m["target"] = t
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
    link_meta = links(dst / "links.pdf")
    g.check_size(dst / "links.pdf", LIMIT)
    g.write_json(dst / "links.json", {"generator": "tools/gen/make_view.py", "pdf": "links.pdf",
                                      "page_height": A4[1], "links": link_meta})
    g.say("view: links.pdf")


if __name__ == "__main__":
    main()
