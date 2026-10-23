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
  links.json        — ссылки: страница, вид, рамка в координатах MuPDF (y вниз), куда ведёт;
  nav.pdf           — 4 страницы A4 для панелей навигации (сессия 2.4): слои /OCProperties на стр. 1 (вложенные,
                      выключенный, заблокированный, группа-подпись), вложения /EmbeddedFiles (inner.pdf, notes.txt,
                      report.docx, setup.exe) и /FileAttachment на стр. 2, назначения /Names /Dests и /Dests
                      каталога, закладки с /C и /F, переходы /Trans и /Dur, /PageLabels i–iv, /OpenAction, /DisplayDocTitle;
  nav.json          — что в nav.pdf (слои, вложения, назначения, закладки, переходы);
  present.pdf       — 3 слайда для полного экрана (сессия 2.4): /PageMode /FullScreen,
                      /NonFullScreenPageMode /UseThumbs, /Dur 1 на стр. 1, /Fade и /Box;
  present.json      — что в present.pdf.
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

from pikepdf import Array, Dictionary, Name, OutlineItem, Stream, String  # noqa: E402

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


# --- nav.pdf (сессия 2.4): панели навигации, начальный вид, полный экран ------------------------------
NAV_FILES = [  # (ключ дерева /EmbeddedFiles, описание, содержимое)
    ("inner.pdf", "Вложенный PDF", None),
    ("notes.txt", "Заметки к отчёту", "Заметки: проверить итоги.\r\n".encode("utf-8")),
    ("report.docx", "Отчёт в Word", b"PK\x03\x04" + b"not a real docx, fixture only" + b"\x00" * 16),
    ("setup.exe", "Установщик (не запускать)", b"MZ" + b"\x00" * 30 + b"fixture only, not a program"),
]
NAV_DESTS = [("chapter1", 1, "XYZ"), ("chapter2", 2, "FitH"), ("summary", 3, "Fit")]


def inner_pdf() -> bytes:
    import io

    import pikepdf
    p = pikepdf.new()
    font = p.make_indirect(Dictionary(Type=Name.Font, Subtype=Name.Type1, BaseFont=Name.Helvetica))
    content = Stream(p, b"BT /F1 24 Tf 72 720 Td (Inner PDF) Tj ET")
    p.pages.append(pikepdf.Page(Dictionary(Type=Name.Page, MediaBox=Array([0, 0, 300, 400]),
                                           Resources=Dictionary(Font=Dictionary(F1=font)),
                                           Contents=p.make_indirect(content))))
    buf = io.BytesIO()
    p.save(buf, static_id=True)
    return buf.getvalue()


def nav(out: Path) -> dict:
    """4 страницы A4: слои на стр. 1, вложения (документ и /FileAttachment на стр. 2), назначения, закладки с
    цветом и стилем, переходы /Trans и /Dur, /OpenAction на стр. 2, /DisplayDocTitle."""
    doc = g.Doc("Навигация: слои, вложения, назначения", "Панели навигации АкроБати",
                creator="tools/gen/make_view.py")
    pdf = doc.pdf
    font = doc.font("arial")
    w, h = A4
    # Стр. 1 — слои: «Сетка» (чёрный квадрат 200 × 200 в центре — по нему тест видит смену рендера),
    # «Подписи» с вложенными «Русские» и «English» (выкл.), группа «Служебные» с заблокированным
    # «Водяной знак».
    cv = doc.canvas(A4)
    cv.text(72, h - 60, "Слои", font, 20)
    cv.raw("/OC /ocGrid BDC")
    cv.rect(w / 2 - 100, h / 2 - 100, 200, 200, fill=(0, 0, 0))
    cv.raw("EMC")
    cv.raw("/OC /ocLabels BDC /OC /ocRu BDC")
    cv.text(72, 200, "Подпись по-русски", font, 14)
    cv.raw("EMC /OC /ocEn BDC")
    cv.text(72, 170, "English caption", font, 14)
    cv.raw("EMC EMC")
    cv.raw("/OC /ocWm BDC")
    cv.text(72, 120, "Водяной знак", font, 28, rgb=(0.8, 0.8, 0.8))
    cv.raw("EMC")
    doc.add_page(cv)
    for i, title in enumerate(["Глава 1", "Глава 2", "Итоги"]):
        cv = doc.canvas(A4)
        cv.text(72, h - 72, title, font, 24)
        cv.text(72, h - 110, "Страница %d из 4" % (i + 2), font, 12, rgb=(0.35, 0.35, 0.35))
        doc.add_page(cv)
    pages = [pdf.pages[i].obj for i in range(4)]

    ocgs = {key: pdf.make_indirect(Dictionary(Type=Name.OCG, Name=String(name)))
            for key, name in (("ocGrid", "Сетка"), ("ocLabels", "Подписи"), ("ocRu", "Русские"),
                              ("ocEn", "English"), ("ocWm", "Водяной знак"))}
    pages[0].Resources.Properties = Dictionary({"/" + k: v for k, v in ocgs.items()})
    order = Array([ocgs["ocGrid"], ocgs["ocLabels"], Array([ocgs["ocRu"], ocgs["ocEn"]]),
                   Array([String("Служебные"), ocgs["ocWm"]])])
    pdf.Root.OCProperties = Dictionary(
        OCGs=Array(list(ocgs.values())),
        D=Dictionary(Name=String("Исходный"), BaseState=Name.ON, Order=order, OFF=Array([ocgs["ocEn"]]),
                     Locked=Array([ocgs["ocWm"]])))

    # Вложения: дерево /EmbeddedFiles (ключи по алфавиту) и /FileAttachment на стр. 2.
    def filespec(name: str, desc: str, data: bytes):
        ef = Stream(pdf, data)
        ef.Type = Name.EmbeddedFile
        ef.Params = Dictionary(Size=len(data))
        return pdf.make_indirect(Dictionary(Type=Name.Filespec, F=String(name), UF=String(name),
                                            Desc=String(desc), EF=Dictionary(F=pdf.make_indirect(ef))))

    names = Array()
    files = []
    for key, desc, data in NAV_FILES:
        data = inner_pdf() if data is None else data
        names.append(String(key))
        names.append(filespec(key, desc, data))
        files.append({"name": key, "description": desc, "size": len(data), "source": "document"})
    csv = "Месяц;Сумма\r\nЯнварь;100\r\n".encode("utf-8")
    pages[1].Annots = Array([pdf.make_indirect(Dictionary(
        Type=Name.Annot, Subtype=Name.FileAttachment, Rect=Array([520, 770, 540, 800]), F=4,
        Contents=String("Данные к главе 1"), Name=Name.Paperclip,
        FS=filespec("data.csv", "Таблица данных", csv)))])
    files.append({"name": "data.csv", "description": "Таблица данных", "size": len(csv), "source": "annotation",
                  "page": 1})

    # Назначения: /Names /Dests (новое) и /Dests каталога (PDF 1.1).
    dest_names = Array()
    dests = []
    for name, page, fit in NAV_DESTS:
        if fit == "XYZ":
            d = Array([pages[page], Name.XYZ, 72, 770, 0])
        elif fit == "FitH":
            d = Array([pages[page], Name.FitH, 800])
        else:
            d = Array([pages[page], Name.Fit])
        dest_names.append(String(name))
        dest_names.append(d)
        dests.append({"name": name, "page": page, "fit": fit})
    pdf.Root.Names = Dictionary(EmbeddedFiles=Dictionary(Names=names), Dests=Dictionary(Names=dest_names))
    pdf.Root.Dests = Dictionary(intro=Array([pages[0], Name.XYZ, 0, h, 0]))
    dests.insert(1, {"name": "intro", "page": 0, "fit": "XYZ"})

    # Закладки: цвет и стиль (/C, /F: 1 — курсив, 2 — жирный), вложенность, /Count < 0 — закрыта.
    with pdf.open_outline() as ol:
        ch1 = OutlineItem("Глава 1", 1)
        ch2 = OutlineItem("Глава 2", 2)
        ch2.children.append(OutlineItem("Раздел 2.1", 2))
        ch2.children.append(OutlineItem("Раздел 2.2", 2))
        ch2.is_closed = False
        ch3 = OutlineItem("Итоги", 3)
        ch3.children.append(OutlineItem("Таблица", 3))
        ch3.is_closed = True
        ol.root.extend([OutlineItem("Слои", 0), ch1, ch2, ch3])
    first = pdf.Root.Outlines.First
    first.Next.C = Array([0.8, 0, 0])
    first.Next.F = 2
    first.Next.Next.F = 1

    # Переходы для полного экрана: стр. 2 — растворение 0,5 с, стр. 3 — шторка вниз 1 с и показ 3 с.
    pages[1].Trans = Dictionary(Type=Name.Trans, S=Name.Dissolve, D=0.5)
    pages[2].Trans = Dictionary(Type=Name.Trans, S=Name.Wipe, Di=270, D=1)
    pages[2].Dur = 3
    pdf.Root.PageLabels = Dictionary(Nums=Array([0, Dictionary(S=Name.r)]))  # i, ii, iii, iv
    pdf.Root.OpenAction = Array([pages[1], Name.Fit])
    pdf.Root.ViewerPreferences = Dictionary(DisplayDocTitle=True)
    doc.save(out)
    return {
        "pages": 4,
        "title": "Навигация: слои, вложения, назначения",
        "layers": [
            {"text": "Сетка", "depth": 0, "on": True, "locked": False},
            {"text": "Подписи", "depth": 0, "on": True, "locked": False},
            {"text": "Русские", "depth": 1, "on": True, "locked": False},
            {"text": "English", "depth": 1, "on": False, "locked": False},
            {"text": "Служебные", "depth": 0, "label": True},
            {"text": "Водяной знак", "depth": 1, "on": True, "locked": True},
        ],
        "grid_square_pt": [w / 2 - 100, h / 2 - 100, 200, 200],
        "attachments": files,
        "destinations": dests,
        "outline_top": ["Слои", "Глава 1", "Глава 2", "Итоги"],
        "open_action": {"page": 1, "fit": "Fit"},
        "page_labels": ["i", "ii", "iii", "iv"],
        "transitions": [{"page": 1, "type": "Dissolve", "duration": 0.5},
                        {"page": 2, "type": "Wipe", "duration": 1, "direction": 270, "dur": 3}],
    }


def present(out: Path) -> dict:
    """3 слайда A4 альбомной (сессия 2.4, полный экран): /PageMode /FullScreen, /NonFullScreenPageMode
    /UseThumbs; стр. 1 — /Dur 1 (сама листается через 1 с), стр. 2 — /Trans /Fade 0,3 с, стр. 3 — /Box наружу."""
    doc = g.Doc("Показ", "Полноэкранный режим АкроБати", creator="tools/gen/make_view.py")
    pdf = doc.pdf
    font = doc.font("arial")
    w, h = A4[1], A4[0]
    colors = [(0.15, 0.35, 0.70), (0.80, 0.30, 0.20), (0.20, 0.55, 0.30)]
    for i, rgb in enumerate(colors):
        cv = doc.canvas((w, h))
        cv.rect(0, 0, w, h, fill=rgb)
        cv.text_center(w / 2, h / 2, "Слайд %d" % (i + 1), font, 48, rgb=(1, 1, 1))
        doc.add_page(cv)
    pages = [pdf.pages[i].obj for i in range(3)]
    pages[0].Dur = 1
    pages[1].Trans = Dictionary(Type=Name.Trans, S=Name.Fade, D=0.3)
    pages[2].Trans = Dictionary(Type=Name.Trans, S=Name.Box, M=Name.O, D=0.3)
    pdf.Root.PageMode = Name.FullScreen
    pdf.Root.ViewerPreferences = Dictionary(NonFullScreenPageMode=Name.UseThumbs)
    doc.save(out)
    return {"pages": 3, "page_mode": "FullScreen", "non_full_screen": "UseThumbs",
            "transitions": [{"page": 0, "dur": 1}, {"page": 1, "type": "Fade", "duration": 0.3},
                            {"page": 2, "type": "Box", "duration": 0.3}]}


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
    nav_meta = nav(dst / "nav.pdf")
    g.check_size(dst / "nav.pdf", LIMIT)
    g.write_json(dst / "nav.json", {"generator": "tools/gen/make_view.py", "pdf": "nav.pdf", **nav_meta})
    g.say("view: nav.pdf")
    present_meta = present(dst / "present.pdf")
    g.check_size(dst / "present.pdf", LIMIT)
    g.write_json(dst / "present.json", {"generator": "tools/gen/make_view.py", "pdf": "present.pdf", **present_meta})
    g.say("view: present.pdf")


if __name__ == "__main__":
    main()
