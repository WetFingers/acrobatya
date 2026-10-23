# -*- coding: utf-8 -*-
"""check_scans_ocr.py — живой OCR-замер сканов tests/fixtures/scans (не часть make_all).

Что: для каждого scan-NN.pdf (или перечисленных) — mutool draw -F ocr.txt -r 300 -t <языки>
-d <tessdata> и доля слов эталона scan-NN.txt, найденных в распознанном тексте (мультимножество,
без регистра и пунктуации, ё = е). Печатает таблицу и итог; код 0 всегда (это замер, не тест).
Откуда: mutool 1.28.5 (Tesseract из состава MuPDF), языки C:\\Tools\\tessdata (TESSDATA_PREFIX).
Детерминизм: OCR детерминирован для одной сборки MuPDF и одних traineddata.
Запуск: "C:\\Tools\\venv-test\\Scripts\\python.exe" -X utf8 -I tools/gen/check_scans_ocr.py [--root <корень>] [NN …]
"""
import argparse
import json
import os
import re
import sys
import time
from collections import Counter
from pathlib import Path

sys.dont_write_bytecode = True                     # без __pycache__ в tools/gen
sys.path.insert(0, str(Path(__file__).resolve().parent))

import _genlib as g  # noqa: E402


def words(text: str):
    text = text.lower().replace("ё", "е")
    return [w for w in re.findall(r"[0-9a-zа-яäöüß]+", text) if w]


def glue_letters(text: str) -> str:
    """MuPDF 1.28.5 в ocr.txt разделяет кириллические буквы пробелами («П р и в е т»),
    слова — переводами строк. Склеиваем буквы, разделённые одиночным пробелом."""
    return re.sub(r"(?<=[А-Яа-яЁё]) (?=[А-Яа-яЁё])", "", text)


def recall(truth: str, ocr: str) -> float:
    t = Counter(words(truth))
    o = Counter(words(ocr))
    hit = sum(min(c, o[w]) for w, c in t.items())
    return hit / max(1, sum(t.values()))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--root", default=str(g.REPO))
    ap.add_argument("--tessdata", default=os.environ.get("TESSDATA_PREFIX", "C:/Tools/tessdata"))
    ap.add_argument("nums", nargs="*")
    a = ap.parse_args()
    d = Path(a.root) / "tests" / "fixtures" / "scans"
    meta = json.loads((d / "scans.json").read_text(encoding="utf-8"))["scans"]
    if a.nums:
        meta = [m for m in meta if m["file"][5:7] in a.nums]
    tmp = Path(os.environ.get("TEMP", ".")) / "ottisk-gen" / "ocr"
    tmp.mkdir(parents=True, exist_ok=True)
    total = []
    for m in meta:
        out = tmp / (m["file"] + ".txt")
        t0 = time.time()
        r = g.run([g.mutool_path(), "draw", "-q", "-F", "ocr.txt", "-r", "300", "-t", m["tesseract"],
                   "-d", a.tessdata, "-o", out, d / m["file"]], check=False)
        dt = time.time() - t0
        if r.returncode != 0:
            print("%s: mutool код %d %s" % (m["file"], r.returncode, r.stderr.decode("utf-8", "replace")[:200]))
            continue
        truth = (d / m["truth"]).read_text(encoding="utf-8")
        ocr = out.read_text(encoding="utf-8", errors="replace")
        raw = recall(truth, ocr)
        glued = recall(truth, glue_letters(ocr))
        total.append(glued)
        print("%s  %-6s поворот %3d перекос %+.1f %-4s  доля слов %.3f (как есть %.3f)  %.1f с" % (
            m["file"], m["lang"], m["rotation_cw"], m["skew_deg_ccw"], m["mode"], glued, raw, dt))
    if total:
        print("среднее %.3f по %d файлам (после склейки букв)" % (sum(total) / len(total), len(total)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
