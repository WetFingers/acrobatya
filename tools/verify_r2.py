"""Рубеж Р2 — автоматическая часть контрольной точки M2 «Редактор текста и подписи» (эталон 9.6; план «Рубеж Р2»).

    "%OTTISK_PYTHON%" -X utf8 -I tools/verify_r2.py [--release-exe build/release/bin/AcroBatya.exe]
        [--engine-tests build/debug/bin/engine_tests.exe] [--ui-tests build/debug/bin/ui_tests.exe]
        [--only edit,cyr,…] [--skip …] [--files 50] [--sign-files 10] [--jobs 4] [--out .queue/r2]

Проверки (каждая — ok / FAIL / SKIP; итог — <out>/verify_r2.json и таблица <out>/report.md):
  edit       release-exe --script на файлах тега text-edit (<корпус>/index.json, по порядку rel, пока не наберётся
             --files правленых): doc.open → doc.saveCopy before.pdf (full) → edit.insertText{page 1, block k, pos 0,
             «Akrobatya »} (k = 1…6 — первый редактируемый абзац страницы 1; подписанные и без редактируемого
             абзаца — пропуск с причиной) → saveCopy edited.pdf → doc.undo → saveCopy undone.pdf. У edited.pdf:
             qpdf --check = 0 (если before.pdf его проходит), pypdfium2 рендерит стр. 1, MuPDF stext (mutool
             draw -O clip=no -F text) содержит слово, рендер MuPDF (mutool draw, 144 dpi) вне абзаца — pixels == 0
             (абзац — рамка символов pypdfium2, у которых сменились текст или место, по выравниванию текста
             до/после, ± 3 pt; pdfium — для сведения: текст после абзаца в том же BT сдвигается на ~3·10⁻⁵ pt
             (float), и его кеш глифов по долям пикселя перебрасывает отдельные глифы); undone.pdf = before.pdf
             по объектам и байтам потоков (qpdf --json=2 --json-stream-data=inline, без /ID трейлера)
  cyr        кириллица «Проверка » в начало того же абзаца первых 10 правленых (латинских) файлов — путь файла
             кириллицей; qpdf --check = 0 (или предупреждение исходного), pypdfium2 извлекает «Проверка»
  stability  engine_tests edit_stability.* — «пустая правка» на всём теге text-edit: доля стабильных ≥ 90 %
             (число — в журнал рубежа)
  sign       первые --sign-files файлов, прошедших edit: release-exe --script — sign.sign B-T (служба времени —
             локальная tools/pki/tsa_serve.py) и sign.certify P=2 (B-T); B-LT и B-LTA — те же действия --script
             в ui_tests script_sign_r2 (служба PKI в процессе: OCSP/CRL тестовой PKI живут на
             http://pki.acrobatya.test/…, в сети их нет). Каждый файл: qpdf --check = 0 (подпись — инкремент:
             предупреждение исходного файла остаётся и не считается), pypdfium2 рендерит,
             pyHanko (compat_check.check_signatures, корень PKI): INTACT, TRUSTED, у T — TIMESTAMPED, у LT — LTV, у
             LTA — LTV и LTA (ades_lta_validation), метка документа INTACT; уровень по структуре (compare_pyhanko)
             совпадает (T, T, LT, LTA), у P=2 — DocMDP 2; openssl cms -verify -cades по /ByteRange — код 0
  dss        engine_tests sig.dss — вердикты корпуса EU DSS против tests/fixtures/sig/dss_expectations.json ≥ 90 %
  cng        engine_tests sign.matrix — CNG (PFXImportCertStore с ключом в памяти) = OpenSSL-путь по атрибутам
             (signed attributes байт в байт, у RSA — и подпись), pyHanko INTACT
  scope      tools/scope_report.py — «закрыто N из M»
Корпус рубеж не качает: нет тега на диске — SKIP и строка «Нужна ручная проверка».
Код: 0 — всё ok или SKIP; 1 — есть FAIL; 2 — неверные аргументы.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import difflib
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import compare_pyhanko  # noqa: E402
import compat_check  # noqa: E402
from verify_r1 import (CORPUS, ROOT, Result, app_env, need, qpdf_check, qpdf_exe, resolve_exe,  # noqa: E402
                       run)

CHECKS = ["edit", "cyr", "stability", "sign", "dss", "cng", "scope"]
WORD = "Akrobatya"
CYR = "Проверка"
MAX_BLOCK = 6
SCALE = 2.0  # рендер pypdfium2 — 144 dpi
MARGIN_PT = 3.0
PKI = ROOT / "tests" / "fixtures" / "pki"
PFX = PKI / "signer-rsa.pfx"
PFX_PASSWORD = "acrobatya"
# pdfium не потокобезопасен: рендер и текст pypdfium2 из потоков пула — только под замком.
PDFIUM = threading.Lock()
MUTOOL = Path(os.environ.get("OTTISK_MUTOOL", r"C:\src\mupdf\platform\win32\x64\Release\mutool.exe"))


def openssl_exe() -> str | None:
    for c in (os.environ.get("OTTISK_OPENSSL", ""), r"C:\Program Files\Git\mingw64\bin\openssl.exe",
              r"C:\Program Files\Git\usr\bin\openssl.exe"):
        if c and Path(c).is_file():
            return c
    return shutil.which("openssl")


class Ctx:
    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.out: Path = args.out
        self.release = resolve_exe(args.release_exe)
        self.engine = resolve_exe(args.engine_tests)
        self.ui = resolve_exe(args.ui_tests)
        self.edited: list[dict] = []  # файлы, прошедшие edit (для cyr и sign)

    def fresh(self, *parts: str) -> Path:
        d = self.out.joinpath(*parts)
        if d.exists():
            shutil.rmtree(d)
        d.mkdir(parents=True)
        return d


def script_run(exe: Path, work: Path, steps: list[dict], timeout: float = 120) -> tuple[bool, str]:
    script = work / "script.json"
    script.write_text(json.dumps({"steps": steps}, ensure_ascii=False, indent=1), encoding="utf-8")
    r = run([str(exe), "--script", str(script), "--out", str(work), "--data-dir", str(work / "data")],
            app_env(work / "data"), timeout=timeout)
    lines = (r.out + "\n" + r.err).splitlines()
    ok = r.code == 0 and any(ln.startswith("ok: ") for ln in lines)
    if ok:
        return True, ""
    # Причина — строка «step N cmd …» сценария (spdlog пишет в тот же поток), иначе — код процесса.
    step = [ln for ln in lines if ln.startswith("step ")]
    return False, (step[-1] if step else "") + f" (код {r.code}{', таймаут' if r.timed_out else ''})"


def text_edit_records() -> list[dict] | None:
    index = CORPUS / "index.json"
    if not index.is_file():
        return None
    recs = json.loads(index.read_text(encoding="utf-8")).get("tags", {}).get("text-edit", [])
    recs = [r for r in recs if Path(r["path"]).is_file()]
    return sorted(recs, key=lambda r: r["rel"]) or None


# ---------------------------------------------------------------- edit


def mutool_text(pdf: Path) -> str:
    # clip=no — и текст за MediaBox (у IRS первым абзацем в порядке чтения бывают служебные метки над страницей).
    r = subprocess.run([str(MUTOOL), "draw", "-q", "-O", "clip=no", "-F", "text", "-o", "-", str(pdf), "1"],
                       capture_output=True)
    return r.stdout.decode("utf-8", "replace")


def squash(s: str) -> str:
    return re.sub(r"\s+", "", s)


def page_chars(page) -> tuple[str, list[tuple[float, float, float, float]]]:
    """Символы стр. 1 pypdfium2 с рамками (пункты страницы); символы без рамки (сгенерированные) — пропуск."""
    tp = page.get_textpage()
    chars, boxes = [], []
    for i in range(tp.count_chars()):
        box = tp.get_charbox(i)
        if box[2] - box[0] <= 0 and box[3] - box[1] <= 0:
            continue
        ch = tp.get_text_range(i, 1)
        if not ch or ch.isspace():
            continue
        chars.append(ch[0])
        boxes.append(box)
    tp.close()
    return "".join(chars), boxes


def changed_region(before, after) -> tuple[tuple[float, float, float, float] | None, int]:
    """Рамка символов, у которых сменился текст или место (выравнивание до/после): (рамка, число символов)."""
    (ta, ba), (tb, bb) = before, after
    sm = difflib.SequenceMatcher(None, ta, tb, autojunk=False)
    changed: list[tuple[float, float, float, float]] = []
    for tag, i1, i2, j1, j2 in sm.get_opcodes():
        if tag == "equal":
            for k in range(i2 - i1):
                a, b = ba[i1 + k], bb[j1 + k]
                if max(abs(a[n] - b[n]) for n in range(4)) > 0.25:
                    changed += [a, b]
        else:
            changed += ba[i1:i2] + bb[j1:j2]
    if not changed:
        return None, 0
    return (min(b[0] for b in changed), min(b[1] for b in changed), max(b[2] for b in changed),
            max(b[3] for b in changed)), len(changed)


def render(page):
    import numpy as np

    return np.asarray(page.render(scale=SCALE).to_pil().convert("RGB")).astype(int)


def mupdf_render(pdf: Path, work: Path):
    """Стр. 1 нашим движком (mutool draw, CropBox, 144 dpi — та же сетка, что pypdfium2 при SCALE 2)."""
    import numpy as np
    from PIL import Image

    png = work / f"{pdf.stem}-mupdf.png"
    subprocess.run([str(MUTOOL), "draw", "-q", "-r", str(int(72 * SCALE)), "-o", str(png), str(pdf), "1"],
                   capture_output=True)
    return np.asarray(Image.open(png).convert("RGB")).astype(int)


def pixels_outside(before_pdf: Path, after_pdf: Path) -> tuple[int | None, int | None, str]:
    """Пиксели стр. 1, изменённые вне абзаца (рамка изменённых символов по pypdfium2 ± MARGIN_PT): (MuPDF,
    pdfium, заметка); None — не измерить. Критерий — MuPDF (движок программы, как engine.edit_render): текст
    после абзаца в том же BT … ET встаёт по восстановленной матрице текста с точностью float (сдвиг ~3·10⁻⁵ pt),
    а pdfium кеширует растры глифов по долям пикселя — такой сдвиг перебрасывает отдельные глифы (глазу не
    видно), поэтому pdfium — для сведения."""
    import numpy as np
    import pypdfium2 as pdfium

    da, db = pdfium.PdfDocument(str(before_pdf)), pdfium.PdfDocument(str(after_pdf))
    try:
        pa, pb = da[0], db[0]
        if pa.get_rotation() or pb.get_rotation():
            return None, None, "страница повёрнута"
        ia, ib = render(pa), render(pb)
        if ia.shape != ib.shape:
            return None, None, f"размер рендера {ia.shape} ≠ {ib.shape}"
        region, n = changed_region(page_chars(pa), page_chars(pb))
        if region is None:
            return None, None, "изменённых символов нет"
        # Действующая рамка (FPDF_GetPageBoundingBox): get_cropbox() не видит рамку, унаследованную от /Pages.
        left, _, _, top = pa.get_bbox()
    finally:
        da.close()
        db.close()
    x0 = max(0, int((region[0] - MARGIN_PT - left) * SCALE))
    x1 = max(0, int((region[2] + MARGIN_PT - left) * SCALE) + 1)
    y0 = max(0, int((top - region[3] - MARGIN_PT) * SCALE))
    y1 = max(0, int((top - region[1] + MARGIN_PT) * SCALE) + 1)

    def outside_of(a, b) -> tuple[int, int]:
        diff = (np.abs(a - b) > 0).any(axis=2)
        total = int(diff.sum())
        diff[y0:y1, x0:x1] = False
        return total, int(diff.sum())

    total, out_pdfium = outside_of(ia, ib)
    ma, mb = mupdf_render(before_pdf, before_pdf.parent), mupdf_render(after_pdf, after_pdf.parent)
    if ma.shape != mb.shape:
        return None, out_pdfium, f"размер рендера MuPDF {ma.shape} ≠ {mb.shape}"
    mtotal, out_mupdf = outside_of(ma, mb)
    h, w = ia.shape[:2]
    share = max(0, min(x1, w) - x0) * max(0, min(y1, h) - y0) / (w * h)
    return out_mupdf, out_pdfium, (f"абзац {region[2] - region[0]:.0f} × {region[3] - region[1]:.0f} pt ({n} симв., "
                                   f"{share * 100:.0f} % стр.); MuPDF: изменено {mtotal} px, вне абзаца {out_mupdf}; "
                                   f"pdfium: изменено {total}, вне абзаца {out_pdfium}")


def qpdf_objects(path: Path) -> dict:
    r = subprocess.run([qpdf_exe(), "--json=2", "--json-key=qpdf", "--json-stream-data=inline", str(path)],
                       capture_output=True)
    objects = json.loads(r.stdout.decode("utf-8"))["qpdf"][1]
    objects.get("trailer", {}).get("value", {}).pop("/ID", None)
    return objects


def edit_one(ctx: Ctx, n: int, rec: dict) -> dict:
    import pypdfium2 as pdfium

    work = ctx.fresh("edit", f"{n:03d}")
    src = work / "src.pdf"
    shutil.copyfile(rec["path"], src)
    res = {"rel": rec["rel"], "dir": str(work), "src": str(src), "edited": False, "problems": []}
    reason = ""
    for block in range(1, MAX_BLOCK + 1):
        steps = [{"cmd": "doc.open", "args": {"path": str(src)}},
                 {"cmd": "doc.saveCopy", "args": {"path": "${OUT_DIR}/before.pdf", "mode": "full"}},
                 {"cmd": "edit.insertText", "args": {"page": 1, "block": block, "pos": 0, "text": WORD + " "},
                  "name": f"слово в абзац {block}"},
                 {"cmd": "doc.saveCopy", "args": {"path": "${OUT_DIR}/edited.pdf", "mode": "full"}},
                 {"cmd": "doc.undo", "args": {"n": 1}},
                 {"cmd": "doc.saveCopy", "args": {"path": "${OUT_DIR}/undone.pdf", "mode": "full"}},
                 {"cmd": "doc.close"}]
        ok, reason = script_run(ctx.release, work, steps)
        if ok:
            res["block"] = block
            break
        if "read-only" not in reason:
            break  # вне диапазона, подписан, ошибка правки — дальше не искать
    if "block" not in res:
        res["skip"] = reason
        return res
    res["edited"] = True
    before, edited, undone = work / "before.pdf", work / "edited.pdf", work / "undone.pdf"
    qb, _ = qpdf_check(before)
    qe, qlast = qpdf_check(edited)
    res["qpdf"] = qe
    res["qpdf_before"] = qb
    if qe != 0 and qb == 0:
        res["problems"].append(f"qpdf --check edited.pdf {qe}: {qlast}")
    try:
        with PDFIUM:
            d = pdfium.PdfDocument(str(edited))
            d[0].render(scale=1).to_pil()
            d.close()
        res["pdfium"] = True
    except Exception as e:  # noqa: BLE001
        res["pdfium"] = False
        res["problems"].append(f"pypdfium2: {type(e).__name__}: {e}")
    res["word"] = WORD in squash(mutool_text(edited))
    if not res["word"]:
        res["problems"].append("слова нет в stext MuPDF")
    try:
        with PDFIUM:
            outside, outside_pdfium, note = pixels_outside(before, edited)
    except Exception as e:  # noqa: BLE001
        outside, outside_pdfium, note = None, None, f"{type(e).__name__}: {e}"
    res["outside"] = outside
    res["outside_pdfium"] = outside_pdfium
    res["pixels"] = note
    if outside:
        res["problems"].append(f"рендер вне абзаца: {note}")
    try:
        res["undo"] = qpdf_objects(before) == qpdf_objects(undone)
    except Exception as e:  # noqa: BLE001
        res["undo"] = False
        res["problems"].append(f"qpdf --json: {type(e).__name__}: {e}")
    if not res["undo"]:
        a, b = qpdf_objects(before), qpdf_objects(undone)
        diff = [k for k in sorted(set(a) | set(b)) if a.get(k) != b.get(k)]
        res["problems"].append(f"undo: объекты отличаются ({len(diff)}): {', '.join(diff[:6])}")
    return res


def check_edit(ctx: Ctx) -> Result:
    if (skip := need(ctx.release, "edit")) is not None:
        return skip
    if not qpdf_exe() or not MUTOOL.is_file():
        return Result("edit", "SKIP", "нет qpdf или mutool")
    recs = text_edit_records()
    if recs is None:
        return Result("edit", "SKIP", "нет тега text-edit", manual=["Р2 · корпус text-edit не скачан"])
    want = ctx.args.files
    results: list[dict] = []
    t0 = time.monotonic()
    pos = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=ctx.args.jobs) as pool:
        while sum(r["edited"] for r in results) < want and pos < len(recs):
            need_more = want - sum(r["edited"] for r in results)
            batch = recs[pos:pos + max(need_more, ctx.args.jobs)]
            futures = [pool.submit(edit_one, ctx, pos + i, rec) for i, rec in enumerate(batch)]
            pos += len(batch)
            results += [f.result() for f in futures]
    edited = [r for r in results if r["edited"]][:want]
    skipped = [r for r in results if not r["edited"]]
    ctx.edited = [r for r in edited if not r["problems"]]
    bad = [r for r in edited if r["problems"]]
    exact = sum(1 for r in edited if r.get("outside") == 0)
    exact_pdfium = sum(1 for r in edited if r.get("outside_pdfium") == 0)
    unmeasured = [r for r in edited if r.get("outside") is None]
    details = ["| # | Файл | Абзац | qpdf | pdfium | Слово | Вне абзаца MuPDF / pdfium, px | Undo | Заметка |",
               "|---:|---|---:|---:|---|---|---:|---|---|"]
    for i, r in enumerate(edited, 1):
        details.append(f"| {i} | {r['rel']} | {r['block']} | {r['qpdf']}"
                       f"{' (до: ' + str(r['qpdf_before']) + ')' if r['qpdf_before'] else ''} | "
                       f"{'да' if r['pdfium'] else 'НЕТ'} | {'да' if r['word'] else 'НЕТ'} | "
                       f"{r['outside'] if r['outside'] is not None else '—'} / "
                       f"{r['outside_pdfium'] if r['outside_pdfium'] is not None else '—'} | "
                       f"{'да' if r['undo'] else 'НЕТ'} | {r['pixels']} |")
    details += [f"проблема: {r['rel']}: {'; '.join(r['problems'])}" for r in bad]
    details += [f"пропуск: {r['rel']}: {r['skip']}" for r in skipped]
    ok = len(edited) >= want and not bad
    summary = (f"{len(edited)} файлов правлено ({len(skipped)} пропущено — подписаны или нет редактируемого абзаца "
               f"в первых {MAX_BLOCK}), за {time.monotonic() - t0:.0f} с: qpdf --check 0 — "
               f"{sum(1 for r in edited if r['qpdf'] == 0)}, pypdfium2 — {sum(1 for r in edited if r['pdfium'])}, "
               f"слово в stext — {sum(1 for r in edited if r['word'])}, вне абзаца 0 px (MuPDF) — {exact}"
               f"{f' (не измерить {len(unmeasured)})' if unmeasured else ''} (pdfium — {exact_pdfium}), "
               f"undo = до правки — "
               f"{sum(1 for r in edited if r['undo'])}; с проблемами {len(bad)}")
    return Result("edit", "ok" if ok else "FAIL", summary, details,
                  {"edited": len(edited), "skipped": len(skipped), "exact": exact, "exact_pdfium": exact_pdfium,
                   "bad": len(bad)},
                  manual=["Р2 · отредактировать абзац в своём документе и открыть в Acrobat Reader (M2)"])


# ---------------------------------------------------------------- cyr


def check_cyr(ctx: Ctx) -> Result:
    if (skip := need(ctx.release, "cyr")) is not None:
        return skip
    if not ctx.edited:
        return Result("cyr", "SKIP", "нет правленых файлов (проверка edit не шла или упала)")
    import pypdfium2 as pdfium

    details, good = [], 0
    files = ctx.edited[:10]
    for i, r in enumerate(files):
        work = ctx.fresh("cyr", f"{i:02d}")
        src = work / f"латиница-{i:02d}.pdf"
        shutil.copyfile(r["src"], src)
        steps = [{"cmd": "doc.open", "args": {"path": str(src)}},
                 {"cmd": "edit.insertText", "args": {"page": 1, "block": r["block"], "pos": 0, "text": CYR + " "},
                  "name": "кириллица в латинский абзац"},
                 {"cmd": "doc.saveCopy", "args": {"path": "${OUT_DIR}/кириллица.pdf", "mode": "full"}},
                 {"cmd": "doc.close"}]
        ok, reason = script_run(ctx.release, work, steps)
        out = work / "кириллица.pdf"
        if not ok or not out.is_file():
            details.append(f"{r['rel']}: --script: {reason}")
            continue
        q, last = qpdf_check(out)
        d = pdfium.PdfDocument(str(out))
        tp = d[0].get_textpage()
        text = tp.get_text_range()
        tp.close()
        d.close()
        found = CYR in squash(text)
        inherited = q != 0 and r.get("qpdf_before", 0) != 0  # предупреждение исходного файла (edit: before.pdf)
        good += (q == 0 or inherited) and found
        details.append(f"{r['rel']}: qpdf {q}{'' if q == 0 else ' (' + last + ')'}"
                       f"{' — как у исходного' if inherited else ''}, pypdfium2 «{CYR}» — {'да' if found else 'НЕТ'}")
    ok = good == len(files)
    return Result("cyr", "ok" if ok else "FAIL",
                  f"«{CYR}» в латинские файлы ({len(files)}, путь кириллицей): читается pypdfium2 и qpdf --check 0 — "
                  f"{good} из {len(files)}", details)


# ---------------------------------------------------------------- stability / dss / cng (engine_tests)


def engine(ctx: Ctx, name: str, gtest_filter: str, timeout: float) -> tuple[Result | None, object]:
    if (skip := need(ctx.engine, name)) is not None:
        return skip, None
    work = ctx.fresh(name)
    r = run([str(ctx.engine), f"--gtest_filter={gtest_filter}", "--gtest_brief=0"],
            app_env(work / "data"), timeout=timeout, memory_mb=8192)
    return None, r


def check_stability(ctx: Ctx) -> Result:
    skip, r = engine(ctx, "stability", "edit_stability.*", 1800)
    if skip:
        return skip
    m = re.search(r"edit_stability: .*?стабильных (\d+) \(([\d.]+) %\)", r.out)
    if "SKIPPED" in r.out and not m:
        return Result("stability", "SKIP", "нет тега text-edit", manual=["Р2 · корпус text-edit не скачан"])
    if not m:
        return Result("stability", "FAIL", f"engine_tests edit_stability: код {r.code}",
                      (r.out + r.err).strip().splitlines()[-10:])
    share = float(m.group(2))
    line = next(ln for ln in r.out.splitlines() if ln.startswith("edit_stability:"))
    unstable = [ln for ln in r.out.splitlines() if ln.startswith("unstable:")]
    ok = r.code == 0 and share >= 90.0
    return Result("stability", "ok" if ok else "FAIL", f"стабильность модели на теге text-edit — {share:.1f} % "
                  f"(≥ 90 %)", [line, *unstable], {"share": share})


def check_dss(ctx: Ctx) -> Result:
    skip, r = engine(ctx, "dss", "sig.dss", 1800)
    if skip:
        return skip
    m = re.search(r"dss: (\d+) of (\d+) files match the expectations \(([\d.]+) %\)", r.out)
    if not m:
        if "SKIPPED" in r.out:
            return Result("dss", "SKIP", "нет корпуса dss", manual=["Р2 · корпус EU DSS не скачан"])
        return Result("dss", "FAIL", f"engine_tests sig.dss: код {r.code}", (r.out + r.err).strip().splitlines()[-10:])
    lines = [ln.strip() for ln in r.out.splitlines() if ln.startswith("  ")][:40]
    ok = r.code == 0 and float(m.group(3)) >= 90.0
    return Result("dss", "ok" if ok else "FAIL", f"корпус EU DSS: {m.group(1)} из {m.group(2)} файлов — вердикты "
                  f"по dss_expectations.json ({m.group(3)} %, ≥ 90 %)", lines, {"share": float(m.group(3))})


def check_cng(ctx: Ctx) -> Result:
    skip, r = engine(ctx, "cng", "sign.matrix", 900)
    if skip:
        return skip
    ok = r.code == 0 and "[  PASSED  ] 1 test" in r.out and "sign-matrix: 12 files" in r.out
    tail = [ln for ln in r.out.splitlines() if ln.startswith("sign-matrix") or "Failure" in ln or "error" in ln]
    return Result("cng", "ok" if ok else "FAIL",
                  "RSA/ECDSA × .pfx/CNG (PFXImportCertStore, ключ в памяти) × B-B/B-T: подписанные атрибуты CNG = "
                  "OpenSSL-путь, у RSA — и подпись; pyHanko INTACT, TIMESTAMPED — " + ("да" if ok else "НЕТ"),
                  tail[-10:] + ([] if ok else (r.out + r.err).strip().splitlines()[-15:]),
                  manual=["Р2 · CNG с неэкспортируемым ключом и PIN в окне Windows — на экране (M2)"])


# ---------------------------------------------------------------- sign


class TsaServer:
    def __init__(self, log: Path):
        self.log = log.open("w", encoding="utf-8")
        self.proc = subprocess.Popen([sys.executable, "-X", "utf8", "-I", str(ROOT / "tools" / "pki" / "tsa_serve.py"),
                                      "--lifetime", "3600"], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                     stderr=self.log, creationflags=subprocess.CREATE_NO_WINDOW)
        line = self.proc.stdout.readline().decode().strip()
        self.port = int(line) if line.isdigit() else None
        self.url = f"http://127.0.0.1:{self.port}/tsr" if self.port else None

    def close(self) -> None:
        try:
            self.proc.stdin.close()
            self.proc.wait(timeout=10)
        except Exception:  # noqa: BLE001
            self.proc.kill()
        self.log.close()


def cms_verify(pdf: Path, work: Path) -> list[str]:
    """openssl cms -verify -cades у каждой подписи (не метки документа) по её /ByteRange; [] — все зелёные."""
    from pyhanko.pdf_utils.reader import PdfFileReader

    exe = openssl_exe()
    if not exe:
        return ["нет openssl"]
    problems = []
    data = pdf.read_bytes()
    with pdf.open("rb") as fh:
        reader = PdfFileReader(fh, strict=False)
        for k, sig in enumerate(reader.embedded_signatures):
            if sig.sig_object_type == "/DocTimeStamp":
                continue
            br = [int(x) for x in sig.sig_object["/ByteRange"]]
            content = work / f"cms-{k}.bin"
            der = work / f"cms-{k}.der"
            content.write_bytes(data[br[0]:br[0] + br[1]] + data[br[2]:br[2] + br[3]])
            der.write_bytes(sig.pkcs7_content)
            r = subprocess.run([exe, "cms", "-verify", "-cades", "-binary", "-inform", "DER", "-in", str(der),
                                "-content", str(content), "-CAfile", str(PKI / "root.pem"), "-purpose", "smimesign",
                                "-out", str(work / f"cms-{k}.out")], capture_output=True, text=True,
                               encoding="utf-8", errors="replace")
            if r.returncode != 0:
                problems.append(f"{sig.field_name}: openssl cms -verify -cades {r.returncode}: "
                                f"{(r.stderr or r.stdout).strip().splitlines()[-1:]}")
    return problems


EXPECT = {  # вариант → (токены pyHanko, уровень по структуре, DocMDP)
    "bt": ("INTACT,TRUSTED,TIMESTAMPED,TIMESTAMP_TRUSTED", "T", None),
    "p2": ("INTACT,TRUSTED,TIMESTAMPED,TIMESTAMP_TRUSTED", "T", 2),
    "lt": ("INTACT,TRUSTED,TIMESTAMPED,LTV", "LT", None),
    "lta": ("INTACT,TRUSTED,TIMESTAMPED,LTV,LTA", "LTA", None),
}


def validate_signed(pdf: Path, variant: str) -> list[str]:
    tokens, level, docmdp = EXPECT[variant]
    problems = []
    q = compat_check.check_qpdf(Path(qpdf_exe()), pdf, None)
    # Подпись — инкремент: предупреждение qpdf исходного файла (src.pdf рядом) остаётся в его байтах.
    if not q["ok"] and qpdf_check(pdf.parent / "src.pdf")[0] == 0:
        problems.append(f"qpdf --check {q['exit']}: {q['warnings'][:2]}")
    p = compat_check.check_pdfium(pdf, None, False)
    if not p["ok"]:
        problems.append(f"pypdfium2: {p['warnings'][:2]}")
    s = compat_check.check_signatures(pdf, tokens, [PKI / "root.pem"])
    if not s["ok"]:
        problems.append(f"pyHanko: {'; '.join(s['warnings'])}")
    survey = compare_pyhanko.survey_file(pdf)
    sigs = [x for x in survey.get("signatures", []) if x.get("kind") == "sig"]
    if variant == "lta" and not any(x.get("kind") == "dts" and x.get("intact") for x in survey.get("signatures", [])):
        problems.append("метки времени документа (INTACT) нет")
    if not sigs:
        problems.append(f"pyHanko: подписи не найдены ({survey.get('error', '')})")
    else:
        got = sigs[0]
        if got.get("level") != level:
            problems.append(f"уровень {got.get('level')}, ждали {level}")
        if docmdp is not None and got.get("docmdp") != docmdp:
            problems.append(f"DocMDP {got.get('docmdp')}, ждали {docmdp}")
        if not got.get("intact"):
            problems.append("pyHanko: подпись не INTACT")
    work = pdf.parent / f"{pdf.stem}-cms"
    work.mkdir(exist_ok=True)
    problems += cms_verify(pdf, work)
    return problems


def check_sign(ctx: Ctx) -> Result:
    if (skip := need(ctx.release, "sign")) is not None:
        return skip
    if (skip := need(ctx.ui, "sign")) is not None:
        return skip
    files = ctx.edited[:ctx.args.sign_files]
    if len(files) < ctx.args.sign_files:
        recs = text_edit_records() or []
        files += [{"rel": r["rel"], "src": r["path"]} for r in recs[:ctx.args.sign_files - len(files)]]
    work = ctx.fresh("sign")
    tsa = TsaServer(work / "tsa.log")
    if not tsa.url:
        tsa.close()
        return Result("sign", "SKIP", "tsa_serve.py не запустилась (нет openssl?)")
    outputs: dict[tuple[int, str], Path] = {}
    details: list[str] = []
    t0 = time.monotonic()
    try:
        def exe_one(i: int, f: dict) -> list[str]:
            d = work / f"{i:02d}"
            d.mkdir()
            src = d / "src.pdf"
            shutil.copyfile(f["src"], src)
            steps = [{"cmd": "sign.trust", "args": {"cert": str(PKI / "root.cer"), "certified": True}},
                     {"cmd": "doc.open", "args": {"path": str(src)}},
                     {"cmd": "sign.sign", "args": {"pfx": str(PFX), "password": PFX_PASSWORD, "level": "T",
                                                   "tsa": tsa.url, "reason": "Рубеж Р2", "page": 1,
                                                   "rect": [36, 36, 236, 96], "target": "${OUT_DIR}/bt.pdf"},
                      "name": "B-T"},
                     {"cmd": "doc.close"},
                     {"cmd": "doc.open", "args": {"path": str(src)}},
                     {"cmd": "sign.certify", "args": {"pfx": str(PFX), "password": PFX_PASSWORD, "perms": 2,
                                                      "level": "T", "tsa": tsa.url, "target": "${OUT_DIR}/p2.pdf"},
                      "name": "P=2"},
                     {"cmd": "doc.close"}]
            ok, reason = script_run(ctx.release, d, steps)
            return [] if ok else [f"{f['rel']}: AcroBatya.exe --script: {reason}"]

        with concurrent.futures.ThreadPoolExecutor(max_workers=ctx.args.jobs) as pool:
            for errs in pool.map(lambda a: exe_one(*a), enumerate(files)):
                details += errs
        # B-LT и B-LTA — те же действия в ui_tests (служба PKI в процессе).
        steps = [{"cmd": "sign.trust", "args": {"cert": str(PKI / "root.cer"), "certified": True}}]
        for i, f in enumerate(files):
            src = work / f"{i:02d}" / "src.pdf"
            for level in ("LT", "LTA"):
                steps += [{"cmd": "doc.open", "args": {"path": str(src)}},
                          {"cmd": "sign.sign", "args": {"pfx": str(PFX), "password": PFX_PASSWORD, "level": level,
                                                        "reason": "Рубеж Р2", "page": 1, "rect": [36, 36, 236, 96],
                                                        "target": f"${{OUT_DIR}}/{i:02d}/{level.lower()}.pdf"},
                           "name": f"{f['rel']}: B-{level}"},
                          {"cmd": "doc.close"}]
        script = work / "lt-lta.json"
        script.write_text(json.dumps({"steps": steps}, ensure_ascii=False, indent=1), encoding="utf-8")
        env = app_env(work / "ui-data", OTTISK_R2_SIGN_SCRIPT=str(script), OTTISK_R2_OUT=str(work))
        r = run([str(ctx.ui), "script_sign_r2"], env, timeout=1800)
        if r.code != 0 or "r2 sign script: ok" not in r.out:
            details.append(f"ui_tests script_sign_r2: код {r.code}: "
                           + " | ".join((r.out + r.err).strip().splitlines()[-6:]))
    finally:
        tsa.close()
    sign_s = time.monotonic() - t0
    table = ["| # | Файл | B-T | P=2 | B-LT | B-LTA |", "|---:|---|---|---|---|---|"]
    passed = {v: 0 for v in EXPECT}
    for i, f in enumerate(files):
        cells = []
        for variant in EXPECT:
            pdf = work / f"{i:02d}" / f"{variant}.pdf"
            if not pdf.is_file():
                cells.append("нет файла")
                continue
            outputs[(i, variant)] = pdf
            problems = validate_signed(pdf, variant)
            if problems:
                details.append(f"{f['rel']} {variant}: " + "; ".join(problems))
                cells.append("FAIL")
            else:
                passed[variant] += 1
                cells.append("ok")
        table.append(f"| {i + 1} | {f['rel']} | " + " | ".join(cells) + " |")
    n = len(files)
    ok = n > 0 and all(v == n for v in passed.values())
    summary = (f"{n} файлов за {sign_s:.0f} с: B-T {passed['bt']}, P=2 {passed['p2']}, B-LT {passed['lt']}, "
               f"B-LTA {passed['lta']} — qpdf 0, pypdfium2, pyHanko INTACT/TRUSTED (+ TIMESTAMPED, LTV, LTA по "
               f"уровню), уровень по структуре совпал, openssl cms -verify -cades 0")
    return Result("sign", "ok" if ok else "FAIL", summary, table + details, {"passed": passed, "files": n},
                  manual=["Р2 · подпись с меткой DigiCert в Acrobat Reader и Edge — зелёная галочка и «LTV включено» "
                          "после «Добавить сведения для проверки» (M2)"])


# ---------------------------------------------------------------- scope


def check_scope(ctx: Ctx) -> Result:
    r = subprocess.run([sys.executable, "-X", "utf8", "-I", str(ROOT / "tools" / "scope_report.py")],
                       capture_output=True, text=True, encoding="utf-8", errors="replace", cwd=ROOT)
    lines = [ln for ln in r.stdout.splitlines() if ln.strip()]
    closed = [ln for ln in lines if "закрыто" in ln.lower()]
    return Result("scope", "ok" if r.returncode == 0 else "FAIL",
                  "; ".join(closed[:3]) or f"scope_report.py: код {r.returncode}", lines[:40])


# ---------------------------------------------------------------- main

RUNNERS = {
    "edit": check_edit,
    "cyr": check_cyr,
    "stability": check_stability,
    "sign": check_sign,
    "dss": check_dss,
    "cng": check_cng,
    "scope": check_scope,
}


def write_report(out: Path, results: list[Result], started: str) -> None:
    lines = [f"# Рубеж Р2 · tools/verify_r2.py · {started}", "", "| Проверка | Итог | Что |", "|---|---|---|"]
    lines += [f"| {r.check} | {r.status} | {r.summary} |" for r in results]
    for r in results:
        if r.details:
            lines += ["", f"## {r.check}", "", *r.details]
    manual = [m for r in results for m in r.manual]
    if manual:
        lines += ["", "## Нужна ручная проверка", "", *(f"- {m}" for m in manual)]
    (out / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    (out / "verify_r2.json").write_text(json.dumps(
        {"started": started, "results": [r.__dict__ for r in results]}, ensure_ascii=False, indent=1, default=str),
        encoding="utf-8")


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--release-exe", type=Path, default=Path("build/release/bin/AcroBatya.exe"))
    ap.add_argument("--engine-tests", type=Path, default=Path("build/debug/bin/engine_tests.exe"))
    ap.add_argument("--ui-tests", type=Path, default=Path("build/debug/bin/ui_tests.exe"),
                    help="ui_tests с помощником script_sign_r2 (B-LT/B-LTA со службой PKI в процессе)")
    ap.add_argument("--only", help="проверки через запятую: " + ", ".join(CHECKS))
    ap.add_argument("--skip", help="пропустить проверки через запятую")
    ap.add_argument("--files", type=int, default=50, help="сколько файлов text-edit править")
    ap.add_argument("--sign-files", type=int, default=10, help="сколько файлов подписывать")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--out", type=Path, default=Path(".queue/r2"))
    args = ap.parse_args(argv)
    args.out = (args.out if args.out.is_absolute() else ROOT / args.out).resolve()
    selected = [c for c in (args.only.split(",") if args.only else CHECKS) if c]
    if args.skip:
        selected = [c for c in selected if c not in args.skip.split(",")]
    bad = [c for c in selected if c not in RUNNERS]
    if bad:
        print(f"неизвестные проверки: {', '.join(bad)}", file=sys.stderr)
        return 2
    args.out.mkdir(parents=True, exist_ok=True)
    ctx = Ctx(args)
    started = time.strftime("%Y-%m-%d %H:%M")
    results = []
    for name in selected:
        t0 = time.monotonic()
        print(f"… {name}", flush=True)
        try:
            res = RUNNERS[name](ctx)
        except Exception as e:  # noqa: BLE001 — сбой проверки — FAIL с причиной, остальные идут
            res = Result(name, "FAIL", f"исключение скрипта: {type(e).__name__}: {e}")
        results.append(res)
        print(f"{res.status:<4} {name:<10} {time.monotonic() - t0:6.0f} с  {res.summary}", flush=True)
        for d in res.details if res.status == "FAIL" else []:
            print(f"     {d}")
    write_report(args.out, results, started)
    for m in (m for r in results for m in r.manual):
        print(f"ручная: {m}")
    print(f"отчёт: {args.out / 'report.md'}")
    return 1 if any(r.status == "FAIL" for r in results) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
