# -*- coding: utf-8 -*-
"""make_demo.py — показательные PDF для сцен и --smoke: tests/fixtures/demo/*.pdf + demo.json.

Что:
  report.pdf      — 12 страниц: обложка с фото, оглавление со ссылками, 5 глав (заголовки, абзацы,
                    векторная диаграмма, таблица, картинки JPEG/PNG с альфой), колонтитулы,
                    закладки (главы и разделы), метки страниц i, ii, 1…10;
  form.pdf        — «Заявка на пропуск»: поля с кириллицей (Tx, Ch, Btn, Sig), дата и число с JS;
  comments.pdf    — 3 страницы текста с комментариями двух авторов: выделение, заметка с ответом
                    (/IRT), подчёркивание, зачёркивание и вставка, надпись, рамка, рисунок, штамп;
  mixed-sizes.pdf — 7 страниц разных размеров: A4, A4 альбомная, A3, Letter, A5, /Rotate 90, /CropBox.
Откуда: _genlib/_forms (pikepdf), аннотации — MuPDF (mutool run tools/gen/annots.js), картинки —
tests/fixtures/images (make_images.py).
Детерминизм: тексты и раскладка — константы и random.Random(seed), даты аннотаций нормализуются,
сохранение через _genlib.save_pdf.
Запуск: "C:\\Tools\\venv-test\\Scripts\\python.exe" -X utf8 -I tools/gen/make_demo.py [--out <корень>]
"""
import random
import sys
from pathlib import Path

sys.dont_write_bytecode = True                     # без __pycache__ в tools/gen
sys.path.insert(0, str(Path(__file__).resolve().parent))

import pikepdf  # noqa: E402
from PIL import Image  # noqa: E402
from pikepdf import Array, Dictionary, Name, OutlineItem, Stream, String  # noqa: E402

import _forms as fm  # noqa: E402
import _genlib as g  # noqa: E402
import _texts as tx  # noqa: E402

FIXED_MS = 1767268800000
W, H = g.A4


def images_dir(root: Path) -> Path:
    d = root / "tests" / "fixtures" / "images"
    return d if (d / "photo.jpg").exists() else g.REPO / "tests" / "fixtures" / "images"


def jpeg_file_xobject(pdf, path: Path):
    im = Image.open(path)
    st = Stream(pdf, path.read_bytes())
    st.Type = Name.XObject
    st.Subtype = Name.Image
    st.Width, st.Height = im.size
    st.ColorSpace = Name.DeviceRGB if im.mode == "RGB" else Name.DeviceGray
    st.BitsPerComponent = 8
    st.Filter = Name.DCTDecode
    return pdf.make_indirect(st)


def png_xobject(pdf, path: Path):
    im = Image.open(path)
    if im.mode == "RGBA":
        return g.flate_xobject(pdf, im.convert("RGB"), smask=im.getchannel("A"))
    return g.flate_xobject(pdf, im.convert("RGB"))


# --- report.pdf -------------------------------------------------------------------

CHAPTERS = [
    ("1. Просмотр документов", ["1.1 Масштаб и страницы", "1.2 Поиск"]),
    ("2. Комментарии", ["2.1 Заметки", "2.2 Выделение"]),
    ("3. Формы", ["3.1 Поля", "3.2 Расчёты"]),
    ("4. Подписи", ["4.1 Сертификаты", "4.2 Проверка"]),
    ("5. Распознавание", ["5.1 Сканы", "5.2 Языки"]),
]


def header_footer(cv, f, label):
    cv.line(56, H - 50, W - 56, H - 50, rgb=(0.7, 0.7, 0.75), lw=0.5)
    cv.text(56, H - 44, "Оттиск — демонстрационный отчёт", f, 8, rgb=(0.4, 0.4, 0.45))
    cv.line(56, 50, W - 56, 50, rgb=(0.7, 0.7, 0.75), lw=0.5)
    cv.text(W - 56 - f.width(label, 9), 36, label, f, 9, rgb=(0.3, 0.3, 0.35))


def bar_chart(cv, f, x, y, w, h, vals, labels):
    cv.rect(x, y, w, h, stroke=(0.8, 0.8, 0.85), lw=0.5)
    m = max(vals)
    bw = w / (len(vals) * 1.5 + 0.5)
    for i, (v, lab) in enumerate(zip(vals, labels)):
        bx = x + bw * 0.5 + i * bw * 1.5
        bh = (h - 30) * v / m
        cv.rect(bx, y + 18, bw, bh, fill=(0.2, 0.47, 0.78))
        cv.text_center(bx + bw / 2, y + 22 + bh, str(v), f, 8)
        cv.text_center(bx + bw / 2, y + 6, lab, f, 8)


def table(cv, f, fb, x, y, cols, rows):
    rh = 18
    widths = [c[1] for c in cols]
    tw = sum(widths)
    cv.rect(x, y - rh, tw, rh, fill=(0.87, 0.91, 0.96))
    cx = x
    for (name, wdt) in cols:
        cv.text(cx + 4, y - 13, name, fb, 9)
        cx += wdt
    for r, row in enumerate(rows, start=1):
        cx = x
        for val, wdt in zip(row, widths):
            cv.text(cx + 4, y - rh * r - 13, val, f, 9)
            cx += wdt
    for r in range(len(rows) + 2):
        cv.line(x, y - rh * r, x + tw, y - rh * r, rgb=(0.6, 0.6, 0.65), lw=0.5)
    cx = x
    for wdt in widths + [0]:
        cv.line(cx, y, cx, y - rh * (len(rows) + 1), rgb=(0.6, 0.6, 0.65), lw=0.5)
        cx += wdt
    return y - rh * (len(rows) + 1)


def report(out: Path, imgs: Path):
    doc = g.Doc("Демонстрационный отчёт", "tests/fixtures/demo/report.pdf", creator="tools/gen/make_demo.py")
    pdf = doc.pdf
    f = doc.font("times")
    fb = doc.font("arialbd")
    fa = doc.font("arial")
    photo = jpeg_file_xobject(pdf, imgs / "photo.jpg")
    chart = png_xobject(pdf, imgs / "chart.png")
    logo = png_xobject(pdf, imgs / "logo.png")
    rng = random.Random(g.SEED)
    # обложка
    cv = doc.canvas()
    cv.rect(0, H - 260, W, 260, fill=(0.11, 0.28, 0.55))
    cv.text(56, H - 150, "Годовой отчёт", fb, 34, rgb=(1, 1, 1))
    cv.text(56, H - 190, "Оттиск — PDF для всей семьи", fa, 18, rgb=(0.85, 0.9, 1))
    cv.image(photo, 56, 230, W - 112, (W - 112) * 2 / 3)
    cv.image(logo, W - 56 - 90, H - 240, 80, 80)
    cv.text(56, 120, "Демонстрационный документ для сцен и проверки «--smoke».", fa, 11, rgb=(0.3, 0.3, 0.3))
    cv.text(56, 100, "2026", fb, 14)
    doc.add_page(cv)
    # оглавление — ссылки добавим после того, как появятся страницы
    toc_cv = doc.canvas()
    toc_cv.text(56, H - 100, "Содержание", fb, 22)
    toc_lines = []
    y = H - 150
    for i, (ch, subs) in enumerate(CHAPTERS):
        target = 2 + i * 2                                   # индекс страницы главы
        label = str(target - 1)
        toc_cv.text(56, y, ch, fa, 13)
        dots_x = 56 + fa.width(ch, 13) + 6
        toc_cv.text(dots_x, y, "." * int((W - 112 - (dots_x - 56) - 30) / fa.width(".", 13)), fa, 13,
                    rgb=(0.6, 0.6, 0.6))
        toc_cv.text(W - 56 - fa.width(label, 13), y, label, fa, 13)
        toc_lines.append((y, target))
        y -= 28
    header_footer(toc_cv, fa, "ii")
    doc.add_page(toc_cv)
    # главы
    for i, (ch, subs) in enumerate(CHAPTERS):
        for half in (0, 1):
            cv = doc.canvas()
            pno = 2 + i * 2 + half
            header_footer(cv, fa, str(pno - 1))
            y = H - 90
            if half == 0:
                cv.text(56, y, ch, fb, 20, rgb=(0.11, 0.28, 0.55))
                y -= 34
            sub = subs[half]
            cv.text(56, y, sub, fb, 14)
            y -= 24
            pool = list(tx.RU)
            rng.shuffle(pool)
            for k in range(3):
                para = " ".join(pool[k * 2:k * 2 + 2]) + " " + tx.sentences(rng, 2)
                y = cv.paragraph(56, y, W - 112, para, f, 11.5, leading=15) - 8
            if (i + half) % 3 == 0:
                bar_chart(cv, fa, 56, y - 170, W - 112, 160, [12, 30, 22, 45, 38], ["янв", "фев", "мар", "апр", "май"])
                y -= 185
            elif (i + half) % 3 == 1:
                y = table(cv, fa, fb, 56, y - 4, [("Функция", 180), ("Статус", 120), ("Сессия", 100)],
                          [("Просмотр", "готово", "1.1"), ("Комментарии", "в работе", "5.1"),
                           ("Формы", "план", "6.1"), ("Подписи", "план", "4.1")]) - 16
            else:
                cv.image(chart, 56, y - 200, 320, 200)
                y -= 214
            if y > 160:
                para = " ".join(pool[6:8])
                y = cv.paragraph(56, y, W - 112, para, f, 11.5, leading=15)
            doc.add_page(cv)
    # ссылки оглавления
    toc_page = pdf.pages[1]
    toc_page.obj.Annots = Array()
    for (yy, target) in toc_lines:
        dest = Array([pdf.pages[target].obj, Name.XYZ, None, H - 60, None])
        toc_page.obj.Annots.append(pdf.make_indirect(Dictionary(
            Type=Name.Annot, Subtype=Name.Link, Rect=Array([56, yy - 4, W - 56, yy + 14]),
            Border=Array([0, 0, 0]), Dest=dest, F=4)))
    # закладки
    with pdf.open_outline() as ol:
        ol.root.append(OutlineItem("Обложка", 0))
        ol.root.append(OutlineItem("Содержание", 1))
        for i, (ch, subs) in enumerate(CHAPTERS):
            item = OutlineItem(ch, 2 + i * 2)
            for half, sub in enumerate(subs):
                item.children.append(OutlineItem(sub, 2 + i * 2 + half))
            ol.root.append(item)
    pdf.Root.PageLabels = Dictionary(Nums=Array([0, Dictionary(S=Name.r), 2, Dictionary(S=Name.D)]))
    pdf.Root.PageMode = Name.UseOutlines
    doc.save(out, objstm=True)
    return len(pdf.pages)


# --- form.pdf ------------------------------------------------------------------------

def form(out: Path):
    doc = g.Doc("Заявка на пропуск", "tests/fixtures/demo/form.pdf", creator="tools/gen/make_demo.py")
    f = doc.font("arial")
    fb = doc.font("arialbd")
    frm = fm.Form(doc, cyr_font=f)
    cv = doc.canvas()
    cv.rect(0, H - 90, W, 90, fill=(0.93, 0.95, 0.98))
    cv.text(56, H - 55, "Заявка на пропуск", fb, 22)
    cv.text(56, H - 75, "Заполните поля и подпишите заявку.", f, 10, rgb=(0.35, 0.35, 0.4))
    rows = [("Фамилия, имя, отчество", 22), ("Отдел", 22), ("Дата визита", 22), ("Число гостей", 22),
            ("Нужна парковка", 16), ("Цель визита", 60), ("Подпись", 50)]
    y = H - 130
    rects = []
    for lab, hh in rows:
        cv.text(56, y - 15, lab, f, 11)
        rects.append([230, y - hh, 540, y])
        y -= hh + 16
    page = doc.add_page(cv)
    frm.text(page, "fio", rects[0], "Иванова Мария Петровна", tu="ФИО", cyr=True, size=11)
    frm.combo(page, "dept", rects[1], ["Бухгалтерия", "Отдел кадров", "Склад"], "Склад", tu="Отдел", cyr=True)
    frm.text(page, "visit_date", rects[2], "15.01.2026", tu="Дата визита",
             aa={"F": 'AFDate_FormatEx("dd.mm.yyyy");', "K": 'AFDate_KeystrokeEx("dd.mm.yyyy");'})
    frm.text(page, "guests", rects[3], "2", tu="Число гостей",
             aa={"F": 'AFNumber_Format(0, 0, 0, 0, "", true);', "K": 'AFNumber_Keystroke(0, 0, 0, 0, "", true);',
                 "V": "AFRange_Validate(true, 0, true, 10);"})
    x0, y0, _, y1 = rects[4]
    frm.checkbox(page, "parking", [x0, y0, x0 + (y1 - y0), y1], checked=False, tu="Нужна парковка")
    frm.text(page, "purpose", rects[5], "Совещание по проекту\nи экскурсия", tu="Цель визита", cyr=True,
             ff=fm.FF_MULTILINE, size=11)
    frm.signature(page, "signature", rects[6], tu="Подпись заявителя")
    frm.finish()
    doc.save(out)


# --- comments.pdf -------------------------------------------------------------------

def comments(out: Path, tmp: Path):
    doc = g.Doc("Документ с комментариями", "tests/fixtures/demo/comments.pdf", creator="tools/gen/make_demo.py")
    f = doc.font("times")
    fb = doc.font("arialbd")
    layout = []                                  # (page, text, x, y, size)
    rng = random.Random(g.SEED + 3)
    for p in range(3):
        cv = doc.canvas()
        cv.text(56, H - 80, "Глава %d. Черновик для обсуждения" % (p + 1), fb, 18)
        y = H - 120
        pool = list(tx.RU)
        rng.shuffle(pool)
        if p == 0:
            pool.insert(0, tx.RU[1])             # гарантируем фразы для комментариев
        for k in range(5):
            para = " ".join(pool[k * 2:k * 2 + 2])
            for line in g.wrap(para, f, 12, W - 112):
                cv.text(56, y, line, f, 12)
                layout.append((p, line, 56, y, 12))
                y -= 17
            y -= 10
        doc.add_page(cv)

    def find(page, phrase):
        for (p, line, x, y, s) in layout:
            if p == page and phrase in line:
                i = line.index(phrase)
                x0 = x + f.width(line[:i], s)
                return [x0, y + f.descent(s), x0 + f.width(phrase, s), y + f.ascent(s)]
        raise RuntimeError("нет фразы %r на странице %d" % (phrase, page))

    anna, boris = "Анна Смирнова", "Борис Петров"
    # фразы из первой строки абзацев — берём первые слова строк раскладки
    lines_p = {p: [l for (pp, l, *_r) in layout if pp == p] for p in range(3)}

    def phrase(p, idx, words=3):
        return " ".join(lines_p[p][idx].split()[:words])

    h1 = phrase(0, 0, 4)
    u1 = phrase(0, 3, 3)
    s2 = phrase(1, 1, 2)
    spec = {"fixed_ms": FIXED_MS, "annots": [
        {"page": 0, "type": "Highlight", "nm": "demo-hl-1", "quads": [find(0, h1)],
         "rect": find(0, h1), "color": [1, 0.9, 0.2], "author": anna, "contents": "Проверить формулировку"},
        {"page": 0, "type": "Text", "nm": "demo-note-1", "rect": [W - 50, H - 140, W - 30, H - 120],
         "icon": "Comment", "color": [1, 0.85, 0.2], "author": anna, "contents": "Нужна ссылка на источник."},
        {"page": 0, "type": "Text", "nm": "demo-reply-1", "rect": [W - 50, H - 140, W - 30, H - 120],
         "icon": "Comment", "color": [1, 0.85, 0.2], "author": boris, "contents": "Добавлю в следующей версии."},
        {"page": 0, "type": "Underline", "nm": "demo-ul-1", "quads": [find(0, u1)], "rect": find(0, u1),
         "color": [0, 0.5, 0.9], "author": boris, "contents": "Подчеркнуть в итоговом тексте"},
        {"page": 1, "type": "StrikeOut", "nm": "demo-so-1", "quads": [find(1, s2)], "rect": find(1, s2),
         "color": [0.9, 0, 0], "author": anna, "contents": "Убрать"},
        {"page": 1, "type": "Caret", "nm": "demo-caret-1",
         "rect": [find(1, s2)[2] - 2, find(1, s2)[1] - 6, find(1, s2)[2] + 10, find(1, s2)[1] + 6],
         "color": [0, 0, 0.9], "author": anna, "contents": "вставить «быстро»"},
        {"page": 1, "type": "FreeText", "nm": "demo-ft-1", "rect": [330, 120, 540, 180],
         "da": ["Helv", 10, [0.6, 0, 0]], "border_width": 1, "author": boris,
         "contents": "Надпись: обсудить на встрече"},
        {"page": 1, "type": "Square", "nm": "demo-sq-1", "rect": [50, H - 300, W - 50, H - 200],
         "color": [0.9, 0.4, 0], "border_width": 1.5, "author": boris, "contents": "Этот блок переписать"},
        {"page": 2, "type": "Ink", "nm": "demo-ink-1", "rect": [380, 120, 540, 200],
         "ink": [[[390 + t * 4, 150 + 25 * ((t % 6) / 5.0) * (1 if t % 2 else -1)] for t in range(36)]],
         "color": [0.1, 0.2, 0.7], "border_width": 2, "author": anna, "contents": "Росчерк"},
        {"page": 2, "type": "Stamp", "nm": "demo-stamp-1", "rect": [380, 220, 540, 270], "icon": "Draft",
         "color": [0.8, 0, 0], "author": anna, "contents": "Черновик"},
        {"page": 2, "type": "Circle", "nm": "demo-circle-1", "rect": [56, 120, 200, 200],
         "color": [0, 0.6, 0.3], "border_width": 2, "author": boris, "contents": "Здесь будет схема"},
    ]}
    base = tmp / "comments-base.pdf"
    doc.save(base)
    sp = tmp / "comments-spec.json"
    g.write_json(sp, spec)
    mid = tmp / "comments-mid.pdf"
    g.run([g.mutool_path(), "run", Path(__file__).resolve().parent / "annots.js", base, sp, mid])
    pdf = pikepdf.open(mid)
    by_nm = {}
    for page in pdf.pages:
        for a in page.obj.get("/Annots", []):
            nm = a.get("/NM")
            if isinstance(nm, Name):
                a.NM = String(str(nm)[1:])
            by_nm[str(a.get("/NM", ""))] = a
    reply = by_nm["demo-reply-1"]
    reply.IRT = by_nm["demo-note-1"]
    reply.RT = Name.R
    g.save_pdf(pdf, out, title="Документ с комментариями", subject="tests/fixtures/demo/comments.pdf",
               creator="tools/gen/make_demo.py")
    return len(spec["annots"])


# --- mixed-sizes.pdf ---------------------------------------------------------------

def mixed(out: Path):
    doc = g.Doc("Страницы разных размеров", "tests/fixtures/demo/mixed-sizes.pdf", creator="tools/gen/make_demo.py")
    fb = doc.font("arialbd")
    f = doc.font("arial")
    pages = [("A4 книжная", g.A4, 0, None), ("A4 альбомная", (g.A4[1], g.A4[0]), 0, None),
             ("A3", g.A3, 0, None), ("Letter", g.LETTER, 0, None), ("A5", g.A5, 0, None),
             ("A4, /Rotate 90", g.A4, 90, None), ("A4, /CropBox с полями 72 pt", g.A4, 0, 72)]
    info = []
    for i, (name, size, rot, crop) in enumerate(pages, start=1):
        cv = doc.canvas(size)
        w, h = size
        cv.rect(10, 10, w - 20, h - 20, stroke=(0.2, 0.4, 0.8), lw=2)
        cv.line(10, 10, w - 10, h - 10, rgb=(0.85, 0.85, 0.9), lw=0.5)
        cv.line(10, h - 10, w - 10, 10, rgb=(0.85, 0.85, 0.9), lw=0.5)
        cv.text_center(w / 2, h / 2 + 10, "%d. %s" % (i, name), fb, 24)
        cv.text_center(w / 2, h / 2 - 20, "%.1f × %.1f pt" % (w, h), f, 14, rgb=(0.3, 0.3, 0.3))
        cv.text(20, h - 36, "верх страницы ↑", f, 12)
        extra = None
        if crop:
            extra = {"/CropBox": Array([crop, crop, round(w - crop, 4), round(h - crop, 4)])}
        doc.add_page(cv, rotate=rot, extra=extra)
        info.append({"page": i, "name": name, "mediabox": [0, 0, round(w, 4), round(h, 4)], "rotate": rot,
                     "cropbox_inset": crop or 0})
    doc.save(out)
    return info


def main():
    args = g.common_args(__doc__)
    root = Path(args.out)
    dst = root / "tests" / "fixtures" / "demo"
    dst.mkdir(parents=True, exist_ok=True)
    tmp = g.tmp_dir(args, "demo")
    imgs = images_dir(root)
    n_report = report(dst / "report.pdf", imgs)
    form(dst / "form.pdf")
    n_comments = comments(dst / "comments.pdf", tmp)
    sizes = mixed(dst / "mixed-sizes.pdf")
    g.write_json(dst / "demo.json", {
        "generator": "tools/gen/make_demo.py",
        "report.pdf": {"pages": n_report, "outline_top": 2 + len(CHAPTERS), "page_labels": ["i", "ii", "1..10"],
                       "links": len(CHAPTERS), "images": ["photo.jpg (DCT)", "chart.png (Flate)",
                                                          "logo.png (Flate + SMask)"]},
        "form.pdf": {"pages": 1, "fields": ["fio", "dept", "visit_date", "guests", "parking", "purpose",
                                            "signature"]},
        "comments.pdf": {"pages": 3, "annots": n_comments, "authors": ["Анна Смирнова", "Борис Петров"],
                         "reply": {"nm": "demo-reply-1", "irt": "demo-note-1"}},
        "mixed-sizes.pdf": {"pages": len(sizes), "pages_info": sizes},
    })
    for n in ("report.pdf", "form.pdf", "comments.pdf", "mixed-sizes.pdf"):
        g.say("demo: %-16s %7d байт" % (n, (dst / n).stat().st_size))


if __name__ == "__main__":
    main()
