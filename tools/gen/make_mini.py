# -*- coding: utf-8 -*-
"""make_mini.py — мини-PDF для модульных тестов (tests/data/mini/, каждый ≤ 50 КБ).

Что:
  blank-a4.pdf   — одна пустая страница A4 (без /Contents);
  cyr-cid.pdf    — кириллица шрифтом Type0/CIDFontType2 Identity-H с ToUnicode
                   (подмножество Arial), «Привет, мир» извлекается mutool draw -F txt;
  type1-diff.pdf — Type1 Helvetica (base-14, не встроен) с /BaseEncoding /WinAnsiEncoding
                   и /Differences: латиница на кодах 1..N, кириллица на кодах cp1251 (afii100xx);
  enc-r6.pdf     — AES-256 V5/R6, пароль пользователя «user», владельца «owner» (pyHanko,
                   соли из детерминированного потока seed=1);
  truncated.pdf  — 3 страницы, обрезан хвост (xref, trailer, startxref и конец последнего
                   объекта) — MuPDF открывает с ремонтом;
  mini.json      — ожидания для тестов (страницы, текст, пароли, ремонт).
Откуда: свои страницы (pikepdf + _genlib), шрифт Arial из C:\\Windows\\Fonts.
Детерминизм: даты/XMP/ID фиксированы (_genlib.save_pdf), соли R6 — DetRandom(seed=1).
Запуск: "C:\\Tools\\venv-test\\Scripts\\python.exe" -X utf8 -I tools/gen/make_mini.py [--out <корень>]
"""
import sys
from pathlib import Path

sys.dont_write_bytecode = True                     # без __pycache__ в tools/gen
sys.path.insert(0, str(Path(__file__).resolve().parent))

import pikepdf  # noqa: E402
from pikepdf import Array, Dictionary, Name, Stream, String  # noqa: E402

import _genlib as g  # noqa: E402

LIMIT = 50 * 1024


def blank_a4(out: Path):
    pdf = pikepdf.new()
    pdf.pages.append(pikepdf.Page(Dictionary(Type=Name.Page, MediaBox=Array([0, 0, 595.2756, 841.8898]),
                                             Resources=Dictionary())))
    g.save_pdf(pdf, out, title="blank-a4", subject="Пустая страница A4", creator="tools/gen/make_mini.py")


def cyr_cid(out: Path):
    doc = g.Doc("cyr-cid", "Кириллица шрифтом CID Identity-H с ToUnicode", creator="tools/gen/make_mini.py")
    f = doc.font("arial")
    cv = doc.canvas()
    cv.text(72, 740, "Привет, мир", f, 28)
    cv.text(72, 700, "Шрифт Type0 / CIDFontType2, кодировка Identity-H, есть ToUnicode.", f, 12)
    cv.text(72, 680, "Съешь же ещё этих мягких французских булок, да выпей же чаю.", f, 12)
    cv.text(72, 660, "ЁЖИК В ТУМАНЕ: 0123456789 — №5 «кавычки».", f, 12)
    doc.add_page(cv)
    doc.save(out)


def _cyr_names():
    """Коды cp1251 0xC0..0xFF → имена afii100xx, плюс Ё (0xA8) и ё (0xB8)."""
    res = {0xA8: "afii10023", 0xB8: "afii10071"}
    for i in range(32):
        res[0xC0 + i] = g.agl_name(chr(0x0410 + i))
        res[0xE0 + i] = g.agl_name(chr(0x0430 + i))
    return res


def type1_diff(out: Path):
    pdf = pikepdf.new()
    arial = g.TrueType(g.FONTS_DIR / "arial.ttf")   # метрики Arial = метрики Helvetica
    line1 = "Differences: Type1 Helvetica"
    remap = {}
    for ch in line1:
        if ch not in remap:
            remap[ch] = len(remap) + 1                   # коды 1..N — не ASCII
    diffs = Array([1] + [Name("/" + g.agl_name(ch)) for ch in remap])
    cyr = _cyr_names()
    diffs.append(0xA8)
    diffs.append(Name("/" + cyr[0xA8]))
    diffs.append(0xB8)
    diffs.append(Name("/" + cyr[0xB8]))
    diffs.append(0xC0)
    for c in range(0xC0, 0x100):
        diffs.append(Name("/" + cyr[c]))
    widths = {}
    for ch, code in remap.items():
        widths[code] = ch
    line2 = "Привет, мир — Ёлка, ёж, щука."
    line3 = "Hello, world! WinAnsi: 5 € ± 2 § ©."    # коды < 0xC0 и не 0xA8/0xB8
    b1 = bytes(remap[ch] for ch in line1)
    b2 = line2.encode("cp1251")
    b3 = line3.encode("cp1252")
    for b, s in ((b2, line2), (b3, line3)):
        for code, ch in zip(b, s):
            widths[code] = ch
    first, last = 1, 255
    warr = Array([round(arial.advance(arial.gid(widths[c])) * 1000 / arial.units) if c in widths else 0
                  for c in range(first, last + 1)])
    font = pdf.make_indirect(Dictionary(
        Type=Name.Font, Subtype=Name.Type1, BaseFont=Name.Helvetica, FirstChar=first, LastChar=last,
        Widths=warr, Encoding=Dictionary(Type=Name.Encoding, BaseEncoding=Name.WinAnsiEncoding,
                                         Differences=diffs)))

    def tj(b: bytes) -> str:
        return "<" + b.hex().upper() + ">"
    content = "\n".join([
        "BT /F1 20 Tf 72 740 Td %s Tj ET" % tj(b1),
        "BT /F1 20 Tf 72 700 Td %s Tj ET" % tj(b2),
        "BT /F1 14 Tf 72 664 Td %s Tj ET" % tj(b3),
    ]) + "\n"
    page = Dictionary(Type=Name.Page, MediaBox=Array([0, 0, 595.2756, 841.8898]),
                      Resources=Dictionary(Font=Dictionary(F1=font)),
                      Contents=pdf.make_indirect(Stream(pdf, content.encode("latin-1"))))
    pdf.pages.append(pikepdf.Page(page))
    g.save_pdf(pdf, out, title="type1-diff", subject="Type1 base-14 с /Differences",
               creator="tools/gen/make_mini.py")
    return line1, line2, line3


def enc_r6(out: Path, tmp: Path):
    doc = g.Doc("enc-r6", "AES-256 R6", creator="tools/gen/make_mini.py")
    f = doc.font("arial")
    cv = doc.canvas()
    cv.text(72, 740, "Зашифровано: AES-256, V5/R6", f, 20)
    cv.text(72, 710, "Пароль пользователя: user; пароль владельца: owner.", f, 12)
    doc.add_page(cv)
    plain = tmp / "enc-r6-plain.pdf"
    doc.save(plain)
    g.encrypt_aes256_r6(plain, out, owner="owner", user="user", seed=g.SEED)


def truncated(out: Path, tmp: Path):
    doc = g.Doc("truncated", "Обрезанный хвост: нет xref и trailer", creator="tools/gen/make_mini.py")
    f = doc.font("arial")
    for i in range(1, 4):
        cv = doc.canvas()
        cv.text(72, 740, "Страница %d из 3 — файл с обрезанным хвостом" % i, f, 16)
        cv.text(72, 710, "Truncated tail: page %d" % i, f, 12)
        doc.add_page(cv)
    full = tmp / "truncated-full.pdf"
    doc.save(full)
    data = full.read_bytes()
    pos = data.rfind(b"\nxref")
    if pos < 0:
        raise RuntimeError("нет классической таблицы xref")
    cut = pos - 3                                    # режем внутри «endobj» последнего объекта
    out.write_bytes(data[:cut])
    return len(data), cut


def main():
    args = g.common_args(__doc__)
    root = Path(args.out)
    dst = root / "tests" / "data" / "mini"
    dst.mkdir(parents=True, exist_ok=True)
    tmp = g.tmp_dir(args, "mini")
    blank_a4(dst / "blank-a4.pdf")
    cyr_cid(dst / "cyr-cid.pdf")
    l1, l2, l3 = type1_diff(dst / "type1-diff.pdf")
    enc_r6(dst / "enc-r6.pdf", tmp)
    full_len, cut = truncated(dst / "truncated.pdf", tmp)
    meta = {
        "generator": "tools/gen/make_mini.py",
        "files": {
            "blank-a4.pdf": {"pages": 1, "size_pt": [595.2756, 841.8898], "text": ""},
            "cyr-cid.pdf": {"pages": 1, "text_contains": ["Привет, мир", "Съешь же ещё"],
                            "font": "Type0/CIDFontType2 Identity-H + ToUnicode (подмножество Arial)"},
            "type1-diff.pdf": {"pages": 1, "text_contains": [l1, l2, l3],
                               "font": "Type1 Helvetica base-14, не встроен, /WinAnsiEncoding + /Differences"},
            "enc-r6.pdf": {"pages": 1, "encryption": "AES-256 V5/R6", "user_password": "user",
                           "owner_password": "owner", "text_contains": ["Зашифровано: AES-256"]},
            "truncated.pdf": {"pages": 3, "repair": True, "full_size": full_len, "truncated_at": cut,
                              "text_contains": ["Страница 1 из 3", "Страница 3 из 3"]},
        },
    }
    g.write_json(dst / "mini.json", meta)
    for name in meta["files"]:
        g.check_size(dst / name, LIMIT)
    # самопроверка извлечения текста
    txt = g.pdf_text(dst / "cyr-cid.pdf")
    if "Привет, мир" not in txt:
        raise RuntimeError("cyr-cid.pdf: mutool не извлёк «Привет, мир»: %r" % txt[:200])
    txt = g.pdf_text(dst / "enc-r6.pdf", password="user")
    if "Зашифровано" not in txt:
        raise RuntimeError("enc-r6.pdf: текст не извлечён с паролем user")
    for name in meta["files"]:
        g.say("mini: %-15s %6d байт" % (name, (dst / name).stat().st_size))


if __name__ == "__main__":
    main()
