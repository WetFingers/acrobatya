# -*- coding: utf-8 -*-
"""normalize_word.py — делает файлы Word детерминированными (вызывается из make_*_fixtures.ps1).

Что:
  pdf  <вход> <выход> --title T [--subject S] — PDF из ExportAsFixedFormat: /Info (автор — не
       пользователь машины, даты фиксированы, Creator/Producer Word сохраняются), XMP заново,
       теги подмножеств шрифтов (ABCDEF+) — из хеша программы шрифта, /ID — deterministic_id;
  docx <вход> <выход> — убирает w:rsid*, w14:paraId/textId, <w:rsids>, docId; core.xml/app.xml —
       фиксированные автор, даты, ревизия, TotalTime; ZIP пересобирается с датой 1980-01-01;
  rtf  <вход> <выход> — убирает \\rsid*, таблицу rsid, {\\*\\datastore}, {\\nonshppict} (WMF-дубль
       картинки); {\\info} — фиксированный.
Откуда: файлы Word, pikepdf, zipfile, re.
Детерминизм: всё, что Word пишет от времени, сессии и пользователя, заменяется константами.
Запуск: "C:\\Tools\\venv-test\\Scripts\\python.exe" -X utf8 -I tools/gen/normalize_word.py <режим> …
"""
import argparse
import hashlib
import re
import sys
import zipfile
from pathlib import Path

sys.dont_write_bytecode = True                     # без __pycache__ в tools/gen
sys.path.insert(0, str(Path(__file__).resolve().parent))

import pikepdf  # noqa: E402
from pikepdf import Name, String  # noqa: E402

import _genlib as g  # noqa: E402

AUTHOR_ASCII = "Ottisk fixtures"
ZIP_DATE = (1980, 1, 1, 0, 0, 0)


def norm_pdf(src: Path, dst: Path, title: str, subject: str):
    pdf = pikepdf.open(src)
    info = pdf.trailer.get("/Info")
    creator = str(info.get("/Creator", "Microsoft Word")) if info is not None else "Microsoft Word"
    producer = str(info.get("/Producer", "Microsoft Word")) if info is not None else "Microsoft Word"
    # теги подмножеств
    renamed = {}
    for obj in pdf.objects:
        if not isinstance(obj, pikepdf.Dictionary) or obj.get("/Type") != Name.FontDescriptor:
            continue
        fn = str(obj.get("/FontName", ""))[1:]
        m = re.match(r"^([A-Z]{6})\+(.+)$", fn)
        if not m:
            continue
        prog = None
        for k in ("/FontFile2", "/FontFile3", "/FontFile"):
            if k in obj:
                prog = obj[k].read_raw_bytes()
        h = hashlib.sha256((m.group(2)).encode() + (prog or b"")).digest()
        tag = "".join(chr(ord("A") + b % 26) for b in h[:6])
        renamed[fn] = tag + "+" + m.group(2)
        obj.FontName = Name("/" + renamed[fn])
    for obj in pdf.objects:
        if isinstance(obj, pikepdf.Dictionary) and obj.get("/Type") == Name.Font and "/BaseFont" in obj:
            bf = str(obj.BaseFont)[1:]
            if bf in renamed:
                obj.BaseFont = Name("/" + renamed[bf])
    lang = str(pdf.Root.get("/Lang", "ru-RU"))
    g.set_metadata(pdf, title, subject, lang=lang, creator=creator, doc_key=dst.name,
                   producer=producer, author=g.AUTHOR)
    if "/ID" in pdf.trailer:               # qpdf сохранил бы ID[0] Word (случайный)
        del pdf.trailer["/ID"]
    dst.parent.mkdir(parents=True, exist_ok=True)
    pdf.save(dst, deterministic_id=True, compress_streams=True,
             object_stream_mode=pikepdf.ObjectStreamMode.generate, fix_metadata_version=False,
             recompress_flate=False, linearize=False)


def _fix_xml(name: str, text: str) -> str:
    text = re.sub(r'\s+w:rsid[A-Za-z]*="[0-9A-Fa-f]+"', "", text)
    text = re.sub(r'\s+w14:(?:paraId|textId)="[0-9A-Fa-f]+"', "", text)
    text = re.sub(r'\s+wp14:(?:anchorId|editId)="[0-9A-Fa-f]+"', "", text)
    text = re.sub(r'<w:rsid w:val="[0-9A-Fa-f]+"/>', "", text)
    ids = {}
    for m in re.finditer(r'<wp:docPr id="(\d+)"', text):    # случайные id рисунков → 1, 2, …
        ids.setdefault(m.group(1), str(len(ids) + 1))
    if ids:
        text = re.sub(r'(<(?:wp:docPr|pic:cNvPr) id=")(\d+)(")',
                      lambda m: m.group(1) + ids.get(m.group(2), m.group(2)) + m.group(3), text)
    if name == "word/settings.xml":
        text = re.sub(r"<w:rsids>.*?</w:rsids>", "", text, flags=re.S)
        text = re.sub(r'(<w14:docId w14:val=")[0-9A-Fa-f]+(")', r"\g<1>0771A5B1\g<2>", text)
        text = re.sub(r'(<w15:docId w15:val=")\{[0-9A-Fa-f-]+\}(")',
                      r"\g<1>{0771A5B1-0000-4000-8000-000000000001}\g<2>", text)
    if name == "docProps/core.xml":
        text = re.sub(r"<dc:creator>.*?</dc:creator>", "<dc:creator>%s</dc:creator>" % AUTHOR_ASCII, text, flags=re.S)
        text = re.sub(r"<cp:lastModifiedBy>.*?</cp:lastModifiedBy>",
                      "<cp:lastModifiedBy>%s</cp:lastModifiedBy>" % AUTHOR_ASCII, text, flags=re.S)
        text = re.sub(r"(<dcterms:(?:created|modified)[^>]*>)[^<]*(</dcterms:)",
                      r"\g<1>2026-01-01T12:00:00Z\g<2>", text)
        text = re.sub(r"<cp:revision>\d+</cp:revision>", "<cp:revision>1</cp:revision>", text)
        text = re.sub(r"<cp:lastPrinted>[^<]*</cp:lastPrinted>", "", text)
    if name == "docProps/app.xml":
        text = re.sub(r"<TotalTime>\d+</TotalTime>", "<TotalTime>0</TotalTime>", text)
        text = re.sub(r"<Company>[^<]*</Company>", "<Company></Company>", text)
        text = re.sub(r"<Manager>[^<]*</Manager>", "", text)
    return text


def rezip(entries, dst: Path):
    dst.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(dst, "w") as z:
        for name, data, compress in entries:
            zi = zipfile.ZipInfo(name, date_time=ZIP_DATE)
            zi.compress_type = compress
            zi.create_system = 0
            zi.external_attr = 0
            z.writestr(zi, data, compress_type=compress, compresslevel=9 if compress == zipfile.ZIP_DEFLATED else None)


def norm_ooxml(src: Path, dst: Path):
    entries = []
    with zipfile.ZipFile(src) as z:
        for zi in z.infolist():
            data = z.read(zi.filename)
            if zi.filename.endswith((".xml", ".rels")):
                data = _fix_xml(zi.filename, data.decode("utf-8")).encode("utf-8")
            entries.append((zi.filename, data, zipfile.ZIP_DEFLATED))
    rezip(entries, dst)


def _group_end(s: str, start: int) -> int:
    depth = 0
    i = start
    while i < len(s):
        c = s[i]
        if c == "\\":
            i += 2
            continue
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    raise ValueError("незакрытая группа RTF")


def _drop_group(s: str, marker: str) -> str:
    while True:
        i = s.find(marker)
        if i < 0:
            return s
        s = s[:i] + s[_group_end(s, i):]


def norm_rtf(src: Path, dst: Path):
    s = src.read_bytes().decode("latin-1")
    s = _drop_group(s, "{\\*\\rsidtbl")
    s = _drop_group(s, "{\\*\\datastore")
    s = _drop_group(s, "{\\nonshppict")          # WMF-дубль картинки для старых читателей (×40 размера)
    # вместе со словом убираем и его пробел-разделитель, иначе пробел станет текстом
    s = re.sub(r"\\(?:ins|char|sect|para|tbl|sty|del|)rsid\d+ ?", "", s)
    s = re.sub(r"\\rsidroot\d+ ?", "", s)
    i = s.find("{\\info")
    if i >= 0:
        j = _group_end(s, i)
        fixed = ("{\\info{\\title Ottisk note}{\\author %s}{\\operator %s}"
                 "{\\creatim\\yr2026\\mo1\\dy1\\hr12\\min0}{\\revtim\\yr2026\\mo1\\dy1\\hr12\\min0}"
                 "{\\version1}{\\edmins0}}" % (AUTHOR_ASCII, AUTHOR_ASCII))
        s = s[:i] + fixed + s[j:]
    s = _renumber_fonts(s)
    s = _canon_lines(s)
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(s.encode("latin-1"))


_FONT_REF = re.compile(r"\\(f|af|deff|adeff|stshfdbch|stshfloch|stshfhich|stshfbi)(\d+)")


def _renumber_fonts(s: str) -> str:
    """Номера шрифтов в {\\fonttbl} зависят от сессии Word (сдвиг на 1) — нумеруем по порядку таблицы."""
    i = s.find("{\\fonttbl")
    if i < 0:
        return s
    tbl = s[i:_group_end(s, i)]
    order = []
    for m in re.finditer(r"\\f(\d+)", tbl):
        n = int(m.group(1))
        if n not in order:
            order.append(n)
    mapping = {n: k for k, n in enumerate(order)}
    return _FONT_REF.sub(lambda m: "\\" + m.group(1) + str(mapping.get(int(m.group(2)), int(m.group(2)))), s)


_CTRL_END = re.compile(r"(?<!\\)\\[a-zA-Z]+-?\d*$")


def _canon_lines(s: str) -> str:
    """Переносы строк RTF зависят от длины удалённых rsid — собираем заново.
    CR/LF в RTF игнорируются, но завершают управляющее слово: в этом случае ставим пробел
    (он тоже завершает слово и поглощается). Затем переносим перед «{\\» после ~200 символов."""
    parts = re.split(r"\r?\n", s)
    out = []
    for i, p in enumerate(parts):
        out.append(p)
        nxt = parts[i + 1][:1] if i < len(parts) - 1 else ""
        # пробел нужен, только если иначе управляющее слово слипнется со следующим символом
        if nxt and (nxt.isalnum() or nxt in " -") and _CTRL_END.search(p):
            out.append(" ")
    flat = "".join(out)
    lines, cur = [], 0
    for m in re.finditer(r"\{\\", flat):
        if m.start() - cur > 200:
            lines.append(flat[cur:m.start()])
            cur = m.start()
    lines.append(flat[cur:])
    return "\r\n".join(lines)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("mode", choices=["pdf", "docx", "rtf"])
    ap.add_argument("src")
    ap.add_argument("dst")
    ap.add_argument("--title", default="")
    ap.add_argument("--subject", default="")
    a = ap.parse_args()
    src, dst = Path(a.src), Path(a.dst)
    if a.mode == "pdf":
        norm_pdf(src, dst, a.title, a.subject)
    elif a.mode == "docx":
        norm_ooxml(src, dst)
    else:
        norm_rtf(src, dst)


if __name__ == "__main__":
    main()
