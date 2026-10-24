"""Фикстура картинок редактора (план 3.4, п. 3): tests/fixtures/edit/images.pdf и images.json.

Страница Letter: заголовок base14, картинка Im1 — JPEG 64 × 48 (DCTDecode) в группе «q 192 0 0 144 72 480 cm /Im1
Do Q», картинка Im2 — RGB 32 × 32 (FlateDecode) без q/Q: «200 0 0 100 320 500 cm /Im2 Do» (CTM после неё не
единичная — добавление картинки обязано это учесть). Детерминированно: фиксированный /ID, без дат, JPEG из
Pillow с одними и теми же параметрами. Запуск: "$OTTISK_PYTHON" -X utf8 -I tools/gen/make_image_fixtures.py
"""

from __future__ import annotations

import io
import json
import zlib
from pathlib import Path

import pikepdf
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "tests" / "fixtures" / "edit"


def jpeg_bytes() -> bytes:
    img = Image.new("RGB", (64, 48))
    px = img.load()
    for y in range(48):
        for x in range(64):
            px[x, y] = (40 + x * 3, 90 + y * 2, 200 - x * 2)
    buf = io.BytesIO()
    img.save(buf, format="JPEG", quality=85, optimize=False, progressive=False)
    return buf.getvalue()


def rgb_bytes() -> bytes:
    out = bytearray()
    for y in range(32):
        for x in range(32):
            out += bytes(((x * 8) & 255, (y * 8) & 255, 128 if (x // 8 + y // 8) % 2 else 32))
    return bytes(out)


def main() -> None:
    pdf = pikepdf.new()
    font = pdf.make_indirect(
        pikepdf.Dictionary(Type=pikepdf.Name.Font, Subtype=pikepdf.Name.Type1, BaseFont=pikepdf.Name.Helvetica,
                           Encoding=pikepdf.Name.WinAnsiEncoding))
    im1 = pikepdf.Stream(pdf, jpeg_bytes())
    im1.Type = pikepdf.Name.XObject
    im1.Subtype = pikepdf.Name.Image
    im1.Width = 64
    im1.Height = 48
    im1.ColorSpace = pikepdf.Name.DeviceRGB
    im1.BitsPerComponent = 8
    im1.Filter = pikepdf.Name.DCTDecode
    im2 = pikepdf.Stream(pdf, zlib.compress(rgb_bytes(), 9))
    im2.Type = pikepdf.Name.XObject
    im2.Subtype = pikepdf.Name.Image
    im2.Width = 32
    im2.Height = 32
    im2.ColorSpace = pikepdf.Name.DeviceRGB
    im2.BitsPerComponent = 8
    im2.Filter = pikepdf.Name.FlateDecode
    content = (b"BT /F1 18 Tf 72 720 Td (Images for the editor) Tj ET\n"
               b"q 192 0 0 144 72 480 cm /Im1 Do Q\n"
               b"200 0 0 100 320 500 cm /Im2 Do\n")
    page = pikepdf.Dictionary(
        Type=pikepdf.Name.Page,
        MediaBox=[0, 0, 612, 792],
        Resources=pikepdf.Dictionary(Font=pikepdf.Dictionary(F1=font),
                                     XObject=pikepdf.Dictionary(Im1=im1, Im2=im2)),
        Contents=pikepdf.Stream(pdf, content),
    )
    pdf.pages.append(pikepdf.Page(page))
    OUT.mkdir(parents=True, exist_ok=True)
    pdf.save(OUT / "images.pdf", static_id=True, compress_streams=True, object_stream_mode=pikepdf.ObjectStreamMode.disable)
    # Ожидания: рамки на странице (координаты MuPDF — y вниз от верхнего края 792).
    expected = {
        "about": "images.pdf — две картинки: Im1 JPEG в группе q cm Do Q, Im2 Flate без q/Q",
        "pageHeight": 792,
        "images": [
            {"name": "Im1", "filter": "DCTDecode", "width": 64, "height": 48, "bbox": [72, 168, 264, 312],
             "group": True},
            {"name": "Im2", "filter": "FlateDecode", "width": 32, "height": 32, "bbox": [320, 192, 520, 292],
             "group": False},
        ],
    }
    (OUT / "images.json").write_text(json.dumps(expected, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("images.pdf", (OUT / "images.pdf").stat().st_size, "bytes")


if __name__ == "__main__":
    main()
