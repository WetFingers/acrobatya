# -*- coding: utf-8 -*-
"""make_images.py — картинки-фикстуры: tests/fixtures/images/*.

Что:
  photo.jpg         — «пейзаж» 1200×800 RGB, JPEG q88 (для Office-файлов, демо, создания PDF из картинки);
  logo.png          — 512×512 RGBA с прозрачностью и кириллицей «Оттиск»;
  chart.png         — 800×500 RGB столбчатая диаграмма с подписями;
  page-gray.png     — страница A4 150 dpi в оттенках серого с текстом (как снимок документа);
  cmyk.jpg          — 600×400 CMYK JPEG (маркер Adobe — частая ловушка инверсии);
  multipage-g4.tif  — 2 страницы 1-бит CCITT G4, 200 dpi (как факс/скан);
  images.json       — размеры, режимы, назначение.
Откуда: Pillow + numpy, шрифт Arial из C:\\Windows\\Fonts; содержание — процедурное.
Детерминизм: numpy.random.default_rng(seed=1), без дат в метаданных; кодеки Pillow детерминированы.
Запуск: "C:\\Tools\\venv-test\\Scripts\\python.exe" -X utf8 -I tools/gen/make_images.py [--out <корень>]
"""
import sys
from pathlib import Path

sys.dont_write_bytecode = True                     # без __pycache__ в tools/gen
sys.path.insert(0, str(Path(__file__).resolve().parent))

import numpy as np  # noqa: E402
from PIL import Image, ImageDraw, ImageFont  # noqa: E402

import _genlib as g  # noqa: E402
import _texts as tx  # noqa: E402


def font(size, bold=False):
    return ImageFont.truetype(str(g.FONTS_DIR / ("arialbd.ttf" if bold else "arial.ttf")), size)


def photo(rng, w=1200, h=800) -> Image.Image:
    y = np.linspace(0, 1, h)[:, None]
    x = np.linspace(0, 1, w)[None, :]
    sky = np.stack([90 + 120 * y + 0 * x, 140 + 90 * y + 0 * x, 230 - 40 * y + 0 * x], axis=-1)
    img = Image.fromarray(np.clip(sky, 0, 255).astype(np.uint8), "RGB")
    d = ImageDraw.Draw(img)
    d.ellipse([w * 0.72, h * 0.12, w * 0.84, h * 0.30], fill=(255, 226, 120))
    for k, (base, col) in enumerate(((0.55, (70, 95, 120)), (0.66, (50, 110, 70)), (0.78, (35, 85, 45)))):
        pts = [(0, h)]
        n = 14
        for i in range(n + 1):
            px = w * i / n
            py = h * base - rng.uniform(20, 120 - k * 25)
            pts.append((px, py))
        pts.append((w, h))
        d.polygon(pts, fill=col)
    arr = np.asarray(img).astype(np.int16)
    arr += rng.normal(0, 6, arr.shape).astype(np.int16)
    return Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGB")


def logo() -> Image.Image:
    img = Image.new("RGBA", (512, 512), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.ellipse([16, 16, 496, 496], fill=(28, 72, 140, 255))
    d.ellipse([60, 60, 452, 452], fill=(255, 255, 255, 230))
    d.text((256, 230), "О", font=font(220, True), fill=(28, 72, 140, 255), anchor="mm")
    d.text((256, 390), "Оттиск", font=font(64, True), fill=(200, 40, 40, 255), anchor="mm")
    return img


def chart() -> Image.Image:
    img = Image.new("RGB", (800, 500), (255, 255, 255))
    d = ImageDraw.Draw(img)
    f = font(18)
    d.text((400, 24), "Страницы в день (пример)", font=font(24, True), fill=(20, 20, 20), anchor="mm")
    vals = [12, 30, 22, 45, 38, 51, 27]
    days = ["пн", "вт", "ср", "чт", "пт", "сб", "вс"]
    x0, y0, bw = 90, 440, 80
    d.line([(70, 60), (70, y0), (770, y0)], fill=(60, 60, 60), width=2)
    for i, (v, dname) in enumerate(zip(vals, days)):
        x = x0 + i * (bw + 15)
        d.rectangle([x, y0 - v * 7, x + bw, y0], fill=(52, 120, 200))
        d.text((x + bw / 2, y0 - v * 7 - 14), str(v), font=f, fill=(20, 20, 20), anchor="mm")
        d.text((x + bw / 2, y0 + 18), dname, font=f, fill=(20, 20, 20), anchor="mm")
    return img


def page_gray(rng) -> Image.Image:
    w, h = 1240, 1754
    img = Image.new("L", (w, h), 250)
    d = ImageDraw.Draw(img)
    d.text((110, 120), "Снимок страницы", font=font(40, True), fill=20)
    y = 200
    for par in tx.RU[:6]:
        words = par.split()
        line = ""
        for wd in words:
            cand = (line + " " + wd).strip()
            if d.textlength(cand, font=font(26)) > w - 220:
                d.text((110, y), line, font=font(26), fill=30)
                y += 38
                line = wd
            else:
                line = cand
        d.text((110, y), line, font=font(26), fill=30)
        y += 60
    return img                                         # без шума: PNG остаётся маленьким


def fax_pages():
    pages = []
    for n in (1, 2):
        img = Image.new("L", (1654, 2339), 255)        # A4, 200 dpi
        d = ImageDraw.Draw(img)
        d.text((150, 150), "Факс, страница %d" % n, font=font(56, True), fill=0)
        y = 280
        for par in (tx.RU[6:10] if n == 1 else tx.DE[:4]):
            words = par.split()
            line = ""
            for wd in words:
                cand = (line + " " + wd).strip()
                if d.textlength(cand, font=font(34)) > 1350:
                    d.text((150, y), line, font=font(34), fill=0)
                    y += 50
                    line = wd
                else:
                    line = cand
            d.text((150, y), line, font=font(34), fill=0)
            y += 80
        pages.append(img.point(lambda v: 255 if v > 128 else 0).convert("1"))
    return pages


def main():
    args = g.common_args(__doc__)
    dst = Path(args.out) / "tests" / "fixtures" / "images"
    dst.mkdir(parents=True, exist_ok=True)
    rng = np.random.default_rng(g.SEED)
    photo(rng).save(dst / "photo.jpg", "JPEG", quality=88, optimize=False, progressive=False)
    logo().save(dst / "logo.png", "PNG", optimize=False, compress_level=9)
    chart().save(dst / "chart.png", "PNG", optimize=False, compress_level=9)
    page_gray(rng).save(dst / "page-gray.png", "PNG", optimize=False, compress_level=9)
    cm = photo(np.random.default_rng(g.SEED + 7), 600, 400).convert("CMYK")
    cm.save(dst / "cmyk.jpg", "JPEG", quality=85, optimize=False, progressive=False)
    p1, p2 = fax_pages()
    p1.save(dst / "multipage-g4.tif", "TIFF", compression="group4", dpi=(200, 200), save_all=True,
            append_images=[p2])
    meta = {}
    for f in sorted(dst.iterdir()):
        if f.suffix.lower() in (".jpg", ".png", ".tif"):
            im = Image.open(f)
            meta[f.name] = {"size": list(im.size), "mode": im.mode, "frames": getattr(im, "n_frames", 1),
                            "bytes": f.stat().st_size}
    meta["_purpose"] = ("photo/logo/chart — вставка в Office-файлы и демо; page-gray — снимок страницы; "
                        "cmyk.jpg — CMYK JPEG с маркером Adobe; multipage-g4.tif — многостраничный TIFF G4 "
                        "для «PDF из картинок»")
    g.write_json(dst / "images.json", meta)
    for k, v in meta.items():
        if not k.startswith("_"):
            g.say("images: %-17s %s %s %7d байт" % (k, v["size"], v["mode"], v["bytes"]))


if __name__ == "__main__":
    main()
