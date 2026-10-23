#!/usr/bin/env python3
"""Найти файлы корпуса, которые сам MuPDF рисует слишком долго, — для tests/corpus/slow.json.

    "%OTTISK_PYTHON%" -X utf8 -I tools/scan_slow_corpus.py [--tag fuzz] [--config Debug] [--limit 30]
                                                             [--jobs 10] [--write]

Каждый файл тега из <корпус>/index.json — отдельный процесс `mutool draw -o <png> -w 32 <файл> 1`
(C:\\src\\mupdf\\platform\\win32\\x64\\<config>) со сроком --limit секунд. Все процессы — в одном Job Object:
2 ГБ на процесс и 8 ГБ на всё (корпус фаззов без предела уронил Windows 23.10.2026), без окон ошибок.
Файлы без расширения mutool не распознаёт — жёсткие ссылки <n>.pdf в %TEMP%\\ottisk-scan.
Печатает файлы дольше --slow секунд, с ненулевым кодом выхода, кроме 1 (ошибка MuPDF), и по сроку;
--write — записать файлы «по сроку» в tests/corpus/slow.json (engine.corpus.* их пропускает).
Тесты корпуса идут в Debug и msvc-asan — по умолчанию Debug: Release под пределом памяти сдаётся быстро.
"""

from __future__ import annotations

import argparse
import ctypes
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from concurrent.futures import ThreadPoolExecutor
from ctypes import wintypes
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


class _Io(ctypes.Structure):
    _fields_ = [(n, ctypes.c_ulonglong) for n in ("r", "w", "o", "rb", "wb", "ob")]


class _Basic(ctypes.Structure):
    _fields_ = [("PerProcessUserTimeLimit", ctypes.c_longlong), ("PerJobUserTimeLimit", ctypes.c_longlong),
                ("LimitFlags", wintypes.DWORD), ("MinimumWorkingSetSize", ctypes.c_size_t),
                ("MaximumWorkingSetSize", ctypes.c_size_t), ("ActiveProcessLimit", wintypes.DWORD),
                ("Affinity", ctypes.c_size_t), ("PriorityClass", wintypes.DWORD), ("SchedulingClass", wintypes.DWORD)]


class _Ext(ctypes.Structure):
    _fields_ = [("Basic", _Basic), ("Io", _Io), ("ProcessMemoryLimit", ctypes.c_size_t),
                ("JobMemoryLimit", ctypes.c_size_t), ("PeakProcessMemoryUsed", ctypes.c_size_t),
                ("PeakJobMemoryUsed", ctypes.c_size_t)]


def limit_memory(per_process_gb: int, total_gb: int) -> None:
    """Этот процесс и все дочерние — в Job Object с пределами памяти и без окон ошибок."""
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.CreateJobObjectW.restype = wintypes.HANDLE
    k32.GetCurrentProcess.restype = wintypes.HANDLE
    k32.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
    k32.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
    job = k32.CreateJobObjectW(None, None)
    info = _Ext()
    # PROCESS_MEMORY | JOB_MEMORY | DIE_ON_UNHANDLED_EXCEPTION
    info.Basic.LimitFlags = 0x100 | 0x200 | 0x400
    info.ProcessMemoryLimit = per_process_gb << 30
    info.JobMemoryLimit = total_gb << 30
    if not k32.SetInformationJobObject(job, 9, ctypes.byref(info), ctypes.sizeof(info)):
        raise OSError(ctypes.get_last_error(), "SetInformationJobObject")
    if not k32.AssignProcessToJobObject(job, k32.GetCurrentProcess()):
        raise OSError(ctypes.get_last_error(), "AssignProcessToJobObject")
    k32.SetErrorMode(0x0001 | 0x0002 | 0x8000)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--tag", default="fuzz")
    ap.add_argument("--config", default="Debug", choices=["Debug", "Release"])
    ap.add_argument("--slow", type=float, default=10.0, help="печатать файлы дольше стольких секунд")
    ap.add_argument("--limit", type=float, default=30.0, help="срок на файл, секунд")
    ap.add_argument("--jobs", type=int, default=10)
    ap.add_argument("--write", action="store_true", help="записать файлы «по сроку» в tests/corpus/slow.json")
    args = ap.parse_args()

    corpus = Path(os.environ.get("OTTISK_CORPUS") or r"C:\Tools\ottisk\corpus")
    index = json.loads((corpus / "index.json").read_text(encoding="utf-8"))
    recs = index["tags"].get(args.tag, [])
    if not recs:
        print(f"нет тега {args.tag} в {corpus / 'index.json'}")
        return 2
    mu = Path(os.environ.get("MUPDF_ROOT") or r"C:\src\mupdf")
    mutool = mu / "platform" / "win32" / "x64" / args.config / "mutool.exe"
    limit_memory(2, 8)
    tmp = Path(tempfile.gettempdir()) / "ottisk-scan" / f"{args.tag}-{args.config}"
    shutil.rmtree(tmp, ignore_errors=True)
    tmp.mkdir(parents=True)

    def one(item: tuple[int, dict]) -> tuple[int, str, float, object]:
        i, rec = item
        src = Path(rec["path"])
        if not src.exists():
            src = corpus / rec["rel"]
        link = tmp / f"{i}.pdf"
        png = tmp / f"{i}.png"
        try:
            os.link(src, link)
        except OSError:
            shutil.copyfile(src, link)
        t0 = time.time()
        try:
            rc: object = subprocess.run([str(mutool), "draw", "-o", str(png), "-w", "32", str(link), "1"],
                                        stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                                        timeout=args.limit).returncode
        except subprocess.TimeoutExpired:
            rc = "timeout"
        dt = time.time() - t0
        for f in (link, png):
            try:
                f.unlink()
            except OSError:
                pass
        return i, rec["rel"], dt, rc

    t0 = time.time()
    found: list[dict] = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for i, rel, dt, rc in pool.map(one, list(enumerate(recs))):
            if dt >= args.slow or rc == "timeout" or (isinstance(rc, int) and rc not in (0, 1)):
                found.append({"index": i, "rel": rel, "seconds": round(dt, 1), "rc": rc})
                print(f"{i:6d} {dt:6.1f} с  код {rc}  {rel}", flush=True)
    shutil.rmtree(tmp, ignore_errors=True)
    timeouts = sorted(f["rel"] for f in found if f["rc"] == "timeout")
    print(f"файлов {len(recs)}, отмечено {len(found)}, по сроку {len(timeouts)}, {time.time() - t0:.0f} с")
    if args.write:
        out = ROOT / "tests" / "corpus" / "slow.json"
        doc = json.loads(out.read_text(encoding="utf-8")) if out.exists() else {"note": "", "files": {}}
        stamp = time.strftime("%d.%m.%Y")
        for rel in timeouts:
            doc["files"].setdefault(rel, f"{args.config} mutool draw -w 32: дольше {args.limit:.0f} с ({stamp})")
        doc["files"] = dict(sorted(doc["files"].items()))
        out.write_text(json.dumps(doc, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(f"записано: {out} ({len(doc['files'])} файлов)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
