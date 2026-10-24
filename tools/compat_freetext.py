"""compat.freetext — текстовые поля «АкроБати» с кириллицей в чужих читалках (план 5.2, п. 1; эталон 3.4, 9.4).

Каталог от freetext_compat.DISABLED_write (unit_tests): boxes.pdf, callout.pdf, rotated.pdf (записаны инкрементом) и
expected.json — ожидаемый текст полей. Для каждого FreeText:
  * pikepdf — шрифт из /DA лежит в /AcroForm /DR /Font: /Type0, /Encoding /Identity-H, у потомка /FontDescriptor
    /FontFile2, есть /ToUnicode, и в нём — коды UTF-16 всех букв кириллицы текста поля;
  * pypdfium2 — страница с аннотациями даёт в /Rect поля ≥ 50 пикселей «не фон» (подмену шрифта pdfium растр не
    покажет — её ловит проверка структуры выше); после сплющивания (FPDFPage_Flatten) текст страницы содержит
    ожидаемые строки (без пробелов и переводов строк);
затем tools/compat_check.py --quiet по каталогу (qpdf --check, pdfcpu validate -m strict, mutool clean, pypdfium2).
Нет pikepdf/pypdfium2 или каталога — 77 (пропуск).

Запуск: "%OTTISK_PYTHON%" -X utf8 -I tools/compat_freetext.py build/debug/compat/freetext
"""
from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from pathlib import Path

SKIP = 77
DA_FONT = re.compile(r"/([^\s/]+)\s+[\d.]+\s+Tf")


def squeeze(text: str) -> str:
    return re.sub(r"\s+", "", text)


def check_structure(pdf_path: Path, pikepdf, failures: list[str]) -> int:
    checked = 0
    with pikepdf.open(pdf_path) as pdf:
        dr_fonts = pdf.Root.get("/AcroForm", {}).get("/DR", {}).get("/Font", {})
        for page_no, page in enumerate(pdf.pages):
            for annot in page.get("/Annots", []):
                if annot.get("/Subtype") != "/FreeText":
                    continue
                checked += 1
                where = f"{pdf_path.name} стр. {page_no + 1} {annot.get('/NM', '')}"
                da = str(annot.get("/DA", ""))
                m = DA_FONT.search(da)
                if not m:
                    failures.append(f"{where}: нет шрифта в /DA «{da}»")
                    continue
                name = "/" + m.group(1)
                font = dr_fonts.get(name)
                if font is None:
                    failures.append(f"{where}: шрифта {name} нет в /AcroForm /DR /Font")
                    continue
                if font.get("/Subtype") != "/Type0" or font.get("/Encoding") != "/Identity-H":
                    failures.append(f"{where}: {name} — не Type0 Identity-H")
                descendant = font.get("/DescendantFonts", [None])[0]
                descriptor = descendant.get("/FontDescriptor") if descendant is not None else None
                if descriptor is None or "/FontFile2" not in descriptor:
                    failures.append(f"{where}: у {name} нет /FontFile2 (не встроенный TrueType)")
                to_unicode = font.get("/ToUnicode")
                if to_unicode is None:
                    failures.append(f"{where}: у {name} нет /ToUnicode")
                    continue
                cmap = to_unicode.read_bytes().decode("latin-1").upper()
                contents = str(annot.get("/Contents", ""))
                for ch in sorted({c for c in contents if "Ѐ" <= c <= "ӿ"}):
                    if f"<{ord(ch):04X}>" not in cmap:
                        failures.append(f"{where}: /ToUnicode {name} не отображает «{ch}»")
                        break
                ap_fonts = annot.get("/AP", {}).get("/N", {}).get("/Resources", {}).get("/Font", {})
                if name not in ap_fonts:
                    failures.append(f"{where}: шрифта {name} нет в ресурсах вида /AP /N")
    return checked


def check_render(pdf_path: Path, expected: list[str], pdfium, pikepdf, failures: list[str]) -> None:
    with pikepdf.open(pdf_path) as pdf:
        rects = []
        for annot in pdf.pages[0].get("/Annots", []):
            if annot.get("/Subtype") == "/FreeText":
                rects.append([float(v) for v in annot["/Rect"]])
    doc = pdfium.PdfDocument(str(pdf_path))
    doc.init_forms()
    page = doc[0]
    scale = 2.0
    image = page.render(scale=scale, draw_annots=True, may_draw_forms=True).to_pil().convert("L")
    page_w, page_h = page.get_size()
    rotation = page.get_rotation()
    for rect in rects:
        x0, y0, x1, y1 = rect
        # Пункты PDF → пиксели показа (страница может быть повёрнута).
        corners = [(x0, y0), (x1, y0), (x0, y1), (x1, y1)]
        pts = []
        for x, y in corners:
            if rotation == 90:
                px, py = y, x
            elif rotation == 180:
                px, py = page_w - x, y
            elif rotation == 270:
                px, py = page_h - y, page_w - x
            else:
                px, py = x, page_h - y
            pts.append((px * scale, py * scale))
        bx0 = max(0, int(min(p[0] for p in pts)))
        by0 = max(0, int(min(p[1] for p in pts)))
        bx1 = min(image.width, int(max(p[0] for p in pts)) + 1)
        by1 = min(image.height, int(max(p[1] for p in pts)) + 1)
        region = image.crop((bx0, by0, bx1, by1))
        ink = sum(region.histogram()[:240])
        if ink < 50:
            failures.append(f"{pdf_path.name}: в рамке {rect} пусто ({ink} пикс.)")
    page.flatten()
    page.close()
    page = doc[0]
    text = squeeze(page.get_textpage().get_text_range())
    for want in expected:
        if squeeze(want) not in text:
            failures.append(f"{pdf_path.name}: после сплющивания нет текста «{want}» (есть: {text[:120]!r})")
    page.close()
    doc.close()


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("dir", type=Path)
    args = ap.parse_args(argv)
    expected_path = args.dir / "expected.json"
    if not expected_path.is_file():
        print(f"compat.freetext: нет {expected_path} — пропуск")
        return SKIP
    try:
        import pikepdf
        import pypdfium2 as pdfium
    except ImportError as e:
        print(f"compat.freetext: нет {e.name} — пропуск")
        return SKIP
    expected = json.loads(expected_path.read_text(encoding="utf-8"))
    failures: list[str] = []
    checked = 0
    for name, texts in expected.items():
        pdf_path = args.dir / name
        if not pdf_path.is_file():
            failures.append(f"нет {name}")
            continue
        checked += check_structure(pdf_path, pikepdf, failures)
        check_render(pdf_path, texts, pdfium, pikepdf, failures)
    script = Path(__file__).with_name("compat_check.py")
    proc = subprocess.run([sys.executable, "-X", "utf8", "-I", str(script), "--quiet", str(args.dir)],
                          capture_output=True, text=True, encoding="utf-8", errors="replace", check=False)
    if proc.returncode not in (0, SKIP):
        failures.append("compat_check.py:\n" + (proc.stdout + proc.stderr).strip())
    for f in failures:
        print(f"ОШИБКА {f}")
    print(f"compat.freetext: полей {checked}, файлов {len(expected)}, ошибок {len(failures)}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
