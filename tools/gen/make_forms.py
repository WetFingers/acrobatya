# -*- coding: utf-8 -*-
"""make_forms.py — формы: tests/fixtures/forms/{forms-all.pdf, fields-all.pdf, xfa-stub.pdf, forms.json}.

Что:
  forms-all.pdf — все типы полей AcroForm: Tx (обычное, многострочное, кириллица шрифтом
    Type0 FRu, дата AFDate_FormatEx, числа AFNumber_*, итог AFSimple_Calculate("SUM") с /CO,
    проверка /AA /V (AFRange_Validate и своя — «только цифры» с app.alert), пароль, comb с /MaxLen),
    Btn (флажок, радиокнопки ×3, кнопки ResetForm, JavaScript, SubmitForm mailto: (FDF) и
    https: (HTML POST)), Ch (выпадающий список, редактируемый выпадающий список, список с
    мультивыбором), Sig (пустое поле подписи); /DV у полей со значением (сброс возвращает их),
    страница /Tabs /R; у всех полей свой /AP, NeedAppearances false; JS документа в /Names /JavaScript.
    Имя forms-all.pdf — из плана (сессии 4.4, 4.5, 6.1–6.3);
  fields-all.pdf — байт в байт копия forms-all.pdf под именем из задания этапа 0 (Git хранит один blob);
  xfa-stub.pdf — /AcroForm с /XFA (массив пакетов xdp: preamble, config, template,
    datasets, postamble) и запасным полем AcroForm — для полосы XFA (6.1);
  forms.json — ожидания (поля, типы, значения, действия).
Откуда: pikepdf + _genlib + _forms (свои потоки /AP), шрифт Arial из C:\\Windows\\Fonts.
Детерминизм: значения и даты — константы, сохранение через _genlib.save_pdf.
Запуск: "C:\\Tools\\venv-test\\Scripts\\python.exe" -X utf8 -I tools/gen/make_forms.py [--out <корень>]
"""
import sys
from pathlib import Path

sys.dont_write_bytecode = True                     # без __pycache__ в tools/gen
sys.path.insert(0, str(Path(__file__).resolve().parent))

import pikepdf  # noqa: E402
from pikepdf import Array, Dictionary, Name, Stream, String  # noqa: E402

import _forms as fm  # noqa: E402
import _genlib as g  # noqa: E402

NUM_F = 'AFNumber_Format(2, 0, 0, 0, "", true);'
NUM_K = 'AFNumber_Keystroke(2, 0, 0, 0, "", true);'
# своя проверка /AA /V: «abc» отклоняется с app.alert (сессия 6.1 ловит его фейковым ответчиком)
QTY_V = ('if (event.value != "" && !/^[0-9]+$/.test(event.value)) '
         '{ app.alert("Количество: только цифры, а не " + event.value); event.rc = false; }')
# SubmitForm: /Flags 0 — FDF всех полей; 4 (ExportFormat) — HTML-форма методом POST
SUBMITS = [
    ("submit_mail", "Submit: e-mail", "mailto:forms@example.org?subject=Ottisk%20form", 0,
     "Отправить по почте (FDF)"),
    ("submit_http", "Submit: https", "https://example.org/ottisk/submit", 4, "Отправить на сервер (POST)"),
]


def fields_all(out: Path):
    doc = g.Doc("Форма: все типы полей", "tests/fixtures/forms/forms-all.pdf", creator="tools/gen/make_forms.py")
    f = doc.font("arial")
    fb = doc.font("arialbd")
    cyr = doc.font("arial")
    form = fm.Form(doc, cyr_font=cyr)
    cv = doc.canvas()
    W, H = g.A4
    cv.text(56, H - 60, "Форма: все типы полей AcroForm", fb, 16)
    cv.text(56, H - 78, "Числа: формат AFNumber_Format; «Итого» = a + b (AFSimple_Calculate, /CO).", f, 9,
            rgb=(0.3, 0.3, 0.3))
    rows = [
        ("ФИО (Tx)", 22), ("Комментарий (многострочное)", 46), ("По-русски (шрифт FRu)", 22),
        ("Дата (AFDate)", 22), ("Сумма a (AFNumber)", 22), ("Сумма b (AFNumber)", 22),
        ("Итого (расчёт, только чтение)", 22), ("Пароль", 22), ("Код (comb, 6)", 22),
        ("Согласен (флажок)", 18), ("Цвет (радиокнопки)", 18), ("Город (выпадающий список)", 22),
        ("Языки (список, мультивыбор)", 52), ("Кнопки", 24), ("Подпись (поле Sig)", 50),
        ("Страна (список, можно вписать)", 22), ("Количество (только цифры)", 22), ("Отправка (Submit)", 24),
    ]
    y = H - 110
    rects = []
    for label, hgt in rows:
        cv.text(56, y - 14, label, f, 10)
        rects.append([240, y - hgt, 520, y])
        y -= hgt + 10
    r = rects
    x0, y0, _, y1 = r[10]
    s = y1 - y0
    radios = []
    for i, (exp, lab) in enumerate((("red", "красный"), ("green", "зелёный"), ("blue", "синий"))):
        bx = x0 + i * 92
        radios.append(([bx, y0, bx + s, y1], exp))
        cv.text(bx + s + 4, y0 + 4, lab, f, 9)
    page = doc.add_page(cv)
    page.obj.Tabs = Name.R                       # обход по Tab — по строкам (6.1)
    form.text(page, "name", r[0], "Ivan Petrov", tu="ФИО")
    form.text(page, "comment", r[1], "First line\nSecond line", tu="Комментарий", ff=fm.FF_MULTILINE)
    form.text(page, "comment_ru", r[2], "Привет, мир", tu="Поле с кириллицей", cyr=True)
    form.text(page, "date", r[3], "01.01.2026", tu="Дата",
              aa={"F": 'AFDate_FormatEx("dd.mm.yyyy");', "K": 'AFDate_KeystrokeEx("dd.mm.yyyy");'})
    form.text(page, "a", r[4], "1,200.50", tu="Сумма a",
              aa={"F": NUM_F, "K": NUM_K, "V": "AFRange_Validate(true, 0, true, 1000000);"})
    form.text(page, "b", r[5], "799.50", tu="Сумма b", aa={"F": NUM_F, "K": NUM_K})
    form.text(page, "total", r[6], "2,000.00", tu="Итого", ff=fm.FF_READONLY, bg=(0.93, 0.93, 0.93),
                      aa={"C": 'AFSimple_Calculate("SUM", new Array ("a", "b"));', "F": NUM_F, "K": NUM_K})
    # /V чисел — без форматирования (как хранит Acrobat), /AP — с форматированием
    for fld in form.fields:
        nm = str(fld.get("/T", ""))
        if nm == "a":
            fld.V = String("1200.5")
        elif nm == "b":
            fld.V = String("799.5")
        elif nm == "total":
            fld.V = String("2000")
    form.text(page, "secret", r[7], "", tu="Пароль", ff=fm.FF_PASSWORD)
    form.text(page, "pin", r[8], "123456", tu="Код", maxlen=6, comb=True)
    x0, y0, _, y1 = r[9]
    form.checkbox(page, "agree", [x0, y0, x0 + (y1 - y0), y1], checked=True, tu="Согласен")
    form.radio(page, "color", radios, "green", tu="Цвет")
    form.combo(page, "city", r[11], ["Moscow", "Kazan", "Sochi"], "Kazan", tu="Город")
    form.combo(page, "country", r[15], ["Russia", "Germany", "France"], "Russia",
               tu="Страна (можно вписать своё)", editable=True)
    form.text(page, "qty", r[16], "3", tu="Количество (только цифры)", aa={"V": QTY_V})
    form.listbox(page, "langs", r[12], [("ru", "Russian"), ("en", "English"), ("de", "German")], ["ru", "de"],
                 tu="Языки")
    x0, y0, x1, y1 = r[13]
    form.push(page, "reset", [x0, y0, x0 + 120, y1], "Reset",
              Dictionary(S=Name.ResetForm), tu="Сбросить форму")
    form.push(page, "hello", [x0 + 140, y0, x0 + 260, y1], "Hello (JS)",
              fm.js('app.alert("Привет из формы «Оттиска»");'), tu="Показать сообщение")
    form.signature(page, "sig", r[14], tu="Подпись")
    x0, y0, x1, y1 = r[17]
    for i, (nm, cap, url, flags, tu) in enumerate(SUBMITS):
        form.push(page, nm, [x0 + i * 140, y0, x0 + i * 140 + 130, y1], cap,
                  Dictionary(S=Name.SubmitForm, F=Dictionary(FS=Name.URL, F=String(url)), Flags=flags), tu=tu)
    # /DV = исходное /V (сброс возвращает исходные значения); у comment /DV нет — сброс очищает его
    for fld in form.fields:
        v = fld.get("/V")
        if v is None or str(fld.get("/T", "")) == "comment":
            continue
        if isinstance(v, Name):
            fld.DV = Name(str(v))
        elif isinstance(v, Array):
            fld.DV = Array([String(str(x)) for x in v])
        else:
            fld.DV = String(str(v))
    form.finish(doc_js={"ottisk-init": "var ottiskFormLoaded = true;"})
    doc.save(out)
    return form


def xfa_stub(out: Path):
    doc = g.Doc("Заглушка XFA", "tests/fixtures/forms/xfa-stub.pdf", creator="tools/gen/make_forms.py")
    f = doc.font("arial")
    form = fm.Form(doc)
    cv = doc.canvas()
    W, H = g.A4
    cv.text(56, H - 60, "Документ с XFA (заглушка)", f, 16)
    cv.text(56, H - 84, "AcroForm содержит /XFA — просмотрщик должен показать полосу XFA.", f, 10)
    cv.text(56, H - 100, "Запасное поле AcroForm «name» ниже.", f, 10)
    cv.text(56, H - 140, "ФИО:", f, 10)
    page = doc.add_page(cv)
    form.text(page, "form1[0].page1[0].name[0]", [140, H - 150, 400, H - 128], "Ivan", tu="ФИО")
    pdf = doc.pdf
    pre = ('<?xml version="1.0" encoding="UTF-8"?>\n'
           '<xdp:xdp xmlns:xdp="http://ns.adobe.com/xdp/" timeStamp="2026-01-01T12:00:00Z" '
           'uuid="0f5e2c1a-6b1d-4c1e-9a57-0771a5b1c0de">\n')
    config = ('<config xmlns="http://www.xfa.org/schema/xci/3.0/">\n'
              '<present><pdf><version>1.7</version><interactive>1</interactive></pdf></present>\n'
              '</config>\n')
    template = ('<template xmlns="http://www.xfa.org/schema/xfa-template/3.3/">\n'
                '<subform name="form1" layout="tb" locale="ru_RU">\n'
                '<pageSet><pageArea name="Page1" id="Page1">'
                '<contentArea x="0.25in" y="0.25in" w="7.77in" h="11.19in"/>'
                '<medium stock="a4" short="210mm" long="297mm"/></pageArea></pageSet>\n'
                '<subform name="page1" w="7.77in" h="11.19in">\n'
                '<field name="name" x="1.5in" y="1.6in" w="3.6in" h="9mm">'
                '<ui><textEdit/></ui><caption reserve="20mm"><value><text>ФИО</text></value></caption>'
                '</field>\n</subform>\n</subform>\n</template>\n')
    datasets = ('<xfa:datasets xmlns:xfa="http://www.xfa.org/schema/xfa-data/1.0/">\n'
                '<xfa:data><form1><page1><name>Ivan</name></page1></form1></xfa:data>\n'
                '</xfa:datasets>\n')
    post = '</xdp:xdp>\n'
    xfa = Array()
    for name, body in (("preamble", pre), ("config", config), ("template", template),
                       ("datasets", datasets), ("postamble", post)):
        xfa.append(String(name))
        xfa.append(pdf.make_indirect(Stream(pdf, body.encode("utf-8"))))
    form.finish(xfa=xfa)
    pdf.Root.NeedsRendering = False
    doc.save(out)


def main():
    args = g.common_args(__doc__)
    root = Path(args.out)
    dst = root / "tests" / "fixtures" / "forms"
    dst.mkdir(parents=True, exist_ok=True)
    fields_all(dst / "forms-all.pdf")
    # то же самое под именем из задания этапа 0 (байт в байт; Git хранит один blob)
    (dst / "fields-all.pdf").write_bytes((dst / "forms-all.pdf").read_bytes())
    xfa_stub(dst / "xfa-stub.pdf")
    # ожидания — из готового файла
    p = pikepdf.open(dst / "forms-all.pdf")
    items = []
    for fld in p.Root.AcroForm.Fields:
        it = {"name": str(fld.T), "type": str(fld.FT)[1:], "ff": int(fld.get("/Ff", 0))}
        v = fld.get("/V")
        if isinstance(v, Name):
            it["value"] = str(v)[1:]
        elif isinstance(v, Array):
            it["value"] = [str(x) for x in v]
        elif v is not None:
            it["value"] = str(v)
        if "/TU" in fld:
            it["tooltip"] = str(fld.TU)
        if "/AA" in fld:
            it["js"] = {k[1:]: str(v.JS) for k, v in fld.AA.items()}
        dv = fld.get("/DV")
        if isinstance(dv, Name):
            it["dv"] = str(dv)[1:]
        elif isinstance(dv, Array):
            it["dv"] = [str(x) for x in dv]
        elif dv is not None:
            it["dv"] = str(dv)
        if "/A" in fld:
            it["action"] = str(fld.A.S)[1:]
            if fld.A.S == Name.SubmitForm:
                it["submit_url"] = str(fld.A.F.F)
                it["submit_flags"] = int(fld.A.Flags)
                it["submit_format"] = "HTML POST" if int(fld.A.Flags) & 4 else "FDF"
        if "/Opt" in fld:
            it["options"] = [[str(x) for x in o] if isinstance(o, Array) else str(o) for o in fld.Opt]
        if "/Kids" in fld:
            it["widgets"] = len(fld.Kids)
            it["exports"] = [[str(n)[1:] for n in kid.AP.N.keys() if n != "/Off"][0] for kid in fld.Kids]
        if "/MaxLen" in fld:
            it["maxlen"] = int(fld.MaxLen)
        items.append(it)
    g.write_json(dst / "forms.json", {
        "generator": "tools/gen/make_forms.py",
        "forms-all.pdf": {"pages": 1, "fields": items,
                          "calculation_order": [str(x.T) for x in p.Root.AcroForm.get("/CO", [])],
                          "need_appearances": False,
                          "tabs": str(p.pages[0].obj.get("/Tabs", Name.S))[1:],
                          "document_js": ["ottisk-init"],
                          "expect_total_after_recalc": "2,000.00",
                          "expect_validate_rejects": {"qty": "abc"},
                          "reset_note": "сброс возвращает /DV (= исходное /V); у comment /DV нет — очищается"},
        "fields-all.pdf": {"copy_of": "forms-all.pdf",
                           "note": "байт в байт копия: имя из задания этапа 0, план (4.4, 4.5, 6.1–6.3) "
                                   "называет файл forms-all.pdf"},
        "xfa-stub.pdf": {"pages": 1, "xfa": True, "xfa_packets": ["preamble", "config", "template", "datasets",
                                                                   "postamble"],
                         "acroform_fallback_fields": ["form1[0].page1[0].name[0]"],
                         "note": "заглушка: статическая XFA-форма с запасным AcroForm, NeedsRendering false"},
    })
    for n in ("forms-all.pdf", "fields-all.pdf", "xfa-stub.pdf"):
        g.say("forms: %-15s %6d байт" % (n, (dst / n).stat().st_size))


if __name__ == "__main__":
    main()
