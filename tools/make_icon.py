"""Значок «АкроБати» — круглая печать: `"$env:OTTISK_PYTHON" -X utf8 -I tools/make_icon.py [--out installer/app.ico]`.

Знак — оттиск круглой печати тёплого тёмно-красного цвета: диск, по краю кольцо со словом
«АКРОБАТЯ • АКРОБАТЯ •», внутреннее кольцо и крупная буква «А» в центре. Мелкие размеры
упрощаются (уровни детализации): 16–32 — диск и «А», 48–64 — ещё кольцо, 128 и 256 — полный знак.
Каждый размер рисуется отдельно в масштабе ×8 и уменьшается LANCZOS — без размытия от готового 256.

Детерминированность: только Pillow, без случайностей и дат; шрифт — Georgia Bold из
C:\\Windows\\Fonts (запасной — Times New Roman Bold), его sha256 печатается. Файл .ico собирается
вручную: 16…128 — 32-битные BMP (DIB + маска AND, их понимает любой rc.exe и Проводник),
256 — PNG (так делает сама Windows). Значок лежит в репозитории, сборка его не перерисовывает.

`--preview <файл.png>` — дополнительно контактный лист всех размеров (для глаз).
"""

from __future__ import annotations

import argparse
import hashlib
import io
import math
import struct
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_OUT = ROOT / "installer" / "app.ico"

SIZES = (16, 24, 32, 48, 64, 128, 256)
SUPERSAMPLE = 8
CANVAS = 256.0  # все числа знака — в этих единицах

INK = (150, 36, 30, 255)  # тёплый тёмно-красный, «штемпельная краска»
INK_EDGE = (118, 24, 22, 255)  # край диска чуть темнее — оттиск по краю плотнее
PAPER = (250, 238, 222, 255)  # кремовый — «бумага» в просветах печати

RING_TEXT = "АКРОБАТЯ•АКРОБАТЯ•"
FONT_CANDIDATES = (
    Path("C:/Windows/Fonts/georgiab.ttf"),
    Path("C:/Windows/Fonts/timesbd.ttf"),
)


def font_path() -> Path:
    for path in FONT_CANDIDATES:
        if path.is_file():
            return path
    sys.exit("нет шрифта Georgia Bold / Times New Roman Bold в C:\\Windows\\Fonts")


def level_of_detail(size: int) -> str:
    if size <= 32:
        return "small"
    if size <= 64:
        return "medium"
    return "full"


def draw_ring(draw: ImageDraw.ImageDraw, k: float, radius: float, width: float, fill) -> None:
    c = CANVAS / 2 * k
    r = radius * k
    draw.ellipse((c - r, c - r, c + r, c + r), outline=fill, width=max(1, round(width * k)))


def draw_center_letter(image: Image.Image, k: float, font_file: Path, height: float) -> None:
    """Буква «А» по центру: высота глифа — `height` единиц знака."""
    font = ImageFont.truetype(str(font_file), size=max(8, round(height * 1.38 * k)))
    layer = Image.new("RGBA", image.size, (0, 0, 0, 0))
    draw = ImageDraw.Draw(layer)
    left, top, right, bottom = draw.textbbox((0, 0), "А", font=font)
    c = CANVAS / 2 * k
    draw.text((c - (left + right) / 2, c - (top + bottom) / 2), "А", font=font, fill=PAPER)
    image.alpha_composite(layer)


def draw_ring_text(image: Image.Image, k: float, font_file: Path) -> None:
    """Слово по кругу: верх букв наружу, читается по часовой стрелке от верхней точки."""
    font = ImageFont.truetype(str(font_file), size=round(25 * k))
    radius = 90.0 * k
    c = CANVAS / 2 * k
    widths = [font.getlength(ch) for ch in RING_TEXT]
    circumference = 2 * math.pi * radius
    gap = (circumference - sum(widths)) / len(RING_TEXT)  # ровная разрядка по всей окружности
    position = 0.0
    side = round(60 * k)
    for ch, width in zip(RING_TEXT, widths):
        middle = position + gap / 2 + width / 2
        position += width + gap
        angle = middle / circumference * 360.0  # по часовой от 12 часов
        glyph = Image.new("RGBA", (side, side), (0, 0, 0, 0))
        gdraw = ImageDraw.Draw(glyph)
        # якорь "mm" — середина глифа по ширине и середина между верхом и базовой линией
        gdraw.text((side / 2, side / 2), ch, font=font, fill=PAPER, anchor="mm")
        rotated = glyph.rotate(-angle, resample=Image.Resampling.BICUBIC, expand=True)
        rad = math.radians(angle)
        x = c + radius * math.sin(rad) - rotated.width / 2
        y = c - radius * math.cos(rad) - rotated.height / 2
        image.alpha_composite(rotated, (round(x), round(y)))


def render(size: int, font_file: Path) -> Image.Image:
    k = size * SUPERSAMPLE / CANVAS
    side = size * SUPERSAMPLE
    image = Image.new("RGBA", (side, side), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    c = CANVAS / 2 * k
    lod = level_of_detail(size)
    outer = 127.0 if lod == "small" else 126.0
    draw.ellipse((c - outer * k, c - outer * k, c + outer * k, c + outer * k), fill=INK_EDGE)
    inner = outer - (4.0 if lod == "small" else 6.0)
    draw.ellipse((c - inner * k, c - inner * k, c + inner * k, c + inner * k), fill=INK)
    if lod == "small":
        draw_center_letter(image, k, font_file, height=150.0)
    elif lod == "medium":
        draw_ring(draw, k, 104.0, 10.0, PAPER)
        draw_center_letter(image, k, font_file, height=118.0)
    else:
        draw_ring(draw, k, 113.0, 5.0, PAPER)
        draw_ring_text(image, k, font_file)
        draw_ring(draw, k, 68.0, 4.0, PAPER)
        draw_center_letter(image, k, font_file, height=74.0)
    return image.resize((size, size), Image.Resampling.LANCZOS)


def bmp_entry(image: Image.Image) -> bytes:
    """32-битный DIB для .ico: BITMAPINFOHEADER (высота ×2), строки снизу вверх BGRA, маска AND."""
    width, height = image.size
    header = struct.pack("<IiiHHIIiiII", 40, width, height * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    pixels = image.load()
    rows = bytearray()
    for y in range(height - 1, -1, -1):
        for x in range(width):
            r, g, b, a = pixels[x, y]
            rows += bytes((b, g, r, a))
    mask_stride = ((width + 31) // 32) * 4
    mask = bytearray()
    for y in range(height - 1, -1, -1):
        line = bytearray(mask_stride)
        for x in range(width):
            if pixels[x, y][3] == 0:
                line[x // 8] |= 0x80 >> (x % 8)
        mask += line
    return header + bytes(rows) + bytes(mask)


def png_entry(image: Image.Image) -> bytes:
    buffer = io.BytesIO()
    image.save(buffer, format="PNG", compress_level=9)
    return buffer.getvalue()


def ico_bytes(images: dict[int, Image.Image]) -> bytes:
    sizes = sorted(images)
    blobs = [png_entry(images[s]) if s >= 256 else bmp_entry(images[s]) for s in sizes]
    offset = 6 + 16 * len(sizes)
    out = bytearray(struct.pack("<HHH", 0, 1, len(sizes)))
    for size, blob in zip(sizes, blobs):
        dim = 0 if size >= 256 else size  # 0 в ICONDIRENTRY означает 256
        out += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(blob), offset)
        offset += len(blob)
    for blob in blobs:
        out += blob
    return bytes(out)


def contact_sheet(images: dict[int, Image.Image]) -> Image.Image:
    """Все размеры подряд на светлом и на тёмном фоне."""
    pad = 16
    width = sum(SIZES) + pad * (len(SIZES) + 1)
    height = max(SIZES) + 2 * pad
    sheet = Image.new("RGBA", (width, height * 2), (255, 255, 255, 255))
    sheet.paste((32, 32, 32, 255), (0, height, width, height * 2))
    for row in range(2):
        x = pad
        for s in SIZES:
            sheet.alpha_composite(images[s], (x, row * height + pad + (max(SIZES) - s) // 2))
            x += s + pad
    return sheet


def main() -> int:
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser(description="Значок «АкроБати» (installer/app.ico)")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT, help="куда записать .ico")
    parser.add_argument("--preview", type=Path, help="контактный лист PNG (светлый и тёмный фон)")
    options = parser.parse_args()

    font_file = font_path()
    font_hash = hashlib.sha256(font_file.read_bytes()).hexdigest()
    images = {size: render(size, font_file) for size in SIZES}
    data = ico_bytes(images)
    options.out.parent.mkdir(parents=True, exist_ok=True)
    options.out.write_bytes(data)
    print(f"{options.out} — {len(data)} байт, размеры {', '.join(map(str, SIZES))}")
    print(f"sha256 {hashlib.sha256(data).hexdigest()}")
    print(f"шрифт {font_file} sha256 {font_hash}")
    if options.preview:
        contact_sheet(images).save(options.preview)
        print(f"контактный лист: {options.preview}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
