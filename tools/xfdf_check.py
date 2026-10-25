"""Совместимость обмена комментариями XFDF/FDF (план 5.4, п. 4; ctest compat.xfdf).

    "%OTTISK_PYTHON%" -X utf8 -I tools/xfdf_check.py <каталог> [--quiet]

Каталог пишет unit_tests xfdf_compat.DISABLED_write: matrix.xfdf, matrix.fdf, imported.pdf, expected.json.
Проверки:
  * matrix.xfdf — XML, корень {http://ns.adobe.com/xfdf/}xfdf, <annots>, <f href>; у каждой аннотации —
    page (целое ≥ 0), rect (4 числа, x0 ≤ x1, y0 ≤ y1), name; цвета — «#RRGGBB»; разметка — coords кратно 8;
    ink — <inklist><gesture>; polygon/polyline — <vertices>; вложение — <data encoding="base64">; ответ —
    inreplyto на name, который есть в файле; <contents-richtext> — XHTML (<body> пространства XHTML);
  * matrix.fdf — заголовок «%FDF-1.2», в теле /FDF и /Annots;
  * imported.pdf (pikepdf) — каждая запись expected.json есть по /NM с тем же /Subtype, /Rect (±0,5 пт), /T и
    /Contents; ответ — /IRT на запись с /NM inreplyto;
  * compat_check.py по imported.pdf — qpdf --check, pdfcpu strict, mutool clean, pypdfium2.
Код 0 — зелёный, 1 — красный, 77 — нет pikepdf (пропуск ctest).
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

NS = "http://ns.adobe.com/xfdf/"
XHTML = "http://www.w3.org/1999/xhtml"
COLOR = re.compile(r"^#[0-9A-Fa-f]{6}$")


def numbers(text: str) -> list[float]:
    return [float(x) for x in re.split(r"[,; \t\r\n]+", text.strip()) if x]


def check_xfdf(path: Path, errors: list[str]) -> None:
    try:
        root = ET.parse(path).getroot()
    except ET.ParseError as e:
        errors.append(f"{path.name}: не XML: {e}")
        return
    if root.tag != f"{{{NS}}}xfdf":
        errors.append(f"{path.name}: корень {root.tag}, нужен xfdf в {NS}")
        return
    annots = root.find(f"{{{NS}}}annots")
    if annots is None or len(annots) == 0:
        errors.append(f"{path.name}: нет <annots>")
        return
    href = root.find(f"{{{NS}}}f")
    if href is None or not href.get("href"):
        errors.append(f"{path.name}: нет <f href>")
    names = {a.get("name") for a in annots}
    for a in annots:
        tag = a.tag.split("}")[-1]
        where = f"{path.name}: <{tag} name={a.get('name')}>"
        page = a.get("page")
        if page is None or not page.isdigit():
            errors.append(f"{where}: page «{page}»")
        rect = numbers(a.get("rect", ""))
        if len(rect) != 4 or rect[0] > rect[2] or rect[1] > rect[3]:
            errors.append(f"{where}: rect «{a.get('rect')}»")
        if not a.get("name"):
            errors.append(f"{where}: нет name")
        for attr in ("color", "interior-color"):
            if a.get(attr) is not None and not COLOR.match(a.get(attr)):
                errors.append(f"{where}: {attr} «{a.get(attr)}»")
        if tag in ("highlight", "underline", "squiggly", "strikeout"):
            coords = numbers(a.get("coords", ""))
            if not coords or len(coords) % 8 != 0:
                errors.append(f"{where}: coords «{a.get('coords')}»")
        if tag == "ink":
            gestures = a.findall(f"{{{NS}}}inklist/{{{NS}}}gesture")
            if not gestures or any(len(numbers(g.text or "")) < 4 for g in gestures):
                errors.append(f"{where}: нет <inklist><gesture>")
        if tag in ("polygon", "polyline"):
            vertices = a.find(f"{{{NS}}}vertices")
            if vertices is None or len(numbers(vertices.text or "")) < 4:
                errors.append(f"{where}: нет <vertices>")
        if tag == "fileattachment":
            data = a.find(f"{{{NS}}}data")
            if data is None or data.get("encoding") != "base64" or not (data.text or "").strip():
                errors.append(f"{where}: нет <data encoding=base64>")
        parent = a.get("inreplyto")
        if parent is not None and parent not in names:
            errors.append(f"{where}: inreplyto «{parent}» нет в файле")
        rich = a.find(f"{{{NS}}}contents-richtext")
        if rich is not None and rich.find(f"{{{XHTML}}}body") is None:
            errors.append(f"{where}: contents-richtext без <body> XHTML")


def check_fdf(path: Path, errors: list[str]) -> None:
    data = path.read_bytes()
    if not data.startswith(b"%FDF-1.2"):
        errors.append(f"{path.name}: заголовок {data[:8]!r}")
    if b"/FDF" not in data or b"/Annots" not in data:
        errors.append(f"{path.name}: нет /FDF /Annots")


def check_imported(pdf: Path, expected: list[dict], errors: list[str]) -> None:
    import pikepdf  # noqa: PLC0415 — проверено в main

    with pikepdf.open(pdf) as doc:
        by_name = {}
        for page in doc.pages:
            for a in page.get("/Annots", []):
                nm = a.get("/NM")
                if nm is not None:
                    by_name[str(nm)] = a
        for e in expected:
            name = e["name"]
            a = by_name.get(name)
            if a is None:
                errors.append(f"imported.pdf: нет /NM {name}")
                continue
            if str(a.get("/Subtype")) != "/" + e["subtype"]:
                errors.append(f"imported.pdf: {name} /Subtype {a.get('/Subtype')} ≠ {e['subtype']}")
            rect = [float(x) for x in a.get("/Rect", [])]
            if len(rect) != 4 or any(abs(x - y) > 0.5 for x, y in zip(rect, e["rect"])):
                errors.append(f"imported.pdf: {name} /Rect {rect} ≠ {e['rect']}")
            if str(a.get("/T", "")) != e["title"]:
                errors.append(f"imported.pdf: {name} /T «{a.get('/T')}» ≠ «{e['title']}»")
            if str(a.get("/Contents", "")) != e["contents"]:
                errors.append(f"imported.pdf: {name} /Contents ≠ «{e['contents']}»")
            if e.get("inreplyto"):
                irt = a.get("/IRT")
                if irt is None or str(irt.get("/NM")) != e["inreplyto"]:
                    errors.append(f"imported.pdf: {name} /IRT не на {e['inreplyto']}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dir", type=Path)
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()
    try:
        import pikepdf  # noqa: F401, PLC0415
    except ImportError:
        print("xfdf_check: нет pikepdf — пропуск")
        return 77
    errors: list[str] = []
    for name in ("matrix.xfdf", "matrix.fdf", "imported.pdf", "expected.json"):
        if not (args.dir / name).is_file():
            errors.append(f"нет {args.dir / name}")
    if errors:
        print("\n".join(errors))
        return 1
    expected = json.loads((args.dir / "expected.json").read_text(encoding="utf-8"))
    check_xfdf(args.dir / "matrix.xfdf", errors)
    check_fdf(args.dir / "matrix.fdf", errors)
    check_imported(args.dir / "imported.pdf", expected, errors)
    compat = Path(__file__).with_name("compat_check.py")
    tmp = args.dir / "check"
    tmp.mkdir(exist_ok=True)
    (tmp / "imported.pdf").write_bytes((args.dir / "imported.pdf").read_bytes())
    proc = subprocess.run([sys.executable, "-X", "utf8", "-I", str(compat), "--quiet", str(tmp)],
                          capture_output=True, text=True, encoding="utf-8", errors="replace", check=False)
    if proc.returncode not in (0, 77):
        errors.append("compat_check imported.pdf:\n" + proc.stdout + proc.stderr)
    if errors:
        print("xfdf: красный\n" + "\n".join(errors))
        return 1
    if not args.quiet:
        print(f"xfdf: зелёный — {len(expected)} записей, XML, FDF и imported.pdf")
    return 0


if __name__ == "__main__":
    sys.exit(main())
