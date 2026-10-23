# -*- coding: utf-8 -*-
"""make_annots.py — матрица аннотаций 28 подтипов: tests/fixtures/annots/{matrix-28.pdf, annots-all.pdf, annots.json}.

Что: одна страница A4, сетка 4×7, в каждой ячейке подпись подтипа и одна аннотация
(ровно 28 аннотаций — по одной на подтип ISO 32000-2, табл. 171). Popup принадлежит
Text из ячейки 1, его прямоугольник — в ячейке 15. TrapNet — последний в /Annots
(требование стандарта).
Откуда: страница и подписи — _genlib (Arial, Type0); 18 подтипов создаёт MuPDF
(mutool run tools/gen/annots.js, внешний вид — pdf_update_appearance); Link — MuPDF
createLink; Widget, Screen, PrinterMark, TrapNet, Watermark, 3D, RichMedia, Projection,
Movie и словари Sound/Link дописывает pikepdf (внешний вид — свой /AP или нет, см. annots.json:
поле "appearance" = mupdf | custom | none).
annots-all.pdf — байт в байт копия matrix-28.pdf под именем из плана (сессии 5.1, 5.2; там же
«tools/gen/annots_matrix.cpp» — эту роль выполняют make_annots.py + annots.js); Git хранит один blob.
Детерминизм: даты аннотаций и вложений — 2026-01-01T12:00:00Z, /NM фиксированы,
сохранение через _genlib.save_pdf (deterministic_id, фиксированные /Info и XMP).
Запуск: "C:\\Tools\\venv-test\\Scripts\\python.exe" -X utf8 -I tools/gen/make_annots.py [--out <корень>]
"""
import math
import sys
from pathlib import Path

sys.dont_write_bytecode = True                     # без __pycache__ в tools/gen
sys.path.insert(0, str(Path(__file__).resolve().parent))

import pikepdf  # noqa: E402
from pikepdf import Array, Dictionary, Name, Stream, String  # noqa: E402

import _genlib as g  # noqa: E402

FIXED_MS = 1767268800000          # 2026-01-01T12:00:00Z в миллисекундах (для JS MuPDF)

SUBTYPES = [
    ("Text", "заметка"), ("Link", "ссылка"), ("FreeText", "надпись"), ("Line", "линия"),
    ("Square", "прямоугольник"), ("Circle", "эллипс"), ("Polygon", "многоугольник"),
    ("PolyLine", "ломаная"), ("Highlight", "выделение"), ("Underline", "подчёркивание"),
    ("Squiggly", "волнистая черта"), ("StrikeOut", "зачёркивание"), ("Caret", "знак вставки"),
    ("Ink", "рисунок от руки"), ("Popup", "всплывающее окно"), ("FileAttachment", "вложение"),
    ("Sound", "звук"), ("Stamp", "штамп"), ("Redact", "скрытие"), ("Widget", "виджет поля"),
    ("Screen", "экран (медиа)"), ("PrinterMark", "метка печати"), ("TrapNet", "сеть треппинга"),
    ("Watermark", "водяной знак"), ("3D", "3D"), ("RichMedia", "мультимедиа"),
    ("Projection", "проекция (PDF 2.0)"), ("Movie", "фильм"),
]

MUPDF_TYPES = {"Text", "FreeText", "Line", "Square", "Circle", "Polygon", "PolyLine", "Highlight",
               "Underline", "Squiggly", "StrikeOut", "Caret", "Ink", "FileAttachment", "Sound",
               "Stamp", "Redact", "Link"}
MARKUP_TEXT = {"Highlight", "Underline", "Squiggly", "StrikeOut", "Redact"}

COLS, ROWS = 4, 7
MARGIN = 28.0
TOP = 56.0


def cells(w, h):
    cw = (w - 2 * MARGIN) / COLS
    ch = (h - MARGIN - TOP) / ROWS
    out = []
    for i in range(COLS * ROWS):
        r, c = divmod(i, COLS)
        x0 = MARGIN + c * cw
        y1 = h - TOP - r * ch
        out.append((x0, y1 - ch, x0 + cw, y1))
    return out


def build_base(doc, cellr):
    f = doc.font("arial")
    fb = doc.font("arialbd")
    cv = doc.canvas()
    cv.text(MARGIN, g.A4[1] - 36, "Матрица аннотаций: 28 подтипов (ISO 32000-2, табл. 171)", fb, 14)
    sample = {}
    for i, ((st, ru), (x0, y0, x1, y1)) in enumerate(zip(SUBTYPES, cellr)):
        cv.rect(x0, y0, x1 - x0, y1 - y0, stroke=(0.75, 0.75, 0.8), lw=0.5)
        cv.text(x0 + 5, y1 - 13, "%02d %s" % (i + 1, st), fb, 9)
        cv.text(x0 + 5, y1 - 24, ru, f, 7.5, rgb=(0.3, 0.3, 0.35))
        if st in MARKUP_TEXT:
            s = "Образец текста" if st != "Redact" else "Секретные данные"
            tx, ty, size = x0 + 10, y0 + 40, 11
            cv.text(tx, ty, s, f, size)
            wdt = f.width(s, size)
            sample[st] = [tx, ty + f.descent(size), tx + wdt, ty + f.ascent(size)]
    doc.add_page(cv)
    return sample


def inner(c, pad=10, top=30):
    x0, y0, x1, y1 = c
    return [x0 + pad, y0 + pad, x1 - pad, y1 - top]


def mupdf_specs(cellr, sample):
    sp = []
    auth = "Оттиск"
    for i, (st, ru) in enumerate(SUBTYPES):
        if st not in MUPDF_TYPES:
            continue
        x0, y0, x1, y1 = inner(cellr[i])
        cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
        nm = "ottisk-%02d-%s" % (i + 1, st.lower())
        s = {"page": 0, "type": st, "nm": nm, "author": auth,
             "contents": "Аннотация %s (%s)" % (st, ru)}
        if st == "Text":
            s.update(rect=[x0, y1 - 20, x0 + 20, y1], icon="Comment", color=[1, 0.85, 0.2],
                     popup=inner(cellr[14], pad=8, top=28), open=True,
                     contents="Заметка с всплывающим окном в ячейке 15")
        elif st == "Link":
            s = {"page": 0, "type": "Link", "nm": nm, "rect": [x0, cy - 10, x1, cy + 10],
                 "uri": "https://example.org/ottisk/link"}
        elif st == "FreeText":
            s.update(rect=[x0, y0 + 10, x1, y1], da=["Helv", 11, [0, 0, 0.6]], border_width=1,
                     contents="Надпись FreeText: Hello, PDF")
        elif st == "Line":
            s.update(rect=[x0, y0, x1, y1], line=[[x0 + 4, y0 + 8], [x1 - 4, y1 - 8]],
                     line_endings=["OpenArrow", "ClosedArrow"], color=[0.8, 0, 0], border_width=2)
        elif st == "Square":
            s.update(rect=[x0, y0 + 5, x1, y1], color=[0, 0.4, 0.8], interior=[0.85, 0.92, 1], border_width=2)
        elif st == "Circle":
            s.update(rect=[x0, y0 + 5, x1, y1], color=[0.6, 0, 0.6], interior=[0.95, 0.85, 0.95], border_width=2)
        elif st == "Polygon":
            pts = [[cx + 32 * math.cos(math.radians(90 + 72 * k)), cy + 28 * math.sin(math.radians(90 + 72 * k))]
                   for k in range(5)]
            s.update(rect=[x0, y0, x1, y1], vertices=pts, color=[0, 0.5, 0], interior=[0.85, 1, 0.85], border_width=1.5)
        elif st == "PolyLine":
            pts = [[x0 + 5 + k * (x1 - x0 - 10) / 5, cy + (18 if k % 2 else -18)] for k in range(6)]
            s.update(rect=[x0, y0, x1, y1], vertices=pts, color=[0.8, 0.4, 0], border_width=2)
        elif st in MARKUP_TEXT:
            q = sample[st]
            col = {"Highlight": [1, 1, 0], "Underline": [0, 0.6, 0], "Squiggly": [0.9, 0, 0.5],
                   "StrikeOut": [0.9, 0, 0], "Redact": [0, 0, 0]}[st]
            s.update(rect=[q[0] - 1, q[1] - 1, q[2] + 1, q[3] + 1], quads=[q], color=col)
            if st == "Redact":
                s["contents"] = "Помечено для скрытия (не применено)"
        elif st == "Caret":
            s.update(rect=[cx - 10, cy - 10, cx + 10, cy + 10], color=[0, 0, 0.9])
        elif st == "Ink":
            stroke1 = [[x0 + 5 + t * 3, cy + 20 * math.sin(t / 3.0)] for t in range(0, 34)]
            stroke2 = [[x0 + 10 + t * 3, cy - 18 + 6 * math.cos(t / 2.0)] for t in range(0, 30)]
            s.update(rect=[x0, y0, x1, y1], ink=[stroke1, stroke2], color=[0.1, 0.2, 0.7], border_width=2)
        elif st == "FileAttachment":
            s.update(rect=[cx - 10, cy - 12, cx + 10, cy + 12], icon="PushPin", color=[0.2, 0.4, 0.9],
                     attach={"name": "заметка.txt", "mime": "text/plain",
                             "text": "Вложенный файл матрицы аннотаций.\nEmbedded file.\n"})
        elif st == "Sound":
            s.update(rect=[cx - 10, cy - 10, cx + 10, cy + 10], icon="Speaker", color=[0.3, 0.3, 0.3])
        elif st == "Stamp":
            s.update(rect=[x0, cy - 18, x1, cy + 18], icon="Approved", color=[0, 0.5, 0])
        sp.append(s)
    return sp


def ap_stream(pdf, bbox, ops: str, fonts=None):
    w, h = bbox[2] - bbox[0], bbox[3] - bbox[1]
    st = Stream(pdf, ops.encode("latin-1"))
    st.Type = Name.XObject
    st.Subtype = Name.Form
    st.BBox = Array([0, 0, round(w, 3), round(h, 3)])
    res = Dictionary()
    if fonts:
        res.Font = Dictionary({"/" + k: v.ref for k, v in fonts.items()})
    st.Resources = res
    return pdf.make_indirect(st)


def poster(pdf, f, rect, label, rgb=(0.2, 0.2, 0.5), dash=True):
    w, h = rect[2] - rect[0], rect[3] - rect[1]
    size = 9
    tw = f.width(label, size)
    ops = ["q %s %s %s RG 1 w %s 0.5 0.5 %.2f %.2f re S Q" % (rgb[0], rgb[1], rgb[2],
           "[3 2] 0 d" if dash else "", w - 1, h - 1),
           "BT %s %s %s rg /F1 %d Tf %.2f %.2f Td <%s> Tj ET" % (rgb[0], rgb[1], rgb[2], size,
           (w - tw) / 2, h / 2 - 3, f.encode(label).hex().upper())]
    return ap_stream(pdf, rect, "\n".join(ops) + "\n", {"F1": f})


def add_raw(doc, pdf, page, cellr, helv):
    f = doc.font("arial")
    annots = page.obj.Annots
    made = {}

    def put(st, d, appearance):
        d.Type = Name.Annot
        d.Subtype = Name("/" + st) if st != "3D" else Name("/3D")
        d.P = page.obj
        d.M = String(g.FIXED_DATE)
        d.NM = String("ottisk-%02d-%s" % ([s for s, _ in SUBTYPES].index(st) + 1, st.lower()))
        ind = pdf.make_indirect(d)
        made[st] = (ind, appearance)
        return ind

    def idx(st):
        return [s for s, _ in SUBTYPES].index(st)

    later = []
    # Widget — текстовое поле (поле и виджет в одном словаре)
    r = inner(cellr[idx("Widget")])
    wr = [r[0], (r[1] + r[3]) / 2 - 10, r[2], (r[1] + r[3]) / 2 + 10]
    ww, wh = wr[2] - wr[0], wr[3] - wr[1]
    ap = ap_stream(pdf, wr, "q 0.97 0.97 1 rg 0 0 %.2f %.2f re f 0.3 0.3 0.6 RG 1 w 0.5 0.5 %.2f %.2f re S Q\n"
                   "/Tx BMC q BT /Helv 10 Tf 0 g 3 6 Td (Widget Tx) Tj ET Q EMC\n" % (ww, wh, ww - 1, wh - 1))
    ap.Resources = Dictionary(Font=Dictionary(Helv=helv))
    wd = Dictionary(Rect=Array(wr), FT=Name.Tx, T=String("matrix_text"), TU=String("Поле в матрице"),
                    V=String("Widget Tx"), DA=String("/Helv 10 Tf 0 g"), F=4,
                    MK=Dictionary(BC=Array([0.3, 0.3, 0.6]), BG=Array([0.97, 0.97, 1])),
                    AP=Dictionary(N=ap))
    w_ind = put("Widget", wd, "custom")
    pdf.Root.AcroForm = Dictionary(Fields=Array([w_ind]), DR=Dictionary(Font=Dictionary(Helv=helv)),
                                   DA=String("/Helv 0 Tf 0 g"))
    later.append(w_ind)

    # Screen — экран для медиа (без действия Rendition), постер в /AP
    r = inner(cellr[idx("Screen")])
    later.append(put("Screen", Dictionary(Rect=Array(r), T=String("Экран"), F=4,
                                          AP=Dictionary(N=poster(pdf, f, r, "Screen: медиа"))), "custom"))
    # PrinterMark — метка совмещения
    r = inner(cellr[idx("PrinterMark")])
    w, h = r[2] - r[0], r[3] - r[1]
    cx, cy, rad = w / 2, h / 2, min(w, h) / 2 - 3
    k = 0.5523 * rad
    circle = ("%.2f %.2f m %.2f %.2f %.2f %.2f %.2f %.2f c %.2f %.2f %.2f %.2f %.2f %.2f c "
              "%.2f %.2f %.2f %.2f %.2f %.2f c %.2f %.2f %.2f %.2f %.2f %.2f c S" % (
                  cx + rad, cy, cx + rad, cy + k, cx + k, cy + rad, cx, cy + rad,
                  cx - k, cy + rad, cx - rad, cy + k, cx - rad, cy,
                  cx - rad, cy - k, cx - k, cy - rad, cx, cy - rad,
                  cx + k, cy - rad, cx + rad, cy - k, cx + rad, cy))
    ops = "q 0 0 0 RG 0.7 w %s %.2f 0 m %.2f %.2f l S 0 %.2f m %.2f %.2f l S Q\n" % (circle, cx, cx, h, cy, w, cy)
    later.append(put("PrinterMark", Dictionary(Rect=Array(r), MN=Name.RegistrationTarget, F=4,
                                               AP=Dictionary(N=ap_stream(pdf, r, ops))), "custom"))
    # Watermark — фиксированная печать, текст под углом
    r = inner(cellr[idx("Watermark")])
    w, h = r[2] - r[0], r[3] - r[1]
    label = "ОБРАЗЕЦ"
    ops = ("q 0.6 0.6 0.6 rg 0.866 0.5 -0.5 0.866 %.2f %.2f cm BT /F1 16 Tf 0 0 Td <%s> Tj ET Q\n"
           % (w / 2 - 30, 6, f.encode(label).hex().upper()))
    later.append(put("Watermark", Dictionary(
        Rect=Array(r), F=4, FixedPrint=Dictionary(Type=Name.FixedPrint, Matrix=Array([1, 0, 0, 1, 0, 0]), H=0, V=0),
        AP=Dictionary(N=ap_stream(pdf, r, ops, {"F1": f}))), "custom"))
    # 3D — заглушка потока 3DD (не настоящий U3D), постер в /AP
    r = inner(cellr[idx("3D")])
    dd = Stream(pdf, b"U3D\x00ottisk-stub")
    dd.Type = Name("/3D")
    dd.Subtype = Name.U3D
    d3 = Dictionary(Rect=Array(r), F=4, Contents=String("3D-заглушка"),
                    AP=Dictionary(N=poster(pdf, f, r, "3D: заглушка")))
    d3["/3DD"] = pdf.make_indirect(dd)
    d3["/3DA"] = Dictionary(A=Name.PO, D=Name.PC)
    later.append(put("3D", d3, "custom"))
    # RichMedia — вложенный «ролик»-заглушка
    r = inner(cellr[idx("RichMedia")])
    ef = Stream(pdf, b"ottisk richmedia stub\n")
    ef.Type = Name.EmbeddedFile
    ef.Subtype = Name("/video#2Fmp4")
    fs = pdf.make_indirect(Dictionary(Type=Name.Filespec, F=String("clip.mp4"), UF=String("clip.mp4"),
                                      EF=Dictionary(F=pdf.make_indirect(ef))))
    inst = Dictionary(Type=Name.RichMediaInstance, Subtype=Name.Video, Asset=fs)
    conf = Dictionary(Type=Name.RichMediaConfiguration, Subtype=Name.Video, Instances=Array([inst]))
    content = Dictionary(Type=Name.RichMediaContent, Assets=Dictionary(Names=Array([String("clip.mp4"), fs])),
                         Configurations=Array([conf]))
    later.append(put("RichMedia", Dictionary(Rect=Array(r), F=4, RichMediaContent=content,
                                             RichMediaSettings=Dictionary(
                                                 Type=Name.RichMediaSettings,
                                                 Activation=Dictionary(Condition=Name.XA)),
                                             AP=Dictionary(N=poster(pdf, f, r, "RichMedia"))), "custom"))
    # Projection (PDF 2.0) — без внешнего вида
    r = inner(cellr[idx("Projection")])
    later.append(put("Projection", Dictionary(Rect=Array(r), F=4, Contents=String("Проекция без внешнего вида"),
                                              T=String("Оттиск")), "none"))
    # Movie — ссылка на внешний (несуществующий) ролик, постер в /AP
    r = inner(cellr[idx("Movie")])
    later.append(put("Movie", Dictionary(Rect=Array(r), F=4, T=String("Фильм"),
                                         Movie=Dictionary(F=String("clip.mov"), Aspect=Array([320, 240])),
                                         A=False, AP=Dictionary(N=poster(pdf, f, r, "Movie"))), "custom"))
    # TrapNet — последним в /Annots
    r = inner(cellr[idx("TrapNet")])
    w, h = r[2] - r[0], r[3] - r[1]
    ops = "q 0.2 0.6 0.6 RG 0.3 w " + " ".join("%.1f 0 m %.1f %.1f l S" % (x, x, h) for x in range(4, int(w), 8)) + " Q\n"
    trap = put("TrapNet", Dictionary(Rect=Array(r), F=4 | 64, LastModified=String(g.FIXED_DATE),
                                     AP=Dictionary(N=ap_stream(pdf, r, ops))), "custom")
    for a in later:
        annots.append(a)
    annots.append(trap)
    return made


def main():
    args = g.common_args(__doc__)
    root = Path(args.out)
    dst = root / "tests" / "fixtures" / "annots"
    dst.mkdir(parents=True, exist_ok=True)
    tmp = g.tmp_dir(args, "annots")
    cellr = cells(*g.A4)

    # 1. Страница с сеткой (без шрифтового finish — шрифт допишем перед сохранением)
    doc = g.Doc("Матрица аннотаций: 28 подтипов", "tests/fixtures/annots/matrix-28.pdf",
                creator="tools/gen/make_annots.py")
    sample = build_base(doc, cellr)
    base = tmp / "base.pdf"
    # Для MuPDF нужна законченная страница: сохраним копию со шрифтами, но шрифты в doc
    # продолжат пополняться (подписи в /AP) — поэтому базу пишем отдельным документом.
    doc_base = g.Doc("base", creator="tools/gen/make_annots.py")
    build_base(doc_base, cellr)
    doc_base.save(base)

    # 2. MuPDF создаёт свои 18 подтипов
    spec = {"fixed_ms": FIXED_MS, "annots": mupdf_specs(cellr, sample)}
    spec_path = tmp / "spec.json"
    g.write_json(spec_path, spec)
    mid = tmp / "mid.pdf"
    g.run([g.mutool_path(), "run", Path(__file__).resolve().parent / "annots.js", base, spec_path, mid])

    # 3. pikepdf: переносим аннотации MuPDF на страницу нашего doc и дописываем сырые подтипы
    src = pikepdf.open(mid)
    keep = []
    for a in src.pages[0].obj.Annots:
        if "/P" in a:
            del a["/P"]
        nm = a.get("/NM")
        if isinstance(nm, Name):                     # MuPDF JS кладёт строку как имя
            a.NM = String(str(nm)[1:])
        st = str(a.Subtype)
        if st == "/Popup" and str(a.Parent.Subtype) != "/Text":
            continue                                 # MuPDF сам добавил окна вложению и звуку
        if st in ("/FileAttachment", "/Sound") and "/Popup" in a:
            del a["/Popup"]
        keep.append(a)
    pdf = doc.pdf
    page = pdf.pages[0]
    page.obj.Annots = Array()
    copied = [pdf.copy_foreign(a) for a in keep]
    # /P и /Parent у скопированных указывают на копию чужой страницы — перепривяжем
    for a in copied:
        a.P = page.obj
        page.obj.Annots.append(a)
    # Остатки чужой страницы (через /P) больше не нужны: уберём ссылки и пересоберём
    # имя вложения/Sound/Link
    by_nm = {}
    for a in page.obj.Annots:
        st = str(a.Subtype)[1:]
        if st == "Popup":
            a.NM = String("ottisk-15-popup")
            a.Contents = String("Всплывающее окно заметки из ячейки 1")
        if st == "Link":
            a.NM = String("ottisk-02-link")
            a.Border = Array([0, 0, 1])
            a.C = Array([0, 0, 1])
            a.H = Name.I
        if st == "Sound":
            n = 800
            samples = bytes((int(127 * math.sin(2 * math.pi * 440 * t / 8000)) & 0xFF) for t in range(n))
            snd = Stream(pdf, samples)
            snd.Type = Name.Sound
            snd.R = 8000
            snd.C = 1
            snd.B = 8
            snd.E = Name.Signed
            a.Sound = pdf.make_indirect(snd)
        by_nm[str(a.get("/NM", ""))] = a
    helv = pdf.make_indirect(Dictionary(Type=Name.Font, Subtype=Name.Type1, BaseFont=Name.Helvetica,
                                        Encoding=Name.WinAnsiEncoding))
    made = add_raw(doc, pdf, page, cellr, helv)

    out = dst / "matrix-28.pdf"
    doc.save(out, version="2.0")
    (dst / "annots-all.pdf").write_bytes(out.read_bytes())   # имя из плана (5.1, 5.2)

    # 4. Ожидания
    chk = pikepdf.open(out)
    annots = list(chk.pages[0].obj.Annots)
    subtypes = [str(a.Subtype)[1:] for a in annots]
    missing = [s for s, _ in SUBTYPES if s not in subtypes]
    if missing or len(annots) != 28:
        raise RuntimeError("матрица неполна: %d аннотаций, нет %s" % (len(annots), missing))
    if subtypes[-1] != "TrapNet":
        raise RuntimeError("TrapNet не последний в /Annots")
    items = []
    for i, (st, ru) in enumerate(SUBTYPES):
        a = next(x for x in annots if str(x.Subtype)[1:] == st)
        if st in made:
            appearance = made[st][1]
            created_by = "pikepdf"
        else:
            created_by = "mupdf"
            appearance = "mupdf" if "/AP" in a else "none"
        items.append({
            "n": i + 1, "subtype": st, "ru": ru, "nm": str(a.get("/NM", "")),
            "rect": [round(float(v), 2) for v in a.Rect],
            "created_by": created_by, "appearance": appearance,
            "contents": str(a.get("/Contents", "")),
            "parent": "ottisk-01-text" if st == "Popup" else None,
            "cell": [round(v, 2) for v in cellr[i]],
        })
    notes = {
        "Link": "без /AP (обычно для ссылок); /A URI https://example.org/ottisk/link, рамка /Border [0 0 1]",
        "Popup": "без /AP; /Parent — Text из ячейки 1, /Open true",
        "Projection": "PDF 2.0, без /AP и без /ExData",
        "3D": "/3DD — заглушка (не настоящий U3D), видим только постер /AP",
        "RichMedia": "вложение clip.mp4 — заглушка (не видео)",
        "Movie": "внешний clip.mov не существует",
        "TrapNet": "флаги Print|ReadOnly, последний в /Annots",
        "Sound": "поток /Sound 8 кГц, 8 бит, 0,1 с",
        "FileAttachment": "вложение заметка.txt (text/plain)",
        "Redact": "не применена (applyRedactions не вызывался)",
        "Widget": "текстовое поле matrix_text, /AcroForm с /Helv",
    }
    for it in items:
        if it["subtype"] in notes:
            it["notes"] = notes[it["subtype"]]
    g.write_json(dst / "annots.json", {
        "generator": "tools/gen/make_annots.py", "pdf": "matrix-28.pdf", "page": 1,
        "aliases": {"annots-all.pdf": "байт в байт копия matrix-28.pdf — имя из плана (сессии 5.1, 5.2)"},
        "pdf_version": "2.0", "annots_total": len(annots), "subtypes_total": 28,
        "order_in_annots": subtypes, "annots": items})
    g.say("annots: matrix-28.pdf %d байт, %d аннотаций" % (out.stat().st_size, len(annots)))


if __name__ == "__main__":
    main()
