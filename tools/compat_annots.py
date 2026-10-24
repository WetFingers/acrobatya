"""compat.annots — комментарии «АкроБати» в pdfium (план 5.1, п. 5; эталон 3.4, 9.4).

Каталог от annots_compat.DISABLED_write (unit_tests): matrix.pdf и created.pdf (записаны инкрементом), наш рендер
*.ours.png и regions.json — рамки аннотаций с внешним видом в пикселях. Скрипт рендерит те же страницы pypdfium2
(с аннотациями и полями) и сверяет imgcmp --cross в рамке каждой: оба растра непустые (≥ 50 пикселей «не фон»),
SSIM по серому ≥ 0,8, IoU масок «не фон» ≥ 0,7 — у pdfium другое сглаживание и толщина линий, поэтому не пороги
fuzzy. Нет pypdfium2 или каталога — 77 (пропуск).

Запуск: "%OTTISK_PYTHON%" -X utf8 -I tools/compat_annots.py --imgcmp build/debug/bin/imgcmp.exe build/debug/compat/annots
"""
from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

SKIP = 77


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("dir", type=Path)
    ap.add_argument("--imgcmp", required=True, type=Path)
    ap.add_argument("--limits", default="50,0.8,0.7", help="ink,ssim,iou для imgcmp --cross-limits")
    args = ap.parse_args(argv)
    manifest_path = args.dir / "regions.json"
    if not manifest_path.is_file():
        print(f"compat.annots: нет {manifest_path} — пропуск")
        return SKIP
    try:
        import pypdfium2 as pdfium
    except ImportError:
        print("compat.annots: нет pypdfium2 — пропуск")
        return SKIP
    if not args.imgcmp.is_file():
        print(f"compat.annots: нет {args.imgcmp}")
        return 1
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    failures: list[str] = []
    checked = 0
    for item in manifest["files"]:
        pdf = args.dir / item["pdf"]
        doc = pdfium.PdfDocument(str(pdf))
        try:
            doc.init_forms()  # поля формы рисует pdfium только после init_forms
        except Exception:  # noqa: BLE001 — без форм тоже сверяем
            pass
        page = doc[item["page"]]
        bitmap = page.render(scale=item["dpi"] / 72.0, may_draw_forms=True, draw_annots=True)
        theirs = args.dir / (pdf.stem + ".pdfium.png")
        bitmap.to_pil().convert("RGB").save(theirs)
        regions = item["regions"]
        for skip in item.get("skipped", []):
            print(f"{pdf.name}: {skip['subtype']} {skip.get('nm', '')} — пропуск ({skip['reason']})")
        if not regions:
            failures.append(f"{pdf.name}: нет рамок для сверки")
            continue
        cmd = [str(args.imgcmp), str(args.dir / item["png"]), str(theirs), "--cross", "--cross-limits", args.limits]
        for r in regions:
            cmd += ["--region", ",".join(str(v) for v in r["box"])]
        proc = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8", errors="replace", check=False)
        lines = [line for line in proc.stdout.splitlines() if line.startswith("{")]
        if not lines:
            failures.append(f"{pdf.name}: imgcmp без ответа (код {proc.returncode}): {proc.stderr[-300:]}")
            continue
        result = json.loads(lines[-1])
        for region, res in zip(regions, result["regions"]):
            checked += 1
            if not res.get("ok"):
                failures.append(f"{pdf.name}: {region['subtype']} {region.get('nm', '')} {json.dumps(res, ensure_ascii=False)}")
    for f in failures:
        print(f)
    print(f"compat.annots: рамок {checked}, расхождений {len(failures)}")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
