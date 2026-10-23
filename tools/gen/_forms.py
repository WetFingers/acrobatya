# -*- coding: utf-8 -*-
"""Помощники AcroForm для генераторов (make_forms.py, make_demo.py).

Что: поля Tx/Btn/Ch/Sig со своими потоками внешнего вида (/AP), JS-действия
(AFNumber_*, AFSimple_Calculate, AFDate_*), ресурсы /DR (Helv, ZaDb, FRu — Type0 Arial).
Детерминизм: только константы и pikepdf; внешний вид строится из значений полей.
"""
from pikepdf import Array, Dictionary, Name, Stream, String

import _genlib as g

FF_READONLY = 1
FF_REQUIRED = 2
FF_MULTILINE = 1 << 12
FF_PASSWORD = 1 << 13
FF_NOTOGGLETOOFF = 1 << 14
FF_RADIO = 1 << 15
FF_PUSH = 1 << 16
FF_COMBO = 1 << 17
FF_EDIT = 1 << 18
FF_MULTISELECT = 1 << 21
FF_COMB = 1 << 24

_ARIAL = None


def helv_width(s: str, size: float) -> float:
    """Метрики Helvetica = метрики Arial (для центрирования подписей)."""
    global _ARIAL
    if _ARIAL is None:
        _ARIAL = g.TrueType(g.FONTS_DIR / "arial.ttf")
    return sum(_ARIAL.advance(_ARIAL.gid(c)) for c in s) * size / _ARIAL.units


def lit(s: str) -> str:
    """Строка PDF в WinAnsi для оператора Tj."""
    b = s.encode("cp1252")
    out = []
    for c in b:
        ch = chr(c)
        if ch in "\\()":
            out.append("\\" + ch)
        elif 32 <= c < 127:
            out.append(ch)
        else:
            out.append("\\%03o" % c)
    return "(" + "".join(out) + ")"


def js(code: str) -> Dictionary:
    return Dictionary(S=Name.JavaScript, JS=String(code))


class Form:
    """Сборщик AcroForm одного документа."""

    def __init__(self, doc: "g.Doc", cyr_font: "g.PdfFont | None" = None):
        self.doc = doc
        self.pdf = doc.pdf
        self.fields = Array()
        self.co = Array()
        self.helv = self.pdf.make_indirect(Dictionary(Type=Name.Font, Subtype=Name.Type1, BaseFont=Name.Helvetica,
                                                      Encoding=Name.WinAnsiEncoding))
        self.zadb = self.pdf.make_indirect(Dictionary(Type=Name.Font, Subtype=Name.Type1,
                                                      BaseFont=Name.ZapfDingbats))
        self.cyr = cyr_font
        if cyr_font is not None:
            # Полный набор кириллицы и латиницы, чтобы поле можно было править в просмотрщике
            cyr_font.encode("".join(chr(c) for c in range(0x20, 0x7F)))
            cyr_font.encode("".join(chr(c) for c in range(0x410, 0x450)) + "Ёё№«»—–")

    # --- внешний вид ---
    def _xobj(self, w, h, ops, fonts):
        st = Stream(self.pdf, ("\n".join(ops) + "\n").encode("latin-1"))
        st.Type = Name.XObject
        st.Subtype = Name.Form
        st.BBox = Array([0, 0, round(w, 3), round(h, 3)])
        res = Dictionary()
        if fonts:
            res.Font = Dictionary(fonts)
        st.Resources = res
        return self.pdf.make_indirect(st)

    @staticmethod
    def _frame(w, h, bg=(1, 1, 1), bc=(0.25, 0.25, 0.45)):
        ops = []
        if bg is not None:
            ops.append("%.3f %.3f %.3f rg 0 0 %.2f %.2f re f" % (bg[0], bg[1], bg[2], w, h))
        if bc is not None:
            ops.append("%.3f %.3f %.3f RG 1 w 0.5 0.5 %.2f %.2f re S" % (bc[0], bc[1], bc[2], w - 1, h - 1))
        return ops

    def text_ap(self, rect, value, size=10, cyr=False, multiline=False, bg=(1, 1, 1), comb=0, password=False):
        w, h = rect[2] - rect[0], rect[3] - rect[1]
        ops = self._frame(w, h, bg)
        ops.append("/Tx BMC q 1 1 %.2f %.2f re W n BT" % (w - 2, h - 2))
        fonts = {}
        key = "FRu" if cyr else "Helv"
        fonts["/" + key] = self.cyr.ref if cyr else self.helv
        ops.append("/%s %s Tf 0 g" % (key, g._num(size)))
        shown = "*" * len(value) if password else value
        lines = shown.split("\n") if multiline else [shown]
        if comb and not multiline:
            cw = w / comb
            for i, ch in enumerate(shown[:comb]):
                x = i * cw + (cw - helv_width(ch, size)) / 2
                ops.append("1 0 0 1 %.2f %.2f Tm %s Tj" % (x, (h - size * 0.7) / 2, lit(ch)))
        else:
            y = h - 2 - size if multiline else (h - size * 0.7) / 2
            for ln in lines:
                enc = ("<" + self.cyr.encode(ln).hex().upper() + ">") if cyr else lit(ln)
                ops.append("1 0 0 1 2 %.2f Tm %s Tj" % (y, enc))
                y -= size * 1.2
        ops.append("ET Q EMC")
        if comb:
            for i in range(1, comb):
                ops.insert(2, "0.7 0.7 0.8 RG 0.5 w %.2f 0 m %.2f %.2f l S" % (i * w / comb, i * w / comb, h))
        return self._xobj(w, h, ops, fonts)

    # --- поля ---
    def _widget(self, page, rect, extra):
        d = Dictionary(Type=Name.Annot, Subtype=Name.Widget, Rect=Array([round(v, 3) for v in rect]), F=4, P=page.obj)
        for k, v in extra.items():
            d[k] = v
        return d

    def _add(self, page, obj, calc=False):
        ind = self.pdf.make_indirect(obj)
        self.fields.append(ind)
        if "/Annots" not in page.obj:
            page.obj.Annots = Array()
        if "/Kids" in obj:
            for k in obj.Kids:
                page.obj.Annots.append(k)
        else:
            page.obj.Annots.append(ind)
        if calc:
            self.co.append(ind)
        return ind

    def text(self, page, name, rect, value, tu="", size=10, cyr=False, ff=0, aa=None, maxlen=None,
             bg=(1, 1, 1), comb=False):
        da = "/%s %s Tf 0 g" % ("FRu" if cyr else "Helv", g._num(size))
        ap = self.text_ap(rect, value, size, cyr, bool(ff & FF_MULTILINE), bg,
                          comb=(maxlen if comb else 0), password=bool(ff & FF_PASSWORD))
        extra = {"/FT": Name.Tx, "/T": String(name), "/V": String(value), "/DA": String(da),
                 "/MK": Dictionary(BC=Array([0.25, 0.25, 0.45]), BG=Array(list(bg))), "/AP": Dictionary(N=ap)}
        if tu:
            extra["/TU"] = String(tu)
        if ff or comb:
            extra["/Ff"] = ff | (FF_COMB if comb else 0)
        if maxlen:
            extra["/MaxLen"] = maxlen
        if aa:
            extra["/AA"] = Dictionary({"/" + k: js(v) for k, v in aa.items()})
        return self._add(page, self._widget(page, rect, extra), calc=aa is not None and "C" in aa)

    def _check_aps(self, w, h, ch, on_name):
        size = min(w, h) * 0.75
        x = (w - size * 0.75) / 2
        y = (h - size * 0.7) / 2
        on = self._xobj(w, h, self._frame(w, h) + ["q BT /ZaDb %.2f Tf 0 g %.2f %.2f Td (%s) Tj ET Q" % (size, x, y, ch)],
                        {"/ZaDb": self.zadb})
        off = self._xobj(w, h, self._frame(w, h), {})
        down_on = self._xobj(w, h, self._frame(w, h, bg=(0.75, 0.75, 0.75)) +
                             ["q BT /ZaDb %.2f Tf 0 g %.2f %.2f Td (%s) Tj ET Q" % (size, x, y, ch)], {"/ZaDb": self.zadb})
        down_off = self._xobj(w, h, self._frame(w, h, bg=(0.75, 0.75, 0.75)), {})
        return Dictionary(N=Dictionary({"/" + on_name: on, "/Off": off}),
                          D=Dictionary({"/" + on_name: down_on, "/Off": down_off}))

    def checkbox(self, page, name, rect, checked=True, tu="", export="Yes"):
        w, h = rect[2] - rect[0], rect[3] - rect[1]
        state = Name("/" + export) if checked else Name.Off
        extra = {"/FT": Name.Btn, "/T": String(name), "/V": state, "/AS": state,
                 "/DA": String("/ZaDb 0 Tf 0 g"),
                 "/MK": Dictionary(CA=String("4"), BC=Array([0.25, 0.25, 0.45]), BG=Array([1, 1, 1])),
                 "/AP": self._check_aps(w, h, "4", export)}
        if tu:
            extra["/TU"] = String(tu)
        return self._add(page, self._widget(page, rect, extra))

    def radio(self, page, name, rects_exports, value, tu=""):
        parent = Dictionary(FT=Name.Btn, T=String(name), Ff=FF_RADIO | FF_NOTOGGLETOOFF,
                            V=Name("/" + value), DA=String("/ZaDb 0 Tf 0 g"), Kids=Array())
        if tu:
            parent.TU = String(tu)
        pind = self.pdf.make_indirect(parent)
        for rect, exp in rects_exports:
            w, h = rect[2] - rect[0], rect[3] - rect[1]
            kid = self._widget(page, rect, {
                "/Parent": pind, "/AS": Name("/" + exp) if exp == value else Name.Off,
                "/MK": Dictionary(CA=String("l"), BC=Array([0.25, 0.25, 0.45]), BG=Array([1, 1, 1])),
                "/AP": self._check_aps(w, h, "l", exp)})
            pind.Kids.append(self.pdf.make_indirect(kid))
        self.fields.append(pind)
        if "/Annots" not in page.obj:
            page.obj.Annots = Array()
        for k in pind.Kids:
            page.obj.Annots.append(k)
        return pind

    def push(self, page, name, rect, caption, action, tu=""):
        w, h = rect[2] - rect[0], rect[3] - rect[1]
        size = 10
        x = (w - helv_width(caption, size)) / 2
        ap = self._xobj(w, h, self._frame(w, h, bg=(0.85, 0.87, 0.92)) +
                        ["q BT /Helv %d Tf 0 g %.2f %.2f Td %s Tj ET Q" % (size, x, (h - size * 0.7) / 2, lit(caption))],
                        {"/Helv": self.helv})
        extra = {"/FT": Name.Btn, "/T": String(name), "/Ff": FF_PUSH, "/DA": String("/Helv 10 Tf 0 g"),
                 "/MK": Dictionary(CA=String(caption), BG=Array([0.85, 0.87, 0.92]), BC=Array([0.25, 0.25, 0.45])),
                 "/A": action, "/AP": Dictionary(N=ap)}
        if tu:
            extra["/TU"] = String(tu)
        return self._add(page, self._widget(page, rect, extra))

    def combo(self, page, name, rect, options, value, tu="", editable=False, cyr=False):
        w, h = rect[2] - rect[0], rect[3] - rect[1]
        size = 10
        key = "FRu" if cyr else "Helv"
        enc = ("<" + self.cyr.encode(value).hex().upper() + ">") if cyr else lit(value)
        ops = self._frame(w, h) + [
            "q 0.85 0.85 0.9 rg %.2f 1 %.2f %.2f re f Q" % (w - h, h - 2, h - 2),
            "q 0.2 0.2 0.2 rg %.2f %.2f m %.2f %.2f l %.2f %.2f l f Q" % (w - h + 4, h * 0.65, w - 4, h * 0.65, w - h / 2, h * 0.3),
            "/Tx BMC q 1 1 %.2f %.2f re W n BT /%s %d Tf 0 g 2 %.2f Td %s Tj ET Q EMC" % (
                w - h - 2, h - 2, key, size, (h - size * 0.7) / 2, enc)]
        ap = self._xobj(w, h, ops, {"/" + key: self.cyr.ref if cyr else self.helv})
        extra = {"/FT": Name.Ch, "/T": String(name), "/Ff": FF_COMBO | (FF_EDIT if editable else 0),
                 "/Opt": Array([String(o) for o in options]), "/V": String(value),
                 "/DA": String("/%s %d Tf 0 g" % (key, size)), "/AP": Dictionary(N=ap),
                 "/MK": Dictionary(BC=Array([0.25, 0.25, 0.45]), BG=Array([1, 1, 1]))}
        if tu:
            extra["/TU"] = String(tu)
        return self._add(page, self._widget(page, rect, extra))

    def listbox(self, page, name, rect, options, selected, tu="", multi=True):
        w, h = rect[2] - rect[0], rect[3] - rect[1]
        size = 10
        lh = size * 1.25
        ops = self._frame(w, h) + ["/Tx BMC q 1 1 %.2f %.2f re W n" % (w - 2, h - 2)]
        y = h - 2
        idx = []
        for i, (exp, disp) in enumerate(options):
            y -= lh
            if exp in selected:
                idx.append(i)
                ops.append("0.6 0.75 0.9 rg 1 %.2f %.2f %.2f re f" % (y - 2, w - 2, lh))
            ops.append("BT /Helv %d Tf 0 g 3 %.2f Td %s Tj ET" % (size, y + 1, lit(disp)))
        ops.append("Q EMC")
        ap = self._xobj(w, h, ops, {"/Helv": self.helv})
        extra = {"/FT": Name.Ch, "/T": String(name), "/Ff": FF_MULTISELECT if multi else 0,
                 "/Opt": Array([Array([String(e), String(d)]) for e, d in options]),
                 "/V": Array([String(s) for s in selected]) if multi else String(selected[0]),
                 "/I": Array(idx), "/DA": String("/Helv %d Tf 0 g" % size), "/AP": Dictionary(N=ap),
                 "/MK": Dictionary(BC=Array([0.25, 0.25, 0.45]), BG=Array([1, 1, 1]))}
        if tu:
            extra["/TU"] = String(tu)
        return self._add(page, self._widget(page, rect, extra))

    def signature(self, page, name, rect, tu=""):
        w, h = rect[2] - rect[0], rect[3] - rect[1]
        ap = self._xobj(w, h, self._frame(w, h, bg=(0.97, 0.97, 0.9), bc=(0.5, 0.5, 0.3)), {})
        extra = {"/FT": Name.Sig, "/T": String(name), "/AP": Dictionary(N=ap),
                 "/MK": Dictionary(BC=Array([0.5, 0.5, 0.3]), BG=Array([0.97, 0.97, 0.9]))}
        if tu:
            extra["/TU"] = String(tu)
        return self._add(page, self._widget(page, rect, extra))

    def finish(self, xfa=None, doc_js=None):
        fonts = Dictionary(Helv=self.helv, ZaDb=self.zadb)
        if self.cyr is not None:
            fonts.FRu = self.cyr.ref
        af = Dictionary(Fields=self.fields, DR=Dictionary(Font=fonts), DA=String("/Helv 0 Tf 0 g"),
                        NeedAppearances=False)
        if len(self.co):
            af.CO = self.co
        if xfa is not None:
            af.XFA = xfa
        self.pdf.Root.AcroForm = self.pdf.make_indirect(af)
        if doc_js:
            names = Array()
            for k, v in doc_js.items():
                names.append(String(k))
                names.append(self.pdf.make_indirect(js(v)))
            self.pdf.Root.Names = Dictionary(JavaScript=Dictionary(Names=names))
