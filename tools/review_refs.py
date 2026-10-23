"""Контакт-листы для просмотра глазами (эталон 9.3, 6.10): эталон рендера / текущий / разница.

    "%OTTISK_PYTHON%" -X utf8 -I tools/review_refs.py [--preset msvc-debug] [--sheet 16] [--changed] [--only <подстрока>]
    "%OTTISK_PYTHON%" -X utf8 -I tools/review_refs.py --shots [.queue/screenshots] [--sheet 16] [--prev .queue/shots-prev]

Эталоны — tests/render/expected/expected.json (поле renders[].png — путь от корня репозитория). Текущие
растры — build/<пресет>/render-actual/<имя png> и <имя>.diff.png (их пишет render_tests при
расхождении); если diff нет, он считается здесь (|эталон − текущий| × 8, красным — где > 3).
Без render-actual строка показывает только эталон («совпал»). --changed — только записи с текущим.
--shots [<каталог>] — контакт-листы снимков сцен (*.png, по умолчанию .queue/screenshots) вместо эталонов;
если есть --prev (по умолчанию .queue/shots-prev — снимки прошлого рубежа), у каждой сцены считается
pHash-расстояние (imagehash) к одноимённому прошлому снимку: > --phash-threshold (8) или «новая» —
в списке «смотреть поштучно» (сигнал, не блокер; код не меняется).
--main [<файл>] (с --shots) — ещё и список главных сцен (по умолчанию tools/main_scenes.txt) с путями к PNG:
их рубеж смотрит поштучно; сцены без снимка — «нет снимка» (код не меняется).
Листы — build/review/refs-NN.png или build/review/shots-NN.png. Код 0; 1 — нечего показывать.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
PRESET_DIRS = {
    "msvc-debug": "debug",
    "msvc-release": "release",
    "msvc-asan": "asan",
    "msvc-fuzz": "fuzz",
    "msvc-analyze": "analyze",
}
BG = (246, 246, 246)
INK = (32, 32, 32)
MUTED = (120, 120, 120)
BAD = (200, 30, 30)
PAD = 12
CAPTION_H = 38


def load_font(size: int) -> ImageFont.ImageFont:
    for name in ("segoeui.ttf", "arial.ttf"):
        try:
            return ImageFont.truetype(str(Path("C:/Windows/Fonts") / name), size)
        except OSError:
            continue
    return ImageFont.load_default()


def thumb(img: Image.Image | None, width: int, height: int, label: str, font) -> Image.Image:
    cell = Image.new("RGB", (width, height), (255, 255, 255))
    draw = ImageDraw.Draw(cell)
    if img is None:
        draw.rectangle([0, 0, width - 1, height - 1], outline=(210, 210, 210))
        draw.text((10, height // 2 - 8), label, fill=MUTED, font=font)
        return cell
    im = img.convert("RGB")
    im.thumbnail((width, height), Image.Resampling.LANCZOS)
    cell.paste(im, ((width - im.width) // 2, (height - im.height) // 2))
    draw.rectangle([0, 0, width - 1, height - 1], outline=(210, 210, 210))
    return cell


def compute_diff(expected: Image.Image, actual: Image.Image) -> tuple[Image.Image, str]:
    import numpy as np

    a = np.asarray(expected.convert("RGB"), dtype=np.int16)
    b = np.asarray(actual.convert("RGB"), dtype=np.int16)
    if a.shape != b.shape:
        return Image.new("RGB", (8, 8), BAD), f"размеры {a.shape[1]}×{a.shape[0]} ≠ {b.shape[1]}×{b.shape[0]}"
    delta = np.abs(a - b)
    per_px = delta.max(axis=2)
    gray = (0.3 * a[..., 0] + 0.59 * a[..., 1] + 0.11 * a[..., 2]).astype(np.uint8)
    base = np.stack([gray // 3 + 170] * 3, axis=2).astype(np.uint8)
    amp = np.clip(delta * 8, 0, 255).astype(np.uint8)
    out = np.where(per_px[..., None] > 0, amp, base)
    out[per_px > 3] = (220, 0, 0)
    differing = int((per_px > 0).sum())
    mse = float((delta.astype(np.float64) ** 2).mean() / 3.0) if delta.size else 0.0
    return Image.fromarray(out), f"max Δ {int(per_px.max())}, пикселей {differing}, MSE {mse:.3f}"


def open_image(path: Path) -> Image.Image | None:
    try:
        with Image.open(path) as im:
            im.load()
            return im.copy()
    except (OSError, ValueError):
        return None


def entries_from_expected(actual_dir: Path, only: str | None, changed: bool) -> list[dict]:
    expected_json = ROOT / "tests" / "render" / "expected" / "expected.json"
    if not expected_json.is_file():
        print(f"review_refs: нет {expected_json.relative_to(ROOT).as_posix()} — эталоны ещё не сняты")
        return []
    data = json.loads(expected_json.read_text(encoding="utf-8"))
    rows: list[dict] = []
    for item in data.get("items", []):
        if only and only not in item.get("id", ""):
            continue
        for r in item.get("renders", []):
            png_rel = Path(r.get("png", ""))
            exp_path = ROOT / png_rel if not png_rel.is_absolute() else png_rel
            if not exp_path.is_file():
                alt = ROOT / "tests" / "render" / "expected" / png_rel
                exp_path = alt if alt.is_file() else exp_path
            act = actual_dir / png_rel.name
            prefix = "tests/render/expected/"
            if not act.is_file() and png_rel.as_posix().startswith(prefix):
                act = actual_dir / png_rel.as_posix()[len(prefix) :]
            diff = act.with_name(act.name.removesuffix(".png") + ".diff.png")
            if changed and not act.is_file():
                continue
            rows.append(
                {
                    "title": f"{item.get('id', '?')} · стр. {r.get('page', '?')} · {r.get('dpi', '?')} dpi"
                    + (" · release-only" if item.get("release_only") else ""),
                    "expected": exp_path,
                    "actual": act if act.is_file() else None,
                    "diff": diff if diff.is_file() else None,
                }
            )
    return rows


def render_ref_sheets(rows: list[dict], per_sheet: int, cols: int, tw: int, th: int, out_dir: Path) -> list[Path]:
    font = load_font(13)
    small = load_font(11)
    triple_w = 3 * tw + 4 * PAD
    cell_h = CAPTION_H + th + PAD
    sheets: list[Path] = []
    out_dir.mkdir(parents=True, exist_ok=True)
    for old in out_dir.glob("refs-*.png"):
        old.unlink()
    for s in range(0, len(rows), per_sheet):
        chunk = rows[s : s + per_sheet]
        n_rows = (len(chunk) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * triple_w, 28 + n_rows * cell_h), BG)
        draw = ImageDraw.Draw(sheet)
        draw.text((PAD, 6), f"эталон | текущий | разница — лист {s // per_sheet + 1}", fill=INK, font=font)
        for k, row in enumerate(chunk):
            x0 = (k % cols) * triple_w
            y0 = 28 + (k // cols) * cell_h
            exp = open_image(row["expected"]) if row["expected"] and row["expected"].is_file() else None
            act = open_image(row["actual"]) if row["actual"] else None
            dif = open_image(row["diff"]) if row["diff"] else None
            note = "совпал (render-actual нет)" if act is None else ""
            if act is not None and exp is not None:
                computed, note = compute_diff(exp, act)
                if dif is None:
                    dif = computed
            draw.text((x0 + PAD, y0 + 2), row["title"], fill=INK, font=font)
            draw.text((x0 + PAD, y0 + 19), note, fill=BAD if act is not None else MUTED, font=small)
            images = [
                thumb(exp, tw, th, "эталона нет", small),
                thumb(act, tw, th, "совпал", small),
                thumb(dif, tw, th, "—", small),
            ]
            for j, im in enumerate(images):
                sheet.paste(im, (x0 + PAD + j * (tw + PAD), y0 + CAPTION_H))
        path = out_dir / f"refs-{s // per_sheet + 1:02d}.png"
        sheet.save(path, optimize=True)
        sheets.append(path)
    return sheets


def phash_distances(files: list[Path], prev_dir: Path | None) -> dict[Path, int | None]:
    """Сцена → pHash-расстояние к прошлому снимку (None — прошлого нет). Пусто — сравнивать не с чем."""
    if prev_dir is None or not prev_dir.is_dir():
        return {}
    try:
        import imagehash
    except ImportError:
        print("review_refs: нет imagehash — pHash не считается (запускайте через OTTISK_PYTHON)")
        return {}
    result: dict[Path, int | None] = {}
    for f in files:
        old = prev_dir / f.name
        cur_img, old_img = open_image(f), open_image(old) if old.is_file() else None
        if cur_img is None or old_img is None:
            result[f] = None
            continue
        result[f] = int(imagehash.phash(cur_img) - imagehash.phash(old_img))
    return result


def render_shot_sheets(files: list[Path], per_sheet: int, cols: int, tw: int, out_dir: Path,
                       distances: dict[Path, int | None] | None = None, threshold: int = 8) -> list[Path]:
    font = load_font(13)
    distances = distances or {}
    th = int(tw * 900 / 1440)
    cell_w = tw + 2 * PAD
    cell_h = th + 24 + PAD
    sheets: list[Path] = []
    out_dir.mkdir(parents=True, exist_ok=True)
    for old in out_dir.glob("shots-*.png"):
        old.unlink()
    for s in range(0, len(files), per_sheet):
        chunk = files[s : s + per_sheet]
        n_rows = (len(chunk) + cols - 1) // cols
        sheet = Image.new("RGB", (cols * cell_w, 28 + n_rows * cell_h), BG)
        draw = ImageDraw.Draw(sheet)
        draw.text((PAD, 6), f"снимки сцен — лист {s // per_sheet + 1}", fill=INK, font=font)
        for k, f in enumerate(chunk):
            x0 = (k % cols) * cell_w + PAD
            y0 = 28 + (k // cols) * cell_h
            img = open_image(f)
            size = f"{img.width}×{img.height}" if img else "не читается"
            caption, color = f"{f.stem} · {size}", INK
            if f in distances:
                d = distances[f]
                caption += " · новая" if d is None else f" · pHash Δ {d}"
                color = BAD if d is None or d > threshold else INK
            draw.text((x0, y0 + 2), caption, fill=color, font=font)
            sheet.paste(thumb(img, tw, th, "не читается", font), (x0, y0 + 22))
        path = out_dir / f"shots-{s // per_sheet + 1:02d}.png"
        sheet.save(path, optimize=True)
        sheets.append(path)
    return sheets


def print_main_scenes(list_path: Path, shots_dir: Path) -> None:
    """Главные сцены рубежа: путь к PNG или «нет снимка»."""
    if not list_path.is_file():
        print(f"главные сцены: нет {list_path}")
        return
    names = [line.split("#", 1)[0].strip() for line in list_path.read_text(encoding="utf-8-sig").splitlines()]
    names = [n.removesuffix(".png") for n in names if n]
    missing = [n for n in names if not (shots_dir / f"{n}.png").is_file()]
    print(f"главные сцены ({len(names)}, без снимка {len(missing)}) — смотреть поштучно:")
    for n in names:
        png = shots_dir / f"{n}.png"
        shown = png.relative_to(ROOT).as_posix() if png.is_relative_to(ROOT) else str(png)
        print(f"  {shown}" if png.is_file() else f"  {n}: нет снимка")


def main(argv: list[str]) -> int:
    global ROOT
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser(description="Контакт-листы эталонов рендера и снимков сцен")
    ap.add_argument("--preset", default="msvc-debug", choices=sorted(PRESET_DIRS))
    ap.add_argument("--actual", type=Path, help="каталог текущих растров (по умолчанию build/<пресет>/render-actual)")
    ap.add_argument("--sheet", type=int, default=16, help="записей на лист (по умолчанию 16)")
    ap.add_argument("--cols", type=int, default=0, help="столбцов на листе (по умолчанию 2 для эталонов, 4 для снимков)")
    ap.add_argument("--thumb", type=int, default=0, help="ширина миниатюры, px")
    ap.add_argument("--only", help="только записи, в id которых есть эта подстрока")
    ap.add_argument("--changed", action="store_true", help="только записи с текущим растром (расхождения)")
    ap.add_argument("--shots", type=Path, nargs="?", const=Path(".queue/screenshots"),
                    help="каталог снимков сцен (*.png); без значения — .queue/screenshots")
    ap.add_argument("--prev", type=Path, default=Path(".queue/shots-prev"),
                    help="снимки прошлого рубежа для pHash (по умолчанию .queue/shots-prev)")
    ap.add_argument("--phash-threshold", type=int, default=8, help="pHash-расстояние «смотреть поштучно» (8)")
    ap.add_argument("--main", type=Path, nargs="?", const=Path("tools/main_scenes.txt"),
                    help="список главных сцен (с --shots; без значения — tools/main_scenes.txt)")
    ap.add_argument("--out", type=Path, default=Path("build") / "review")
    ap.add_argument("--root", type=Path, default=ROOT, help="корень репозитория")
    args = ap.parse_args(argv)
    ROOT = args.root.resolve()
    out_dir = args.out if args.out.is_absolute() else ROOT / args.out

    if args.shots:
        shots_dir = args.shots if args.shots.is_absolute() else ROOT / args.shots
        files = sorted(p for p in shots_dir.glob("*.png") if p.is_file()) if shots_dir.is_dir() else []
        if not files:
            print(f"review_refs: в {shots_dir} нет *.png")
            return 1
        prev_dir = args.prev if args.prev.is_absolute() else ROOT / args.prev
        distances = phash_distances(files, prev_dir)
        sheets = render_shot_sheets(files, args.sheet, args.cols or 4, args.thumb or 360, out_dir,
                                    distances, args.phash_threshold)
        if distances:
            changed = [f.stem for f, d in distances.items() if d is None or d > args.phash_threshold]
            print(f"pHash к {prev_dir.name}: изменилось (> {args.phash_threshold}) или новые — {len(changed)}"
                  + (f": {', '.join(changed)}" if changed else ""))
        print(f"снимков {len(files)}, листов {len(sheets)}:")
    else:
        actual_dir = args.actual or ROOT / "build" / PRESET_DIRS[args.preset] / "render-actual"
        rows = entries_from_expected(actual_dir, args.only, args.changed)
        if not rows:
            print("review_refs: показывать нечего" + (" (расхождений нет)" if args.changed else ""))
            return 1
        tw = args.thumb or 220
        sheets = render_ref_sheets(rows, args.sheet, args.cols or 2, tw, int(tw * 1.3), out_dir)
        changed = sum(1 for r in rows if r["actual"])
        print(f"записей {len(rows)}, с расхождением {changed}, листов {len(sheets)}:")
    for p in sheets:
        print(f"  {p.relative_to(ROOT).as_posix() if p.is_relative_to(ROOT) else p}")
    if args.shots and args.main:
        print_main_scenes(args.main if args.main.is_absolute() else ROOT / args.main, shots_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
