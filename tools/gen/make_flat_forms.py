# -*- coding: utf-8 -*-
"""Плоские анкеты для Fill & Sign (план 5.3, «умное размещение»; расширяется в 6.4 — автоопределение полей).

Что: tests/fixtures/forms/flat/
  anketa.pdf  — анкета A4 без полей AcroForm, только линии и рамки содержимого страницы:
                подчёркивания (m l S) «Фамилия», «Имя», «Отчество», «Подпись», «Дата»; гребёнка из 8 смежных
                клеток (re S) «Дата рождения»; гребёнка «ИНН» — базовая линия с 13 засечками (12 клеток);
                две рамки разной ширины «Серия» и «Номер»; два квадрата-флажка 12 × 12;
  flat.json   — что нарисовано, в координатах MuPDF (пункты от левого верхнего угла, y вниз): у каждого поля
                вид (line, comb, box, check), рамка, линия-основание (base), клетки гребёнки, высота клетки.
Откуда: свои тексты; шрифт Arial из C:\\Windows\\Fonts.
Детерминизм: даты/XMP/ID фиксированы (_genlib.save_pdf), случайности нет.
Запуск: tools\\gen\\make_flat_forms.ps1 или "C:\\Tools\\venv-test\\Scripts\\python.exe" -X utf8 -I
tools/gen/make_flat_forms.py [--out <корень>]
"""
import sys
from pathlib import Path

sys.dont_write_bytecode = True                     # без __pycache__ в tools/gen
sys.path.insert(0, str(Path(__file__).resolve().parent))

import _genlib as g  # noqa: E402

LIMIT = 50 * 1024
W, H = g.A4
LW = 0.75                                          # толщина линий бланка


def top(y: float) -> float:
    """y PDF (вверх от низа) → y MuPDF (вниз от верха)."""
    return round(H - y, 3)


def anketa(out: Path) -> list:
    doc = g.Doc("Анкета участника", "Плоская анкета для Fill & Sign", creator="tools/gen/make_flat_forms.py")
    f = doc.font("arial")
    cv = doc.canvas()
    fields = []
    cv.text(72, 780, "Анкета участника", f, 16)

    # Подчёркивания: подпись слева, линия до правого поля; над линией свободно.
    y = 720.0
    for name in ["Фамилия", "Имя", "Отчество"]:
        cv.text(72, y, name + ":", f, 11)
        cv.line(160, y - 2, 520, y - 2, lw=LW)
        fields.append({"name": name, "kind": "line", "base": top(y - 2),
                       "rect": [160, top(y + 26), 520, top(y - 2)]})
        y -= 32

    # Гребёнка из смежных клеток: 8 рамок 18 × 22 (ДД ММ ГГГГ).
    cv.text(72, 612, "Дата рождения:", f, 11)
    x0, yb, cw, ch = 180.0, 606.0, 18.0, 22.0
    cells = []
    for i in range(8):
        cv.rect(x0 + i * cw, yb, cw, ch, stroke=(0, 0, 0), lw=LW)
        cells.append([x0 + i * cw, top(yb + ch), x0 + (i + 1) * cw, top(yb)])
    fields.append({"name": "Дата рождения", "kind": "comb", "base": top(yb), "cell_height": ch, "step": cw,
                   "rect": [x0, top(yb + ch), x0 + 8 * cw, top(yb)], "cells": cells})

    # Гребёнка-засечки: базовая линия и 13 засечек высотой 10 (12 клеток по 16).
    cv.text(72, 562, "ИНН:", f, 11)
    x0, yb, step, tick = 180.0, 558.0, 16.0, 10.0
    cv.line(x0, yb, x0 + 12 * step, yb, lw=LW)
    cells = []
    for i in range(13):
        cv.line(x0 + i * step, yb, x0 + i * step, yb + tick, lw=LW)
        if i < 12:
            cells.append([x0 + i * step, top(yb + tick), x0 + (i + 1) * step, top(yb)])
    fields.append({"name": "ИНН", "kind": "comb", "base": top(yb), "cell_height": tick, "step": step,
                   "rect": [x0, top(yb + tick), x0 + 12 * step, top(yb)], "cells": cells})

    # Две рамки разной ширины — не гребёнка.
    cv.text(72, 512, "Паспорт:", f, 11)
    cv.text(182, 536, "Серия", f, 8)
    cv.text(262, 536, "Номер", f, 8)
    cv.rect(180, 506, 80, 24, stroke=(0, 0, 0), lw=LW)
    cv.rect(260, 506, 140, 24, stroke=(0, 0, 0), lw=LW)
    fields.append({"name": "Серия", "kind": "box", "base": top(506), "rect": [180, top(530), 260, top(506)]})
    fields.append({"name": "Номер", "kind": "box", "base": top(506), "rect": [260, top(530), 400, top(506)]})

    # Флажки: квадраты 12 × 12 с подписью справа.
    yy = 470.0
    for name in ["Согласен на обработку данных", "Нужна справка"]:
        cv.rect(72, yy - 2, 12, 12, stroke=(0, 0, 0), lw=LW)
        cv.text(92, yy, name, f, 11)
        fields.append({"name": name, "kind": "check", "rect": [72, top(yy + 10), 84, top(yy - 2)]})
        yy -= 24

    # Подпись и дата — подчёркивания в одной строке.
    cv.text(72, 380, "Подпись:", f, 11)
    cv.line(140, 378, 330, 378, lw=LW)
    fields.append({"name": "Подпись", "kind": "line", "base": top(378), "rect": [140, top(406), 330, top(378)]})
    cv.text(360, 380, "Дата:", f, 11)
    cv.line(400, 378, 520, 378, lw=LW)
    fields.append({"name": "Дата", "kind": "line", "base": top(378), "rect": [400, top(406), 520, top(378)]})

    doc.add_page(cv)
    doc.save(out)
    return fields


def main():
    args = g.common_args(__doc__)
    dst = Path(args.out) / "tests" / "fixtures" / "forms" / "flat"
    dst.mkdir(parents=True, exist_ok=True)
    fields = anketa(dst / "anketa.pdf")
    g.check_size(dst / "anketa.pdf", LIMIT)
    meta = {"generator": "tools/gen/make_flat_forms.py",
            "coordinates": "points, origin top-left (MuPDF page space)",
            "line_width": LW, "page": [W, H],
            "files": {"anketa.pdf": {"fields": fields}}}
    g.write_json(dst / "flat.json", meta)
    g.say("flat forms: anketa.pdf (%d fields)" % len(fields))


if __name__ == "__main__":
    main()
