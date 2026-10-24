"""Рубеж Р1 — автоматическая часть контрольной точки M1 «Движок и читалка с печатью» (эталон 9.6; план «Рубеж Р1»).

    "%OTTISK_PYTHON%" -X utf8 -I tools/verify_r1.py [--release-exe build/release/bin/AcroBatya.exe]
        [--asan-exe build/asan/bin/AcroBatya.exe] [--bench-exe build/release/bin/bench.exe]
        [--unit-tests build/debug/bin/unit_tests.exe] [--only corpus,paging,…] [--skip corpus-asan]
        [--jobs 4] [--asan-jobs 2] [--out .queue/r1]

Проверки (каждая — ok / FAIL / SKIP; итог — <out>/verify_r1.json и таблица <out>/report.md):
  corpus       --open-all release-exe по тегам annots forms enc-r2…enc-r6 big pdfa-pass pdfa-fail cyr
               (<корпус>/index.json; файлы — жёсткими ссылками в <out>/corpus-farm/<тег>-NN, по 250): ≥ 2 000 файлов,
               ни одного падения процесса и сбоя нашего кода; таблица открытых / с паролем / повреждённых
  corpus-asan  то же под --asan-exe: ещё и ни одной строки «ERROR: AddressSanitizer» (предел памяти процесса 6 ГБ)
  paging       bench.exe ui-paging (PDF32000_2008.pdf, стр. 1→100 при 100 %): page_warm_ms (медиана, из кеша) ≤ 50,
               first_frame_ms ≤ 500; page_cold_ms ≤ 150 — для справки
  memory       bench.exe big-1200: peak_working_set_mb ≤ 800; release-exe --script над big-1200.pdf (открыть,
               страницы 1, 300, 600, 900, 1200) — пиковый рабочий набор процесса ≤ 800 МБ
  undo         release-exe --script: 50 случайных правок свойств (doc.setProperty, seed --seed; между ними 1,1 с —
               иначе одноимённые правки склеиваются в один шаг, UndoStack::kGroupWindow), doc.undo 50,
               doc.redo 50 — объекты (qpdf --json=2 без /ID трейлера) до правок = после отмены, с правками = после
               возврата
  autosave     release-exe --script: правка заголовка, автосохранение окна (documents/autosaveMinutes = 1), taskkill
               /F посреди «wait»; unit_tests autosave_child.DISABLED_recover_from_store восстанавливает правку
               из каталога автосохранения программы
  scale        release-exe --shots all-tools под QT_SCALE_FACTOR 1.25 и 1.5: кадр 1440 × 900 логических пикселей,
               синяя шапка report.pdf шире тайла (512 px устройства) однородна — без швов
  print        unit_tests print_compat.DISABLED_layouts_to_pdf (QPdfWriter = PdfFormat): n-up 2 × 2 и буклет
               report.pdf, постер A1 на A4 (10 мм) — qpdf --check = 0, размеры листов и какая страница где — по
               эталонам раскладки ниже (сверка растров pypdfium2 с исходными страницами)
  print-cli    release-exe /pt report.pdf под OTTISK_FAKE_PRINTER: код 0, PDF задания (12 листов), qpdf --check = 0
  scenes       release-exe --shots all-tools, home, shell-document, preferences-general в светлой и тёмной теме:
               PNG 1440 × 900, тёмная заметно темнее светлой
Корпус рубеж не качает: нет тега на диске — SKIP тега и строка «Нужна ручная проверка» в выводе.
Код: 0 — всё ok или SKIP; 1 — есть FAIL; 2 — неверные аргументы.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import ctypes
import json
import os
import random
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from ctypes import wintypes
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))
import bench_compare  # noqa: E402

QT_BIN = Path(os.environ.get("QT_ROOT", r"C:\Qt\6.12.0\msvc2022_64")) / "bin"
CORPUS = Path(os.environ.get("OTTISK_CORPUS", r"C:\Tools\ottisk\corpus"))
TOOLS = Path(os.environ.get("OTTISK_TOOLS", r"C:\Tools"))
TAGS = ["annots", "forms", "enc-r2", "enc-r3", "enc-r4", "enc-r5", "enc-r6", "big", "pdfa-pass", "pdfa-fail", "cyr"]
CHECKS = ["corpus", "corpus-asan", "paging", "memory", "undo", "autosave", "scale", "print", "print-cli", "scenes"]
MIN_CORPUS_FILES = 2000
CHUNK = 250
PT = 72.0
A4 = (595.2756, 841.8898)

# ---------------------------------------------------------------- процессы: предел памяти и пиковый рабочий набор

_k32 = ctypes.WinDLL("kernel32", use_last_error=True)
_k32.CreateJobObjectW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR]
_k32.CreateJobObjectW.restype = wintypes.HANDLE
_k32.SetInformationJobObject.argtypes = [wintypes.HANDLE, ctypes.c_int, ctypes.c_void_p, wintypes.DWORD]
_k32.SetInformationJobObject.restype = wintypes.BOOL
_k32.AssignProcessToJobObject.argtypes = [wintypes.HANDLE, wintypes.HANDLE]
_k32.AssignProcessToJobObject.restype = wintypes.BOOL
_k32.CloseHandle.argtypes = [wintypes.HANDLE]
_k32.K32GetProcessMemoryInfo.argtypes = [wintypes.HANDLE, ctypes.c_void_p, wintypes.DWORD]
_k32.K32GetProcessMemoryInfo.restype = wintypes.BOOL


class _IoCounters(ctypes.Structure):
    _fields_ = [(n, ctypes.c_ulonglong) for n in ("r", "w", "o", "rb", "wb", "ob")]


class _BasicLimit(ctypes.Structure):
    _fields_ = [("PerProcessUserTimeLimit", ctypes.c_int64), ("PerJobUserTimeLimit", ctypes.c_int64),
                ("LimitFlags", wintypes.DWORD), ("MinimumWorkingSetSize", ctypes.c_size_t),
                ("MaximumWorkingSetSize", ctypes.c_size_t), ("ActiveProcessLimit", wintypes.DWORD),
                ("Affinity", ctypes.c_size_t), ("PriorityClass", wintypes.DWORD), ("SchedulingClass", wintypes.DWORD)]


class _ExtendedLimit(ctypes.Structure):
    _fields_ = [("BasicLimitInformation", _BasicLimit), ("IoInfo", _IoCounters),
                ("ProcessMemoryLimit", ctypes.c_size_t), ("JobMemoryLimit", ctypes.c_size_t),
                ("PeakProcessMemoryUsed", ctypes.c_size_t), ("PeakJobMemoryUsed", ctypes.c_size_t)]


class _MemoryCounters(ctypes.Structure):
    _fields_ = [("cb", wintypes.DWORD), ("PageFaultCount", wintypes.DWORD), ("PeakWorkingSetSize", ctypes.c_size_t),
                ("WorkingSetSize", ctypes.c_size_t), ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPagedPoolUsage", ctypes.c_size_t), ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                ("QuotaNonPagedPoolUsage", ctypes.c_size_t), ("PagefileUsage", ctypes.c_size_t),
                ("PeakPagefileUsage", ctypes.c_size_t)]


def _memory_job(limit_mb: int) -> int | None:
    """Job Object с пределом памяти процесса (битые PDF заставляют MuPDF выделять гигабайты — 23.10.2026 прогон
    без предела уронил Windows) и KILL_ON_JOB_CLOSE."""
    job = _k32.CreateJobObjectW(None, None)
    if not job:
        return None
    info = _ExtendedLimit()
    info.BasicLimitInformation.LimitFlags = 0x2000 | (0x100 if limit_mb > 0 else 0)
    info.ProcessMemoryLimit = limit_mb * 1024 * 1024
    if not _k32.SetInformationJobObject(job, 9, ctypes.byref(info), ctypes.sizeof(info)):
        _k32.CloseHandle(job)
        return None
    return job


@dataclass
class Run:
    code: int
    out: str
    err: str
    seconds: float
    peak_mb: float
    timed_out: bool = False


def run(cmd: list[str], env: dict, timeout: float, memory_mb: int = 4096, cwd: Path | None = None,
        on_start=None) -> Run:
    """Процесс без окна с выводом во временные файлы (без тупика на канале), под Job Object."""
    t0 = time.monotonic()
    job = _memory_job(memory_mb)
    with tempfile.TemporaryFile() as fo, tempfile.TemporaryFile() as fe:
        p = subprocess.Popen(cmd, env=env, stdout=fo, stderr=fe, cwd=cwd, creationflags=subprocess.CREATE_NO_WINDOW)
        if job:
            _k32.AssignProcessToJobObject(job, int(p._handle))  # noqa: SLF001 — дескриптор процесса Popen
        if on_start:
            on_start(p)
        timed_out = False
        try:
            p.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            timed_out = True
            p.kill()
            p.wait()
        counters = _MemoryCounters()
        counters.cb = ctypes.sizeof(counters)
        peak = 0.0
        if _k32.K32GetProcessMemoryInfo(int(p._handle), ctypes.byref(counters), counters.cb):  # noqa: SLF001
            peak = counters.PeakWorkingSetSize / (1024 * 1024)
        if job:
            _k32.CloseHandle(job)
        fo.seek(0)
        fe.seek(0)
        out = fo.read().decode("utf-8", "replace")
        err = fe.read().decode("utf-8", "replace")
    return Run(p.returncode, out, err, time.monotonic() - t0, peak, timed_out)


def asan_dll_dirs() -> list[Path]:
    """Каталог clang_rt.asan_dynamic-x86_64.dll (MSVC 14.44 Build Tools) — для --asan-exe."""
    roots = [Path(os.environ.get("VCToolsInstallDir", "")) / "bin" / "Hostx64" / "x64"]
    for base in (Path(r"C:\Program Files (x86)\Microsoft Visual Studio"), Path(r"C:\Program Files\Microsoft Visual Studio")):
        roots += sorted(base.glob("*/*/VC/Tools/MSVC/14.44.*/bin/Hostx64/x64"), reverse=True)
    return [r for r in roots if (r / "clang_rt.asan_dynamic-x86_64.dll").is_file()][:1]


def app_env(data_dir: Path, extra_path: list[Path] | None = None, **extra: str) -> dict:
    env = dict(os.environ)
    env.update({"QT_QPA_PLATFORM": "offscreen", "QT_QPA_FONTDIR": "C:/Windows/Fonts", "OTTISK_INSTANCE": "test",
                "OTTISK_DATA_DIR": str(data_dir)})
    env["PATH"] = os.pathsep.join([*(str(p) for p in extra_path or []), str(QT_BIN), env.get("PATH", "")])
    env.update(extra)
    data_dir.mkdir(parents=True, exist_ok=True)
    return env


def qpdf_exe() -> str | None:
    for c in (os.environ.get("OTTISK_QPDF", ""), str(TOOLS / "qpdf" / "bin" / "qpdf.exe")):
        if c and Path(c).is_file():
            return c
    return shutil.which("qpdf")


def qpdf_check(path: Path) -> tuple[int, str]:
    exe = qpdf_exe()
    if not exe:
        return 77, "qpdf не найден"
    r = subprocess.run([exe, "--check", str(path)], capture_output=True, text=True, encoding="utf-8", errors="replace")
    return r.returncode, (r.stdout + r.stderr).strip().splitlines()[-1:][0] if (r.stdout + r.stderr).strip() else ""


def qpdf_objects(path: Path) -> dict:
    """Объекты PDF по qpdf --json=2 без /ID трейлера (время записи) — как ui.script_runner."""
    exe = qpdf_exe()
    r = subprocess.run([exe, "--json=2", "--json-key=qpdf", str(path)], capture_output=True)
    data = json.loads(r.stdout.decode("utf-8"))
    objects = data["qpdf"][1]
    objects.get("trailer", {}).get("value", {}).pop("/ID", None)
    return objects


# ---------------------------------------------------------------- итог


@dataclass
class Result:
    check: str
    status: str  # ok | FAIL | SKIP
    summary: str
    details: list[str] = field(default_factory=list)
    data: dict = field(default_factory=dict)
    manual: list[str] = field(default_factory=list)  # строки для «Нужна ручная проверка»


class Ctx:
    def __init__(self, args: argparse.Namespace):
        self.args = args
        self.out: Path = args.out
        self.release = resolve_exe(args.release_exe)
        self.asan = resolve_exe(args.asan_exe) if args.asan_exe else None
        self.bench = resolve_exe(args.bench_exe)
        self.unit = resolve_exe(args.unit_tests)

    def dir(self, *parts: str) -> Path:
        d = self.out.joinpath(*parts)
        d.mkdir(parents=True, exist_ok=True)
        return d

    def fresh(self, *parts: str) -> Path:
        d = self.out.joinpath(*parts)
        if d.exists():
            shutil.rmtree(d)
        d.mkdir(parents=True)
        return d


def resolve_exe(p: Path | None) -> Path | None:
    """build/release/AcroBatya.exe (как в плане) или build/release/bin/AcroBatya.exe — что есть."""
    if p is None:
        return None
    p = p if p.is_absolute() else ROOT / p
    if p.is_file():
        return p
    alt = p.parent / "bin" / p.name
    return alt if alt.is_file() else p


def need(ctx_exe: Path | None, what: str) -> Result | None:
    if ctx_exe is None or not ctx_exe.is_file():
        return Result(what, "SKIP", f"нет {ctx_exe}")
    return None


# ---------------------------------------------------------------- corpus / corpus-asan


@dataclass
class Chunk:
    tag: str
    dir: Path
    files: dict  # имя ссылки → запись index.json


def corpus_farm(farm: Path) -> tuple[list[Chunk], list[str], int]:
    index = json.loads((CORPUS / "index.json").read_text(encoding="utf-8"))
    tags = index.get("tags", {})
    if farm.exists():
        shutil.rmtree(farm)  # только жёсткие ссылки: сами файлы корпуса остаются
    chunks: list[Chunk] = []
    missing: list[str] = []
    copied = 0
    for tag in TAGS:
        entries = [e for e in tags.get(tag, []) if Path(e["path"]).is_file()]
        if not entries:
            missing.append(tag)
            continue
        for k in range(0, len(entries), CHUNK):
            d = farm / f"{tag}-{k // CHUNK:02d}"
            d.mkdir(parents=True)
            files = {}
            for i, e in enumerate(entries[k:k + CHUNK]):
                name = f"{k + i:05d}.pdf"
                try:
                    os.link(e["path"], d / name)
                except OSError:
                    shutil.copyfile(e["path"], d / name)
                    copied += 1
                files[name] = e
            chunks.append(Chunk(tag, d, files))
    return chunks, missing, copied


SUMMARY_RE = re.compile(r"open-all: (\d+) files, opened (\d+), refused (\d+), failures (\d+)")
LINE_RE = re.compile(r"^(refused|FAIL) (.+?\.pdf): (.*)$")


def open_chunk(exe: Path, chunk: Chunk, env: dict, memory_mb: int, per_file_s: float, log: Path) -> dict:
    """--open-all над каталогом части; падение процесса — виновник по последней строке «probe», остаток —
    новым запуском без уже пройденных файлов."""
    res = {"tag": chunk.tag, "files": 0, "opened": 0, "page1": 0, "password": 0, "damaged": 0, "failures": 0,
           "crashes": [],
           "asan": [], "fail_lines": [], "unexpected": [], "seconds": 0.0, "peak_mb": 0.0}
    remaining = sorted(chunk.files)
    with log.open("w", encoding="utf-8") as lf:
        while remaining:
            r = run([str(exe), "--open-all", str(chunk.dir)], env, timeout=60 + per_file_s * len(remaining),
                    memory_mb=memory_mb)
            lf.write(f"$ --open-all {chunk.dir} ({len(remaining)} files) → code {r.code}, {r.seconds:.1f} s\n")
            lf.write(r.out)
            lf.write(r.err)
            res["seconds"] += r.seconds
            res["peak_mb"] = max(res["peak_mb"], r.peak_mb)
            if "ERROR: AddressSanitizer" in r.err:
                excerpt = [ln for ln in r.err.splitlines() if "AddressSanitizer" in ln or ln.strip().startswith("#")]
                res["asan"].append("\n".join(excerpt[:12]))
            probes = [ln[6:].strip() for ln in r.out.splitlines() if ln.startswith("probe ")]
            m = SUMMARY_RE.search(r.out)
            for ln in r.out.splitlines():
                lm = LINE_RE.match(ln.strip())
                if not lm:
                    continue
                name = Path(lm.group(2)).name
                entry = chunk.files.get(name, {})
                reason = lm.group(3)
                if lm.group(1) == "FAIL":
                    res["fail_lines"].append(f"{entry.get('rel', name)}: {reason}")
                elif reason == "password":
                    res["password"] += 1
                else:
                    # «page 1: …» — открылся, но первая страница не рисуется; иначе — отказ (повреждён).
                    res["page1" if reason.startswith("page 1:") else "damaged"] += 1
                    if entry.get("expect") == "open":
                        res["unexpected"].append(f"{entry.get('rel', name)}: {reason}")
            if m and not r.timed_out:
                files, opened, refused, failures = map(int, m.groups())
                res["files"] += files
                res["opened"] += opened
                res["failures"] += failures
                if r.code != failures:
                    res["crashes"].append(f"код {r.code} после итоговой строки (выход процесса)")
                break
            # Падение (или зависание): виновник — последний «probe»; убрать пройденные и продолжить.
            culprit = Path(probes[-1]).name if probes else None
            entry = chunk.files.get(culprit or "", {})
            res["crashes"].append(f"{entry.get('rel', culprit)}: код {r.code}{' (таймаут)' if r.timed_out else ''}")
            done = set(Path(p).name for p in probes)
            res["files"] += len(done)
            # Строки отказов печатаются только в конце — у упавшего запуска их нет: пройденные до виновника
            # считаются открытыми (сбой был бы строкой FAIL, а она — тоже в конце).
            res["opened"] += max(0, len(done) - 1)
            for name in done:
                (chunk.dir / name).unlink(missing_ok=True)
            remaining = [n for n in remaining if n not in done]
            if not probes:
                break
    return res


def check_corpus(ctx: Ctx, asan: bool) -> Result:
    name = "corpus-asan" if asan else "corpus"
    exe = ctx.asan if asan else ctx.release
    if (skip := need(exe, name)) is not None:
        return skip
    if not (CORPUS / "index.json").is_file():
        return Result(name, "SKIP", f"нет {CORPUS / 'index.json'}",
                      manual=[f"Р1 · корпус: нет {CORPUS} — прогнать tools/verify_r1.py --only corpus после этапа 0"])
    extra = []
    if asan:
        dlls = asan_dll_dirs()
        if not dlls:
            return Result(name, "SKIP", "нет clang_rt.asan_dynamic-x86_64.dll (MSVC 14.44)")
        extra = dlls
    farm = ctx.out / ("corpus-farm-asan" if asan else "corpus-farm")
    chunks, missing, copied = corpus_farm(farm)
    logs = ctx.fresh("corpus-asan-logs" if asan else "corpus-logs")
    jobs = ctx.args.asan_jobs if asan else ctx.args.jobs
    memory = 6144 if asan else 4096
    per_file = 120.0 if asan else 45.0
    t0 = time.monotonic()
    results: list[dict] = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = []
        for ch in chunks:
            env = app_env(ctx.dir("data", f"{name}-{ch.dir.name}"), extra, OTTISK_OPEN_ALL_TRACE="1")
            futures.append(pool.submit(open_chunk, exe, ch, env, memory, per_file, logs / f"{ch.dir.name}.log"))
        for f in concurrent.futures.as_completed(futures):
            r = f.result()
            results.append(r)
            print(f"  {name}: {r['tag']} +{r['files']} файлов, {r['seconds']:.0f} с", flush=True)
    wall = time.monotonic() - t0
    per_tag: dict[str, dict] = {}
    for r in results:
        t = per_tag.setdefault(r["tag"], {"files": 0, "opened": 0, "page1": 0, "password": 0, "damaged": 0,
                                          "failures": 0,
                                          "crashes": [], "asan": [], "fail_lines": [], "unexpected": [],
                                          "seconds": 0.0, "peak_mb": 0.0})
        for k in ("files", "opened", "page1", "password", "damaged", "failures"):
            t[k] += r[k]
        for k in ("crashes", "asan", "fail_lines", "unexpected"):
            t[k] += r[k]
        t["seconds"] += r["seconds"]
        t["peak_mb"] = max(t["peak_mb"], r["peak_mb"])
    total = sum(t["files"] for t in per_tag.values())
    crashes = [c for t in per_tag.values() for c in t["crashes"]]
    asan_errors = [a for t in per_tag.values() for a in t["asan"]]
    fails = [f for t in per_tag.values() for f in t["fail_lines"]]
    details = ["| Тег | Файлов | Открыто | из них стр. 1 с ошибкой | С паролем | Повреждённых | Сбоев | Падений | Пик, МБ | Время, с |",
               "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|"]
    for tag in TAGS:
        if tag in per_tag:
            t = per_tag[tag]
            details.append(f"| {tag} | {t['files']} | {t['opened']} | {t['page1']} | {t['password']} | "
                           f"{t['damaged']} | {t['failures']} | {len(t['crashes'])} | {t['peak_mb']:.0f} | "
                           f"{t['seconds']:.0f} |")
        else:
            details.append(f"| {tag} | — | — | — | — | — | — | — | — | SKIP |")
    details += [f"падение: {c}" for c in crashes]
    details += [f"ASan: {a}" for a in asan_errors]
    details += [f"сбой нашего кода: {f}" for f in fails]
    unexpected = [u for t in per_tag.values() for u in t["unexpected"]]
    if unexpected:
        details.append(f"ожидалось «open», но отказ ({len(unexpected)}; сведения, не провал):")
        details += [f"  {u}" for u in unexpected[:40]]
    manual = [f"Р1 · корпус: тег «{t}» не скачан — открыть после fetch_corpus.py" for t in missing]
    ok = total >= MIN_CORPUS_FILES and not crashes and not asan_errors and not fails
    summary = (f"{total} файлов по {len(per_tag)} тегам за {wall:.0f} с: открыто "
               f"{sum(t['opened'] for t in per_tag.values())}, с паролем {sum(t['password'] for t in per_tag.values())}, "
               f"повреждённых {sum(t['damaged'] for t in per_tag.values())} (+ стр. 1 с ошибкой "
               f"{sum(t['page1'] for t in per_tag.values())}), сбоев {len(fails)}, падений {len(crashes)}"
               + (f", ASan-ошибок {len(asan_errors)}" if asan else "")
               + (f"; нет тегов: {', '.join(missing)}" if missing else "")
               + (f"; скопировано вместо ссылок: {copied}" if copied else ""))
    if total < MIN_CORPUS_FILES:
        summary += f" — меньше {MIN_CORPUS_FILES}"
    shutil.rmtree(farm, ignore_errors=True)
    return Result(name, "ok" if ok else "FAIL", summary, details, {"per_tag": per_tag, "wall_s": wall}, manual)


# ---------------------------------------------------------------- paging / memory (bench.exe, Release)


def bench_metrics(ctx: Ctx, mode: str, args: list[str], timeout: float) -> tuple[dict, Run, Path]:
    out = ctx.dir("bench") / f"{mode}.json"
    out.unlink(missing_ok=True)
    env = app_env(ctx.dir("data", f"bench-{mode}"))
    r = run([str(ctx.bench), mode, *args, "--json", str(out)], env, timeout=timeout, memory_mb=8192)
    metrics = {}
    if out.is_file():
        metrics = {k.split("/", 1)[-1]: m.value for k, m in bench_compare.load(out).items()}
    return metrics, r, out


def check_paging(ctx: Ctx) -> Result:
    if (skip := need(ctx.bench, "paging")) is not None:
        return skip
    m, r, out = bench_metrics(ctx, "ui-paging", ["--runs", "3", "--pages", "100"], 1200)
    if r.code == 77:
        return Result("paging", "SKIP", "нет PDF32000_2008.pdf (тег big)",
                      manual=["Р1 · листание PDF32000_2008.pdf — нет файла корпуса"])
    first, warm, cold = m.get("first_frame_ms"), m.get("page_warm_ms"), m.get("page_cold_ms")
    if r.code != 0 or first is None or warm is None:
        return Result("paging", "FAIL", f"bench ui-paging: код {r.code}, метрик {len(m)}",
                      (r.out + r.err).strip().splitlines()[-15:])
    ok = first <= 500 and warm <= 50
    summary = (f"PDF32000_2008.pdf 1→100 при 100 %: из кеша {warm:.1f} мс/стр. (≤ 50), первый кадр {first:.0f} мс "
               f"(≤ 500), без кеша {cold:.1f} мс/стр. (цель 5.8 ≤ 150)" if cold is not None else "")
    return Result("paging", "ok" if ok else "FAIL", summary, [f"JSON: {out}"], m)


def check_memory(ctx: Ctx) -> Result:
    if (skip := need(ctx.bench, "memory")) is not None:
        return skip
    m, r, out = bench_metrics(ctx, "big-1200", ["--runs", "3"], 1800)
    if r.code == 77:
        return Result("memory", "SKIP", "нет big-1200.pdf", manual=["Р1 · память big-1200.pdf — нет файла"])
    peak = m.get("peak_working_set_mb")
    if r.code != 0 or peak is None:
        return Result("memory", "FAIL", f"bench big-1200: код {r.code}", (r.out + r.err).strip().splitlines()[-15:])
    details = [f"bench big-1200: peak_working_set_mb {peak:.0f}, open_first_ms {m.get('open_first_ms', 0):.0f}, "
               f"page_uncached_ms {m.get('page_uncached_ms', 0):.1f}, page_cached_ms {m.get('page_cached_ms', 0):.1f}"]
    ok = peak <= 800
    app_peak = None
    big = next((p for p in (CORPUS / "gen" / "big-1200.pdf", ROOT / "build" / "release" / "gen" / "big-1200.pdf")
                if p.is_file()), None)
    if big and ctx.release and ctx.release.is_file():
        work = ctx.fresh("memory")
        steps = [{"cmd": "doc.open", "args": {"path": str(big)}}]
        for page in (1, 300, 600, 900, 1200):
            steps += [{"cmd": "view.page", "args": {"page": page}}, {"cmd": "wait", "args": {"ms": 400}}]
        steps.append({"cmd": "doc.close"})
        script = work / "big.json"
        script.write_text(json.dumps({"steps": steps}, ensure_ascii=False, indent=1), encoding="utf-8")
        ra = run([str(ctx.release), "--script", str(script), "--out", str(work), "--data-dir", str(work / "data")],
                 app_env(work / "data"), timeout=600, memory_mb=4096)
        app_peak = ra.peak_mb
        details.append(f"AcroBatya.exe --script big-1200 (стр. 1, 300, 600, 900, 1200): код {ra.code}, "
                       f"пиковый рабочий набор {ra.peak_mb:.0f} МБ, {ra.seconds:.1f} с")
        ok = ok and ra.code == 0 and ra.peak_mb <= 800
        if ra.code != 0:
            details += (ra.out + ra.err).strip().splitlines()[-10:]
    summary = f"big-1200.pdf: bench {peak:.0f} МБ" + (f", окно {app_peak:.0f} МБ" if app_peak is not None else "") + \
        " пиковой (≤ 800)"
    return Result("memory", "ok" if ok else "FAIL", summary, details, {"bench": m, "app_peak_mb": app_peak})


# ---------------------------------------------------------------- undo (doc.setProperty / doc.undo / doc.redo)

KEYS = ["title", "author", "subject", "keywords", "creator", "producer"]
WORDS = ["отчёт", "печать", "Ёжик", "Zürich", "αβγ", "R1", "проверка", "№ 7", "«кавычки»", "журнал", "undo", "ИТОГ"]


def check_undo(ctx: Ctx) -> Result:
    if (skip := need(ctx.release, "undo")) is not None:
        return skip
    if not qpdf_exe():
        return Result("undo", "SKIP", "нет qpdf")
    work = ctx.fresh("undo")
    src = work / "отчёт-undo.pdf"
    shutil.copyfile(ROOT / "tests" / "fixtures" / "demo" / "report.pdf", src)
    rng = random.Random(ctx.args.seed)
    edits = []
    for i in range(50):
        key = rng.choice(KEYS)
        value = "" if rng.random() < 0.08 else " ".join(rng.sample(WORDS, rng.randint(1, 4))) + f" {i + 1}"
        edits.append({"cmd": "doc.setProperty", "args": {"key": key, "value": value}, "name": f"правка {i + 1}: {key}"})
        # Одноимённые правки в пределах 1 с склеиваются в один шаг (UndoStack::kGroupWindow, как набор текста) —
        # пауза делает каждую правку своим шагом: undo 50 / redo 50 проходят все 50.
        edits.append({"cmd": "wait", "args": {"ms": 1100}})
    steps = [{"cmd": "doc.open", "args": {"path": str(src)}},
             {"cmd": "doc.saveCopy", "args": {"path": "${OUT_DIR}/before.pdf", "mode": "full"}},
             *edits,
             {"cmd": "doc.saveCopy", "args": {"path": "${OUT_DIR}/edited.pdf", "mode": "full"}},
             {"cmd": "doc.undo", "args": {"n": 50}},
             {"cmd": "doc.saveCopy", "args": {"path": "${OUT_DIR}/undone.pdf", "mode": "full"}},
             {"cmd": "doc.redo", "args": {"n": 50}},
             {"cmd": "doc.saveCopy", "args": {"path": "${OUT_DIR}/redone.pdf", "mode": "full"}},
             {"cmd": "doc.undo", "args": {"n": 50}},
             {"cmd": "doc.close"}]
    script = work / "undo50.json"
    script.write_text(json.dumps({"steps": steps}, ensure_ascii=False, indent=1), encoding="utf-8")
    r = run([str(ctx.release), "--script", str(script), "--out", str(work), "--data-dir", str(work / "data")],
            app_env(work / "data"), timeout=900)
    if r.code != 0 or "ok: " not in r.out:
        return Result("undo", "FAIL", f"--script: код {r.code}", (r.out + r.err).strip().splitlines()[-10:])
    files = {n: work / f"{n}.pdf" for n in ("before", "edited", "undone", "redone")}
    checks = {n: qpdf_check(p)[0] for n, p in files.items()}
    objs = {n: qpdf_objects(p) for n, p in files.items()}
    same_undo = objs["before"] == objs["undone"]
    same_redo = objs["edited"] == objs["redone"]
    changed = objs["before"] != objs["edited"]
    details = [f"qpdf --check: " + ", ".join(f"{n} {c}" for n, c in checks.items()),
               f"до правок = после undo 50: {same_undo}; с правками = после redo 50: {same_redo}; правки видны: {changed}"]
    if not same_undo:
        details += diff_objects(objs["before"], objs["undone"])
    if not same_redo:
        details += diff_objects(objs["edited"], objs["redone"])
    ok = same_undo and same_redo and changed and all(c == 0 for c in checks.values())
    return Result("undo", "ok" if ok else "FAIL",
                  f"50 правок свойств (seed {ctx.args.seed}): undo 50 → объекты исходные {'да' if same_undo else 'НЕТ'}, "
                  f"redo 50 → объекты с правками {'да' if same_redo else 'НЕТ'}; qpdf --check 0 у всех четырёх: "
                  f"{'да' if all(c == 0 for c in checks.values()) else 'НЕТ'}", details)


def diff_objects(a: dict, b: dict) -> list[str]:
    keys = sorted(set(a) | set(b))
    out = [f"  {k}: {json.dumps(a.get(k), ensure_ascii=False)[:160]} ≠ {json.dumps(b.get(k), ensure_ascii=False)[:160]}"
           for k in keys if a.get(k) != b.get(k)]
    return out[:10]


# ---------------------------------------------------------------- autosave (taskkill)


def check_autosave(ctx: Ctx) -> Result:
    if (skip := need(ctx.release, "autosave")) is not None:
        return skip
    if (skip := need(ctx.unit, "autosave")) is not None:
        return skip
    work = ctx.fresh("autosave")
    data = work / "data"
    data.mkdir()
    (data / "settings.ini").write_text("[documents]\nautosave=true\nautosaveMinutes=1\n", encoding="utf-8")
    doc = work / "отчёт-автосохранение.pdf"
    shutil.copyfile(ROOT / "tests" / "fixtures" / "demo" / "report.pdf", doc)
    title = f"Р1 автосохранение {ctx.args.seed}"
    steps = [{"cmd": "doc.open", "args": {"path": str(doc)}},
             {"cmd": "doc.setProperty", "args": {"key": "title", "value": title}},
             {"cmd": "wait", "args": {"ms": 240000}}]
    script = work / "autosave.json"
    script.write_text(json.dumps({"steps": steps}, ensure_ascii=False, indent=1), encoding="utf-8")
    index = data / "autosave" / "index.json"
    state = {"killed": False, "waited": 0.0}

    def killer(p: subprocess.Popen) -> None:
        # Ждём запись окна в index.json (интервал — 1 мин) и снимаем процесс taskkill /F посреди «wait».
        t0 = time.monotonic()
        while time.monotonic() - t0 < 200 and p.poll() is None:
            try:
                if index.is_file() and json.loads(index.read_text(encoding="utf-8")):
                    time.sleep(1.0)  # журнал пишется до index.json — запас на fsync
                    break
            except (OSError, ValueError):
                pass
            time.sleep(0.5)
        state["waited"] = time.monotonic() - t0
        if p.poll() is None and index.is_file():
            k = subprocess.run(["taskkill.exe", "/F", "/PID", str(p.pid)], capture_output=True)
            state["killed"] = k.returncode == 0

    r = run([str(ctx.release), "--script", str(script), "--out", str(work), "--data-dir", str(data)],
            app_env(data), timeout=300, on_start=killer)
    details = [f"окно: код {r.code}, ждали автосохранения {state['waited']:.0f} с, taskkill /F: {state['killed']}"]
    if not state["killed"]:
        return Result("autosave", "FAIL", "автосохранение не появилось за 200 с или процесс не снят",
                      details + (r.out + r.err).strip().splitlines()[-10:])
    env = app_env(work / "unit-data", OTTISK_AUTOSAVE_CHILD_DIR=str(data / "autosave"),
                  OTTISK_AUTOSAVE_CHILD_FILE=str(doc))
    u = run([str(ctx.unit), "--gtest_also_run_disabled_tests", "--gtest_filter=autosave_child.DISABLED_recover_from_store",
             "--gtest_brief=1"], env, timeout=120)
    m = re.search(r"recovered: steps=(\d+) position=(\d+) title=(.*)", u.out)
    details.append(f"unit_tests recover_from_store: код {u.code}" + (f", {m.group(0).strip()}" if m else ""))
    ok = u.code == 0 and m is not None and m.group(3).strip() == title
    if not ok:
        details += (u.out + u.err).strip().splitlines()[-12:]
    return Result("autosave", "ok" if ok else "FAIL",
                  f"правка заголовка пережила taskkill /F окна: восстановлена из автосохранения — {'да' if ok else 'НЕТ'}",
                  details)


# ---------------------------------------------------------------- scale (QT_SCALE_FACTOR 1.25 / 1.5)


def check_scale(ctx: Ctx) -> Result:
    if (skip := need(ctx.release, "scale")) is not None:
        return skip
    import numpy as np
    from PIL import Image

    details = []
    ok = True
    for factor in (1.25, 1.5):
        work = ctx.fresh("scale", f"{factor}")
        env = app_env(work / "data", QT_SCALE_FACTOR=str(factor))
        r = run([str(ctx.release), "--shots", "all-tools", "--out", str(work)], env, timeout=180)
        png = work / "all-tools.png"
        if r.code != 0 or not png.is_file():
            ok = False
            details.append(f"{factor}: --shots код {r.code}")
            continue
        img = np.asarray(Image.open(png).convert("RGB")).astype(int)
        h, w, _ = img.shape
        want = (round(1440 * factor), round(900 * factor))
        blue = (abs(img[:, :, 0] - 28) <= 6) & (abs(img[:, :, 1] - 71) <= 6) & (abs(img[:, :, 2] - 140) <= 6)
        cols = np.where(blue[::2, :].sum(axis=0) > 10)[0]
        if (w, h) != want or cols.size == 0:
            ok = False
            details.append(f"{factor}: кадр {w} × {h} (ждали {want[0]} × {want[1]}), синей шапки нет")
            continue
        x0, x1 = int(cols.min()), int(cols.max())
        rows = np.where(blue[:, x0:x1 + 1].sum(axis=1) * 10 > (x1 - x0 + 1) * 6)[0]
        y0 = int(rows.min())
        y1 = y0
        while y1 + 1 < h and (y1 + 1) in set(rows.tolist()):
            y1 += 1
        band = img[y0 + 4:y0 + max(5, (y1 - y0) // 3), x0 + 4:x1 - 3]
        ref = img[y0 + 4, (x0 + x1) // 2]
        bad = int((np.abs(band - ref) > 4).any(axis=2).sum())
        wide = x1 - x0 > 512 + 16
        good = wide and bad == 0 and y1 > y0 + 20
        ok = ok and good
        details.append(f"{factor}: кадр {w} × {h}, шапка {x1 - x0} × {y1 - y0} px (тайл 512), "
                       f"пикселей не того цвета {bad} — {'без швов' if good else 'ШОВ'}")
    return Result("scale", "ok" if ok else "FAIL", "тайлы без швов при QT_SCALE_FACTOR 1,25 и 1,5: "
                  + ("да" if ok else "НЕТ"), details,
                  manual=["Р1 · окно при масштабе Windows 125 % и 150 % — на настоящем экране (M1)"])


# ---------------------------------------------------------------- print (эталоны раскладки)

# Эталоны раскладки (эталон 3.3, считаны вручную, без кода imposition): QPdfWriter — лист без полей.
#   n-up 2 × 2 по горизонтали, A4 книжный на A4 книжном: ячейка — четверть листа, страница в ней 1 : 2;
#     листы [1 2 / 3 4], [5 6 / 7 8], [9 10 / 11 12];
#   буклет 12 страниц (обе стороны, корешок слева): альбомный A4, половина листа — страница 1 : √2;
#     листы (лицо, оборот, …): [12 | 1], [2 | 11], [10 | 3], [4 | 9], [8 | 5], [6 | 7];
#   постер A1 (1683,78 × 2383,94 pt) на A4 при 100 % с перекрытием 10 мм: 3 × 3 книжных листа слева направо и сверху
#     вниз, шаг — лист минус перекрытие; где начинается сетка, скрипт находит по листу 1 (левый верхний угол
#     плаката с цветным квадратом), остальные листы обязаны лечь на плакат с тем же сдвигом.
NUP_SHEETS = [[1, 2, 3, 4], [5, 6, 7, 8], [9, 10, 11, 12]]
BOOKLET_SHEETS = [[12, 1], [2, 11], [10, 3], [4, 9], [8, 5], [6, 7]]
OVERLAP = 10 * PT / 25.4
DPI = 36  # постер: плакат A1 целиком
ID_DPI = 72  # какая страница в ячейке: при 36 dpi текстовые страницы report.pdf почти неразличимы


def render_gray(page, dpi: float = DPI):
    import numpy as np

    bmp = page.render(scale=dpi / PT, grayscale=True)
    return np.asarray(bmp.to_pil().convert("L")).astype(float)


def blur(a):
    from scipy.ndimage import gaussian_filter  # scikit-image тянет scipy

    return gaussian_filter(a, 1.2)


def fit(img, w: int, h: int):
    import numpy as np
    from PIL import Image

    return np.asarray(Image.fromarray(img.astype("uint8")).resize((w, h), Image.BILINEAR)).astype(float)


def mad(a, b) -> float:
    import numpy as np

    return float(np.mean(np.abs(blur(a) - blur(b))))


def identify(cell, pages: list) -> tuple[int, float, float]:
    """Какая исходная страница в ячейке: (номер с 1, расхождение лучшей, расхождение второй)."""
    h, w = cell.shape
    scores = sorted((mad(cell, fit(p, w, h)), i + 1) for i, p in enumerate(pages))
    return scores[0][1], scores[0][0], scores[1][0] if len(scores) > 1 else 255.0


def distinct(found: tuple[int, float, float]) -> bool:
    """Страница узнана уверенно: расхождение мало и заметно меньше, чем у второй по сходству."""
    return found[1] < 8 and found[1] * 1.4 < found[2]


def check_print(ctx: Ctx) -> Result:
    if (skip := need(ctx.unit, "print")) is not None:
        return skip
    import numpy as np
    import pypdfium2 as pdfium

    work = ctx.fresh("print")
    env = app_env(work / "data", OTTISK_COMPAT_OUT=str(work))
    u = run([str(ctx.unit), "--gtest_also_run_disabled_tests", "--gtest_filter=print_compat.DISABLED_layouts_to_pdf",
             "--gtest_brief=1"], env, timeout=300)
    out = work / "print" / "layouts"
    if u.code != 0:
        return Result("print", "FAIL", f"unit_tests layouts_to_pdf: код {u.code}", (u.out + u.err).splitlines()[-12:])
    details = []
    ok = True
    for name in ("nup4.pdf", "booklet.pdf", "poster.pdf"):
        code, last = qpdf_check(out / name)
        ok = ok and code == 0
        details.append(f"qpdf --check {name}: {code} ({last})")

    report = pdfium.PdfDocument(str(ROOT / "tests" / "fixtures" / "demo" / "report.pdf"))
    src = [render_gray(report[i], ID_DPI) for i in range(len(report))]

    def sheets_of(name: str, dpi: float = ID_DPI):
        d = pdfium.PdfDocument(str(out / name))
        return [(d[i].get_size(), render_gray(d[i], dpi)) for i in range(len(d))]

    # n-up
    nup = sheets_of("nup4.pdf")
    got_nup = []
    margin_ok = True
    for (size, img), want in zip(nup, NUP_SHEETS):
        h, w = img.shape
        cells = [img[r * h // 2:(r + 1) * h // 2, c * w // 2:(c + 1) * w // 2] for r in range(2) for c in range(2)]
        ids = [identify(c, src) for c in cells]
        got_nup.append([i[0] for i in ids])
        margin_ok = margin_ok and all(distinct(i) for i in ids)
        ok = ok and abs(size[0] - A4[0]) < 1 and abs(size[1] - A4[1]) < 1
    nup_ok = len(nup) == 3 and got_nup == NUP_SHEETS and margin_ok
    details.append(f"n-up 2 × 2: {len(nup)} листов A4 книжных, страницы {got_nup} (эталон {NUP_SHEETS}) — "
                   f"{'совпало' if nup_ok else 'НЕ СОВПАЛО'}")
    # буклет
    booklet = sheets_of("booklet.pdf")
    got_booklet = []
    b_ok = len(booklet) == 6
    for (size, img), want in zip(booklet, BOOKLET_SHEETS):
        h, w = img.shape
        ids = [identify(img[:, :w // 2], src), identify(img[:, w // 2:], src)]
        got_booklet.append([i[0] for i in ids])
        b_ok = b_ok and size[0] > size[1] and abs(size[0] - A4[1]) < 1 and all(distinct(i) for i in ids)
    b_ok = b_ok and got_booklet == BOOKLET_SHEETS
    details.append(f"буклет: {len(booklet)} альбомных листов, страницы {got_booklet} (эталон {BOOKLET_SHEETS}) — "
                   f"{'совпало' if b_ok else 'НЕ СОВПАЛО'}")
    # постер
    mixed = pdfium.PdfDocument(str(ROOT / "tests" / "fixtures" / "pages" / "mixed-sizes.pdf"))
    a1 = render_gray(mixed[3])
    poster = sheets_of("poster.pdf", DPI)
    p_ok, p_details = poster_matches(a1, poster)
    details += p_details
    ok = ok and nup_ok and b_ok and p_ok
    return Result("print", "ok" if ok else "FAIL",
                  f"PdfFormat: n-up {'ok' if nup_ok else 'FAIL'}, буклет {'ok' if b_ok else 'FAIL'}, постер "
                  f"{'ok' if p_ok else 'FAIL'}; qpdf --check = 0 у всех: "
                  f"{'да' if all('qpdf --check' not in d or ': 0 ' in d for d in details) else 'НЕТ'}", details,
                  manual=["Р1 · буклет на «Microsoft Print to PDF» открыть в Edge (M1)"])


def poster_matches(a1, poster) -> tuple[bool, list[str]]:
    """Листы постера ложатся на плакат сеткой 3 × 3 с шагом «лист − перекрытие» и общим сдвигом (по листу 1)."""
    import numpy as np
    from skimage.feature import match_template

    details = []
    if len(poster) != 9:
        return False, [f"постер: {len(poster)} листов (эталон 9 = 3 × 3)"]
    px = DPI / PT
    sw, sh = poster[0][1].shape[1], poster[0][1].shape[0]
    step = ((A4[0] - OVERLAP) * px, (A4[1] - OVERLAP) * px)
    # Белые поля вокруг плаката: сетка шире плаката и выходит за его края.
    pad = int(80 * px)
    canvas = np.full((a1.shape[0] + 2 * pad, a1.shape[1] + 2 * pad), 255.0)
    canvas[pad:pad + a1.shape[0], pad:pad + a1.shape[1]] = a1
    first = poster[0][1]
    win = canvas[:int(sh + 2 * pad), :int(sw + 2 * pad)]
    res = match_template(blur(win), blur(first))
    oy, ox = np.unravel_index(int(np.argmax(res)), res.shape)
    details.append(f"постер: лист 1 на плакате со сдвигом ({(ox - pad) / px:.1f}, {(oy - pad) / px:.1f}) pt, "
                   f"корреляция {res.max():.3f}")
    ok = res.max() > 0.8
    worst = 0.0
    for k, (size, img) in enumerate(poster):
        r, c = divmod(k, 3)
        ok = ok and abs(size[0] - A4[0]) < 1 and abs(size[1] - A4[1]) < 1
        x = int(round(ox + c * step[0]))
        y = int(round(oy + r * step[1]))
        best = 255.0
        for dy in range(-2, 3):
            for dx in range(-2, 3):
                crop = canvas[y + dy:y + dy + img.shape[0], x + dx:x + dx + img.shape[1]]
                if crop.shape == img.shape:
                    best = min(best, mad(crop, img))
        worst = max(worst, best)
        ok = ok and best < 6
    details.append(f"постер: 9 книжных листов A4, сетка 3 × 3 с перекрытием 10 мм — худшее расхождение листа с "
                   f"куском плаката {worst:.2f} из 255 ({'ok' if ok else 'FAIL'}, порог 6)")
    return ok, details


def check_print_cli(ctx: Ctx) -> Result:
    if (skip := need(ctx.release, "print-cli")) is not None:
        return skip
    work = ctx.fresh("print-cli")
    printed = work / "printed"
    env = app_env(work / "data", OTTISK_FAKE_PRINTER=str(printed))
    src = ROOT / "tests" / "fixtures" / "demo" / "report.pdf"
    r = run([str(ctx.release), "/pt", str(src), "Fake Printer", "Fake Driver", "FAKE:"], env, timeout=300)
    pdfs = sorted(printed.glob("*.pdf")) if printed.is_dir() else []
    details = [f"/pt: код {r.code}, {r.seconds:.1f} с, файлов {len(pdfs)}"]
    ok = r.code == 0 and len(pdfs) == 1
    if pdfs:
        import pypdfium2 as pdfium

        code, last = qpdf_check(pdfs[0])
        n = len(pdfium.PdfDocument(str(pdfs[0])))
        details.append(f"{pdfs[0].name}: листов {n}, qpdf --check {code} ({last})")
        ok = ok and code == 0 and n == 12
    if r.code != 0:
        details += (r.out + r.err).strip().splitlines()[-8:]
    return Result("print-cli", "ok" if ok else "FAIL", "/pt без окна под OTTISK_FAKE_PRINTER: "
                  + ("12 листов, qpdf --check = 0" if ok else "НЕТ"), details,
                  manual=["Р1 · Проводник «Печать» на настоящем принтере после установки 0.2.0"])


# ---------------------------------------------------------------- scenes


def check_scenes(ctx: Ctx) -> Result:
    if (skip := need(ctx.release, "scenes")) is not None:
        return skip
    import numpy as np
    from PIL import Image

    names = ["all-tools", "home", "shell-document", "preferences-general"]
    details = []
    ok = True
    means = {}
    for theme in ("light", "dark"):
        work = ctx.fresh("scenes", theme)
        r = run([str(ctx.release), "--shots", ",".join(names), "--theme", theme, "--out", str(work)],
                app_env(work / "data"), timeout=300)
        ok = ok and r.code == 0
        for n in names:
            png = next(iter(sorted(work.glob(f"{n}*.png"))), None)
            if png is None:
                ok = False
                details.append(f"{theme}: нет {n}.png (код {r.code})")
                continue
            img = Image.open(png)
            good = img.size == (1440, 900)
            ok = ok and good
            means[(theme, n)] = float(np.asarray(img.convert("L")).mean())
            details.append(f"{theme}: {png.name} {img.size[0]} × {img.size[1]}, средняя яркость "
                           f"{means[(theme, n)]:.0f}")
    for n in names:
        if ("light", n) in means and ("dark", n) in means and not means[("dark", n)] < means[("light", n)] - 20:
            ok = False
            details.append(f"{n}: тёмная тема не темнее светлой")
    return Result("scenes", "ok" if ok else "FAIL", "«Все инструменты», Главная, документ, Установки в светлой и тёмной "
                  "теме: " + ("сняты 1440 × 900" if ok else "НЕТ"), details)


# ---------------------------------------------------------------- main

RUNNERS = {
    "corpus": lambda c: check_corpus(c, False),
    "corpus-asan": lambda c: check_corpus(c, True),
    "paging": check_paging,
    "memory": check_memory,
    "undo": check_undo,
    "autosave": check_autosave,
    "scale": check_scale,
    "print": check_print,
    "print-cli": check_print_cli,
    "scenes": check_scenes,
}


def write_report(out: Path, results: list[Result], started: str) -> None:
    lines = [f"# Рубеж Р1 · tools/verify_r1.py · {started}", "", "| Проверка | Итог | Что |", "|---|---|---|"]
    lines += [f"| {r.check} | {r.status} | {r.summary} |" for r in results]
    for r in results:
        if r.details:
            lines += ["", f"## {r.check}", "", *r.details]
    manual = [m for r in results for m in r.manual]
    if manual:
        lines += ["", "## Нужна ручная проверка", "", *(f"- {m}" for m in manual)]
    (out / "report.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    (out / "verify_r1.json").write_text(json.dumps(
        {"started": started, "results": [r.__dict__ for r in results]}, ensure_ascii=False, indent=1, default=str),
        encoding="utf-8")


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--release-exe", type=Path, default=Path("build/release/bin/AcroBatya.exe"))
    ap.add_argument("--asan-exe", type=Path, help="AcroBatya.exe под ASan (build/asan/bin) — проверка corpus-asan")
    ap.add_argument("--bench-exe", type=Path, default=Path("build/release/bin/bench.exe"))
    ap.add_argument("--unit-tests", type=Path, default=Path("build/debug/bin/unit_tests.exe"),
                    help="unit_tests с помощниками autosave_child/print_compat (Debug годится: это не замер)")
    ap.add_argument("--only", help="проверки через запятую: " + ", ".join(CHECKS))
    ap.add_argument("--skip", help="пропустить проверки через запятую")
    ap.add_argument("--jobs", type=int, default=4, help="параллельных --open-all (release)")
    ap.add_argument("--asan-jobs", type=int, default=2, help="параллельных --open-all под ASan")
    ap.add_argument("--seed", type=int, default=20261024)
    ap.add_argument("--out", type=Path, default=Path(".queue/r1"))
    args = ap.parse_args(argv)
    args.out = (args.out if args.out.is_absolute() else ROOT / args.out).resolve()
    selected = [c for c in (args.only.split(",") if args.only else CHECKS) if c]
    if args.skip:
        selected = [c for c in selected if c not in args.skip.split(",")]
    if not args.asan_exe and not args.only:
        selected = [c for c in selected if c != "corpus-asan"]
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
        print(f"{res.status:<4} {name:<12} {time.monotonic() - t0:6.0f} с  {res.summary}", flush=True)
        for d in res.details if res.status == "FAIL" else []:
            print(f"     {d}")
    write_report(args.out, results, started)
    manual = [m for r in results for m in r.manual]
    for m in manual:
        print(f"ручная: {m}")
    print(f"отчёт: {args.out / 'report.md'}")
    return 1 if any(r.status == "FAIL" for r in results) else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
