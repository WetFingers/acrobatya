# -*- coding: utf-8 -*-
"""make_scans.py — 10 «сканов» с эталонами: tests/fixtures/scans/scan-NN.{pdf,txt} + scans.json.

Что: страница A4 с текстом (русский / английский / немецкий, Arial или Times New Roman)
рендерится mutool draw в 300 dpi (оттенки серого), затем: перекос ±3°, лёгкое размытие,
тон бумаги, шум и редкие точки, поворот 0/90/180/270 (страница положена в сканер боком
или вверх ногами), и кладётся в PDF как растр на всю страницу: JPEG (серый или цветной)
или 1 бит Flate (как ч/б скан). scan-NN.txt — эталонный текст (UTF-8, строки как на странице);
по нему рубеж Р4 считает долю слов OCR. scans.json — язык, поворот, перекос, режим, число слов.
Откуда: свои тексты (_texts.py), шрифты C:\\Windows\\Fonts, mutool 1.28.5, Pillow, numpy.
Детерминизм: шум — numpy.random.default_rng(seed=1+N), JPEG/zlib Pillow детерминированы,
PDF — _genlib.save_pdf (фиксированные /Info, XMP, /ID). Каждый файл ≤ 1,5 МБ.
Запуск: "C:\\Tools\\venv-test\\Scripts\\python.exe" -X utf8 -I tools/gen/make_scans.py [--out <корень>]
Живой OCR-замер (не часть make_all): tools/gen/check_scans_ocr.py.
"""
import io
import random
import sys
from pathlib import Path

sys.dont_write_bytecode = True                     # без __pycache__ в tools/gen
sys.path.insert(0, str(Path(__file__).resolve().parent))

import numpy as np  # noqa: E402
import pikepdf  # noqa: E402
from PIL import Image, ImageFilter  # noqa: E402
from pikepdf import Array, Dictionary, Name, Stream  # noqa: E402

import _genlib as g  # noqa: E402
import _texts as tx  # noqa: E402

LIMIT = 1536 * 1024
DPI = 300

# n, языки текста, tesseract-языки, шрифт, поворот (по часовой), перекос (°, + против часовой), режим
SPECS = [
    (1, "ru", "rus", "arial", 0, 1.0, "gray"),
    (2, "en", "eng", "times", 0, -2.0, "gray"),
    (3, "de", "deu", "arial", 0, 3.0, "gray"),
    (4, "ru", "rus", "times", 90, -1.5, "gray"),
    (5, "en", "eng", "arial", 180, 2.5, "gray"),
    (6, "de", "deu", "times", 270, -3.0, "gray"),
    (7, "ru+en", "rus+eng", "arial", 0, -0.5, "bw"),
    (8, "ru", "rus", "times", 180, 1.5, "bw"),
    (9, "de+en", "deu+eng", "arial", 90, 0.8, "rgb"),
    (10, "ru", "rus", "arial", 270, -2.2, "rgb"),
]

TITLES = {
    "ru": "Служебная записка", "en": "Office memo", "de": "Interne Mitteilung",
    "ru+en": "Записка / Memo", "de+en": "Mitteilung / Memo",
}


def paragraphs(lang: str, rng: random.Random):
    pools = {"ru": tx.RU, "en": tx.EN, "de": tx.DE}
    out = []
    for part in lang.split("+"):
        pool = list(pools[part])
        rng.shuffle(pool)
        k = 5 if "+" not in lang else 3
        for i in range(0, k * 2, 2):
            out.append(" ".join(pool[i % len(pool):i % len(pool) + 2]))
    return out


def text_page(path: Path, n: int, lang: str, font: str):
    rng = random.Random(1000 + n)
    doc = g.Doc("scan-%02d source" % n, creator="tools/gen/make_scans.py")
    f = doc.font(font)
    fb = doc.font(font + "bd")
    cv = doc.canvas()
    lines = []
    title = "%s № %d" % (TITLES[lang], n)
    cv.text(72, 770, title, fb, 20)
    lines.append(title)
    y = 730
    size = 13
    for par in paragraphs(lang, rng):
        for line in g.wrap(par, f, size, 450):
            cv.text(72, y, line, f, size)
            lines.append(line)
            y -= size * 1.45
        y -= size * 0.8
        if y < 100:
            break
    doc.add_page(cv)
    doc.save(path)
    return lines


def degrade(png: Path, n: int, rot: int, skew: float, mode: str) -> Image.Image:
    rng = np.random.default_rng(g.SEED + n)
    img = Image.open(png).convert("L")
    img = img.rotate(skew, resample=Image.BICUBIC, expand=False, fillcolor=255)
    img = img.filter(ImageFilter.GaussianBlur(0.6))
    a = np.asarray(img).astype(np.float32)
    a = 28 + a * (217.0 / 255.0)                       # тон бумаги и «не чёрная» краска
    a += rng.normal(0, 2.5, a.shape)
    specks = rng.random(a.shape) < 0.00015
    a[specks] = 40
    a = np.clip(a, 0, 255).astype(np.uint8)
    img = Image.fromarray(a, "L")
    if rot:
        img = img.rotate(-rot, expand=True)            # PIL: + против часовой → «-» = по часовой
    if mode == "bw":
        return img.point(lambda v: 255 if v > 150 else 0).convert("1")
    if mode == "rgb":
        g_ = np.asarray(img).astype(np.float32) / 255.0
        tint = np.array([1.0, 0.975, 0.91], dtype=np.float32)
        rgb = np.clip(g_[..., None] * tint[None, None, :] * 255.0, 0, 255).astype(np.uint8)
        return Image.fromarray(rgb, "RGB")
    return img


def scan_pdf(out: Path, img: Image.Image, n: int, lang: str):
    pdf = pikepdf.new()
    if img.mode == "1":
        xo = g.flate_xobject(pdf, img)
    else:
        buf = io.BytesIO()
        img.save(buf, "JPEG", quality=70, optimize=False, progressive=False)
        st = Stream(pdf, buf.getvalue())
        st.Type = Name.XObject
        st.Subtype = Name.Image
        st.Width, st.Height = img.size
        st.ColorSpace = Name.DeviceGray if img.mode == "L" else Name.DeviceRGB
        st.BitsPerComponent = 8
        st.Filter = Name.DCTDecode
        xo = pdf.make_indirect(st)
    w = img.size[0] * 72.0 / DPI
    h = img.size[1] * 72.0 / DPI
    content = ("q %.4f 0 0 %.4f 0 0 cm /Im1 Do Q\n" % (w, h)).encode("ascii")
    pdf.pages.append(pikepdf.Page(Dictionary(
        Type=Name.Page, MediaBox=Array([0, 0, round(w, 4), round(h, 4)]),
        Resources=Dictionary(XObject=Dictionary(Im1=xo)),
        Contents=pdf.make_indirect(Stream(pdf, content)))))
    lang_tag = {"ru": "ru-RU", "en": "en-US", "de": "de-DE"}[lang.split("+")[0]]
    g.save_pdf(pdf, out, title="scan-%02d" % n, subject="Скан для OCR (эталон scan-%02d.txt)" % n,
               lang=lang_tag, creator="tools/gen/make_scans.py")


def main():
    args = g.common_args(__doc__)
    dst = Path(args.out) / "tests" / "fixtures" / "scans"
    dst.mkdir(parents=True, exist_ok=True)
    tmp = g.tmp_dir(args, "scans")
    meta = []
    for n, lang, tess, font, rot, skew, mode in SPECS:
        src = tmp / ("src-%02d.pdf" % n)
        lines = text_page(src, n, lang, font)
        png = tmp / ("src-%02d.png" % n)
        g.run([g.mutool_path(), "draw", "-q", "-r", str(DPI), "-c", "gray", "-o", png, src, "1"])
        img = degrade(png, n, rot, skew, mode)
        out = dst / ("scan-%02d.pdf" % n)
        scan_pdf(out, img, n, lang)
        g.check_size(out, LIMIT)
        g.write_text(dst / ("scan-%02d.txt" % n), "\n".join(lines) + "\n")
        words = sum(len(l.split()) for l in lines)
        meta.append({"file": out.name, "truth": "scan-%02d.txt" % n, "lang": lang, "tesseract": tess,
                     "font": font, "rotation_cw": rot, "skew_deg_ccw": skew, "mode": mode, "dpi": DPI,
                     "pixels": list(img.size), "words": words, "bytes": out.stat().st_size})
        g.say("scans: %s %-6s rot %3d skew %+.1f %-4s %7d байт, слов %d" % (
            out.name, lang, rot, skew, mode, out.stat().st_size, words))
    g.write_json(dst / "scans.json", {
        "generator": "tools/gen/make_scans.py",
        "note": "rotation_cw — на сколько градусов по часовой повёрнуто содержимое; чтобы выпрямить, "
                "повернуть против часовой. skew_deg_ccw — перекос против часовой. Эталон — scan-NN.txt.",
        "plan_names": {
            "scan-ru.pdf": "scan-01.pdf (план 8.1: русский, 300 dpi, без поворота; эталон scan-01.txt)",
            "перекос 3°": "scan-03.pdf (+3,0° против часовой, без поворота; fz_detect_skew ≈ 3)",
            "повороты 90/180/270": "90 — scan-04, scan-09; 180 — scan-05, scan-08; 270 — scan-06, scan-10",
        },
        "scans": meta})


if __name__ == "__main__":
    main()
