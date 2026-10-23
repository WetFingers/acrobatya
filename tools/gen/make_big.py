# -*- coding: utf-8 -*-
"""make_big.py — большие файлы вне Git: <corpus>\\gen\\{big-1200.pdf, images-200.pdf, big.json}.

Что:
  big-1200.pdf   — 1 200 страниц A4 текста (Times New Roman и Arial, кириллица), колонтитулы,
                   закладки: 12 глав по 100 страниц × 10 разделов, метки страниц; объектные потоки;
  images-200.pdf — 200 страниц, на каждой картинка: JPEG RGB 2000×1500 (большинство), JPEG в
                   оттенках серого (каждая 7-я), Flate RGB с маской SMask (каждая 10-я), подпись;
  big.json       — число страниц, размеры, sha256 (для манифеста корпуса и рубежа Р1).
Каталог по умолчанию — %OTTISK_CORPUS%\\gen или %LOCALAPPDATA%\\ottisk\\corpus\\gen (--corpus).
Откуда: _genlib (pikepdf), тексты _texts.py, картинки — процедурные (numpy + Pillow).
Детерминизм: random.Random / numpy.default_rng с seed=1, сохранение через _genlib.save_pdf.
Запуск: "C:\\Tools\\venv-test\\Scripts\\python.exe" -X utf8 -I tools/gen/make_big.py [--corpus <каталог>] [--skip-existing]
"""
import random
import sys
import time
from pathlib import Path

sys.dont_write_bytecode = True                     # без __pycache__ в tools/gen
sys.path.insert(0, str(Path(__file__).resolve().parent))

import numpy as np  # noqa: E402
from PIL import Image, ImageDraw  # noqa: E402
from pikepdf import Array, Dictionary, Name, OutlineItem  # noqa: E402

import _genlib as g  # noqa: E402
import _texts as tx  # noqa: E402

W, H = g.A4


def big_text(out: Path, pages: int = 1200):
    doc = g.Doc("Большой документ: 1200 страниц", "corpus/gen/big-1200.pdf", creator="tools/gen/make_big.py")
    f = doc.font("times")
    fh = doc.font("arialbd")
    fa = doc.font("arial")
    rng = random.Random(g.SEED)
    for p in range(1, pages + 1):
        ch = (p - 1) // 100 + 1
        sec = (p - 1) % 100 // 10 + 1
        cv = doc.canvas()
        cv.text(56, H - 40, "Глава %d · раздел %d.%d" % (ch, ch, sec), fa, 8, rgb=(0.4, 0.4, 0.45))
        lab = "%d / %d" % (p, pages)
        cv.text(W - 56 - fa.width(lab, 9), 30, lab, fa, 9)
        y = H - 80
        if (p - 1) % 10 == 0:
            cv.text(56, y, "Раздел %d.%d" % (ch, sec), fh, 16)
            y -= 28
        while y > 70:
            para = tx.sentences(rng, rng.randint(2, 4))
            y = cv.paragraph(56, y, W - 112, para, f, 11, leading=14, bottom=60)
            if y is None:
                break
            y -= 8
        doc.add_page(cv)
    pdf = doc.pdf
    with pdf.open_outline() as ol:
        for ch in range(1, pages // 100 + 1):
            item = OutlineItem("Глава %d" % ch, (ch - 1) * 100)
            for sec in range(1, 11):
                item.children.append(OutlineItem("Раздел %d.%d" % (ch, sec), (ch - 1) * 100 + (sec - 1) * 10))
            ol.root.append(item)
    pdf.Root.PageLabels = Dictionary(Nums=Array([0, Dictionary(S=Name.D)]))
    doc.save(out, objstm=True)


def picture(rng, w, h, seed_shift=0):
    y = np.linspace(0, 1, h, dtype=np.float32)[:, None]
    x = np.linspace(0, 1, w, dtype=np.float32)[None, :]
    c0 = rng.uniform(20, 235, 3)
    c1 = rng.uniform(20, 235, 3)
    base = c0[None, None, :] * (1 - y[..., None]) + c1[None, None, :] * y[..., None]
    base = base + 25 * np.sin(x[..., None] * rng.uniform(3, 12) + seed_shift)
    img = Image.fromarray(np.clip(base, 0, 255).astype(np.uint8), "RGB")
    d = ImageDraw.Draw(img)
    for _ in range(int(rng.integers(6, 14))):
        cx, cy = rng.uniform(0, w), rng.uniform(0, h)
        r = rng.uniform(w * 0.03, w * 0.18)
        col = tuple(int(v) for v in rng.integers(0, 256, 3))
        if rng.random() < 0.5:
            d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=col)
        else:
            d.rectangle([cx - r, cy - r * 0.6, cx + r, cy + r * 0.6], fill=col)
    return img


def big_images(out: Path, pages: int = 200):
    doc = g.Doc("200 страниц с картинками", "corpus/gen/images-200.pdf", creator="tools/gen/make_big.py")
    fa = doc.font("arial")
    rng = np.random.default_rng(g.SEED)
    kinds = {"jpeg-rgb": 0, "jpeg-gray": 0, "flate-rgb-smask": 0}
    for p in range(1, pages + 1):
        cv = doc.canvas()
        if p % 10 == 0:
            img = picture(rng, 1000, 750, p)
            a = np.zeros((750, 1000), dtype=np.uint8)
            yy, xx = np.mgrid[0:750, 0:1000]
            a[((xx - 500) ** 2 / 500.0 ** 2 + (yy - 375) ** 2 / 375.0 ** 2) < 1.0] = 255
            xo = g.flate_xobject(doc.pdf, img, smask=Image.fromarray(a, "L"))
            kind = "flate-rgb-smask"
        elif p % 7 == 0:
            xo = g.jpeg_xobject(doc.pdf, picture(rng, 2000, 1500, p).convert("L"), quality=80)
            kind = "jpeg-gray"
        else:
            xo = g.jpeg_xobject(doc.pdf, picture(rng, 2000, 1500, p), quality=80)
            kind = "jpeg-rgb"
        kinds[kind] += 1
        iw = W - 112
        ih = iw * 0.75
        cv.image(xo, 56, H - 80 - ih, iw, ih)
        cv.text(56, H - 100 - ih, "Страница %d из %d · %s" % (p, pages, kind), fa, 12)
        doc.add_page(cv)
    doc.save(out, objstm=True)
    return kinds


def main():
    def extra(ap):
        ap.add_argument("--skip-existing", action="store_true",
                        help="файлы уже есть здесь или в корпусе — ничего не делать (фикстура bench.engine.generate)")
    args = g.common_args(__doc__, corpus=True, extra=extra)
    dst = Path(args.corpus)
    dst.mkdir(parents=True, exist_ok=True)
    if args.skip_existing:
        # Готовые — в каталоге вывода или в корпусе (%OTTISK_CORPUS%\gen): файлы детерминированы, те же байты.
        for have in (dst, g.corpus_gen_dir()):
            if all((have / n).exists() for n in ("big.json", "big-1200.pdf", "images-200.pdf")):
                g.say("big: уже есть в %s — пропуск" % have)
                return
    t0 = time.time()
    big_text(dst / "big-1200.pdf")
    t1 = time.time()
    kinds = big_images(dst / "images-200.pdf")
    t2 = time.time()
    info = {"generator": "tools/gen/make_big.py",
            "big-1200.pdf": {"pages": 1200, "bytes": (dst / "big-1200.pdf").stat().st_size,
                             "sha256": g.sha256_file(dst / "big-1200.pdf"), "outline": "12 глав × 10 разделов"},
            "images-200.pdf": {"pages": 200, "bytes": (dst / "images-200.pdf").stat().st_size,
                               "sha256": g.sha256_file(dst / "images-200.pdf"), "images": kinds}}
    g.write_json(dst / "big.json", info)
    g.say("big: big-1200.pdf %d байт за %.1f с; images-200.pdf %d байт за %.1f с" % (
        info["big-1200.pdf"]["bytes"], t1 - t0, info["images-200.pdf"]["bytes"], t2 - t1))


if __name__ == "__main__":
    main()
