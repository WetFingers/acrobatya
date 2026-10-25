# -*- coding: utf-8 -*-
"""План этажа с масштабом документа для измерений (план 5.5; эталон 3.4 «Измерения»; comment-29, comment-30).

Что: tests/fixtures/annots/
  measure-viewport.pdf — A4: план этажа (стены — обводка re и отрезки m l S, лестница — диагональ, дверь — дуга
                         Безье) в видовом экране /VP «План» с /Measure /RL «1 cm = 1 m» (метры, 0,01) и узел в
                         экране «Узел» «1 in = 10 ft» (футы, 0,1); две готовые пометки-измерения, как пишет
                         Acrobat (Line /IT /LineDimension с /Cap и Polygon /IT /PolygonDimension со своим
                         /Measure), без /AP;
  measure.json         — ожидания в координатах MuPDF (пункты от левого верхнего угла, y вниз): экраны (рамка,
                         единицы, единиц на пункт, точность), вершины путей (привязка), углы рамки диагонали и
                         управляющие точки дуги (не вершины), значения пометок.
Откуда: свои тексты; шрифт Arial из C:\\Windows\\Fonts.
Детерминизм: даты/XMP/ID фиксированы (_genlib.save_pdf), случайности нет.
Запуск: tools\\gen\\make_all.ps1 или "C:\\Tools\\venv-test\\Scripts\\python.exe" -X utf8 -I
tools/gen/make_measure.py [--out <корень>]
"""
import sys
from pathlib import Path

sys.dont_write_bytecode = True                     # без __pycache__ в tools/gen
sys.path.insert(0, str(Path(__file__).resolve().parent))

from pikepdf import Array, Dictionary, Name, String  # noqa: E402

import _genlib as g  # noqa: E402

LIMIT = 60 * 1024
W, H = g.A4
CM = 72 / 2.54                                     # пунктов в сантиметре
PLAN_UPP = 1 / CM                                  # «1 cm = 1 m»: метров на пункт
NODE_UPP = 10 / 72                                 # «1 in = 10 ft»: футов на пункт


def top(y: float) -> float:
    """y PDF (вверх от низа) → y MuPDF (вниз от верха)."""
    return round(H - y, 3)


def pt(x: float, y: float) -> list:
    return [round(x, 3), top(y)]


def number_format(unit: str, c: float, d: int) -> Dictionary:
    return Dictionary(Type=Name.NumberFormat, U=String(unit), C=round(c, 9), D=d)


def measure(ratio: str, unit: str, upp: float, d: int) -> Dictionary:
    return Dictionary(Type=Name.Measure, Subtype=Name.RL, R=String(ratio),
                      X=Array([number_format(unit, upp, d)]),
                      D=Array([number_format(unit, 1, d)]),
                      A=Array([number_format("sq " + unit, 1, d)]),
                      O=Array([0, 0]))


def plan(out: Path) -> dict:
    doc = g.Doc("План этажа", "Масштаб документа для измерений", creator="tools/gen/make_measure.py")
    f = doc.font("arial")
    cv = doc.canvas()
    vertices = []          # вершины и концы путей (MuPDF)
    not_vertices = []      # точки рядом с путями, которые вершинами не являются

    cv.text(72, 800, "План этажа, масштаб 1:100", f, 14)

    # Наружные стены — прямоугольник обводкой (re S): четыре угла.
    x0, y0, x1, y1 = 80.0, 340.0, 520.0, 740.0
    cv.rect(x0, y0, x1 - x0, y1 - y0, stroke=(0, 0, 0), lw=2)
    vertices += [pt(x0, y0), pt(x1, y0), pt(x1, y1), pt(x0, y1)]

    # Перегородки — отрезки (m l S).
    cv.line(300, 540, 300, 740, lw=1.5)
    cv.line(80, 540, 300, 540, lw=1.5)
    vertices += [pt(300, 540), pt(300, 740), pt(80, 540)]

    # Лестница — диагональ: концы — вершины, другие углы её рамки — нет.
    cv.line(320, 380, 480, 460, lw=1)
    vertices += [pt(320, 380), pt(480, 460)]
    not_vertices += [pt(320, 460), pt(480, 380)]

    # Дверь — дуга Безье: концы — вершины, управляющие точки — нет.
    cv.raw("q 0 0 0 RG 1 w 380 540 m 380 570 400 590 430 590 c S Q")
    vertices += [pt(380, 540), pt(430, 590)]
    not_vertices += [pt(380, 570), pt(400, 590)]

    cv.text(150, 640, "Комната 1", f, 11)
    cv.text(380, 640, "Комната 2", f, 11)
    cv.text(120, 300, "Узел А (1 in = 10 ft)", f, 10)

    # Узел — прямоугольник 144 × 72 пт: 20 × 10 ft.
    cv.rect(120, 140, 144, 72, stroke=(0, 0, 0), lw=1)
    vertices += [pt(120, 140), pt(264, 140), pt(264, 212), pt(120, 212)]

    viewports = Array([
        Dictionary(Type=Name.Viewport, BBox=Array([60, 320, 540, 760]), Name=String("Plan"),
                   Measure=measure("1 cm = 1 m", "m", PLAN_UPP, 100)),
        Dictionary(Type=Name.Viewport, BBox=Array([100, 100, 300, 280]), Name=String("Node A"),
                   Measure=measure("1 in = 10 ft", "ft", NODE_UPP, 10)),
    ])
    page = doc.add_page(cv, extra={"/VP": viewports})

    # Готовые измерения (как пишет Acrobat): расстояние по нижней стене и площадь комнаты 1.
    pdf = doc.pdf
    line = pdf.make_indirect(Dictionary(
        Type=Name.Annot, Subtype=Name.Line, Rect=Array([76, 310, 524, 330]), L=Array([80, 320, 520, 320]),
        IT=Name.LineDimension, Cap=True, Contents=String("15.52 m"), C=Array([1, 0, 0]),
        LE=Array([Name.OpenArrow, Name.OpenArrow]), NM=String("measure-distance-1"), T=String("Acrobat"),
        F=4, Measure=measure("1 cm = 1 m", "m", PLAN_UPP, 100)))
    area = pdf.make_indirect(Dictionary(
        Type=Name.Annot, Subtype=Name.Polygon, Rect=Array([78, 538, 302, 742]),
        Vertices=Array([80, 540, 300, 540, 300, 740, 80, 740]), IT=Name.PolygonDimension,
        Contents=String("54.76 sq m"), C=Array([0, 0, 1]), NM=String("measure-area-1"), T=String("Acrobat"),
        F=4, Measure=measure("1 cm = 1 m", "m", PLAN_UPP, 100)))
    page.obj.Annots = Array([line, area])
    doc.save(out)

    distance = 440 * PLAN_UPP
    room = (220 * PLAN_UPP) * (200 * PLAN_UPP)
    return {
        "viewports": [
            {"name": "Plan", "bbox": [60, top(760), 540, top(320)], "ratio": "1 cm = 1 m", "unit": "m",
             "units_per_point": PLAN_UPP, "precision": 2, "inside": pt(200, 600)},
            {"name": "Node A", "bbox": [100, top(280), 300, top(100)], "ratio": "1 in = 10 ft", "unit": "ft",
             "units_per_point": NODE_UPP, "precision": 1, "inside": pt(150, 180)},
        ],
        "outside": pt(400, 200),
        "vertices": vertices,
        "not_vertices": not_vertices,
        "measurements": [
            {"nm": "measure-distance-1", "kind": "distance", "value": round(distance, 4), "unit": "m",
             "caption": "15.52 m"},
            {"nm": "measure-area-1", "kind": "area", "value": round(room, 4), "unit": "sq m",
             "caption": "54.76 sq m"},
        ],
    }


def main():
    args = g.common_args(__doc__)
    dst = Path(args.out) / "tests" / "fixtures" / "annots"
    dst.mkdir(parents=True, exist_ok=True)
    expected = plan(dst / "measure-viewport.pdf")
    g.check_size(dst / "measure-viewport.pdf", LIMIT)
    meta = {"generator": "tools/gen/make_measure.py",
            "coordinates": "points, origin top-left (MuPDF page space)",
            "page": [W, H],
            "files": {"measure-viewport.pdf": expected}}
    g.write_json(dst / "measure.json", meta)
    g.say("measure: measure-viewport.pdf (%d vertices)" % len(expected["vertices"]))


if __name__ == "__main__":
    main()
