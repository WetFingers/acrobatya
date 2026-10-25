"""Полная проверка «АкроБати» (эталон 9.5): configure при нужде → сборка ∥ стражи → ctest.

    py -3.14 tools/verify.py                       # msvc-debug: сборка, стражи, ctest -L "unit|ui" -LE slow
    py -3.14 tools/verify.py --fast                # без ctest
    py -3.14 tools/verify.py --no-build            # без configure и сборки (стражи и ctest по готовой сборке)
    py -3.14 tools/verify.py --fast --no-build     # только стражи
    py -3.14 tools/verify.py --full                # ещё ctest -L "render|engine|compat|e2e" -j 6 --repeat until-pass:2
    py -3.14 tools/verify.py --preset msvc-release --labels "render|bench" --no-configure

Шаги:
  (1) cmake --preset <p>, если нет build/<dir>/CMakeCache.txt или CMakeLists.txt, vcpkg.json,
      CMakePresets.json, CMakeUserPresets.json, cmake/*.cmake новее кэша → build/<dir>/configure.log;
  (2) cmake --build --preset <p> → build/<dir>/build.log (под замком .queue/build.lock — протокол
      tools/build_lock.ps1: строки pid=, pidStart=, taken=, target=, preset=; чужой ждём --lock-wait мин);
  (3) параллельно с (1)–(2): стражи tools/*_guard.py и scope_check.py через OTTISK_PYTHON
      (-X utf8 -I, код 77 — пропуск) и PowerShell-стражи tools/test_reset_parse.ps1, tools/test_guard.ps1;
  (4) после зелёной сборки: ctest --test-dir build/<dir> -L <labels> -LE slow --timeout 60 -j 12 --repeat until-pass:2
      --output-on-failure --no-tests=error --output-junit <abs>/build/<dir>/ctest.xml
      (QT_QPA_PLATFORM=offscreen, QT_QPA_FONTDIR=C:/Windows/Fonts, QT_FORCE_STDERR_LOGGING=1, Qt bin в PATH);
      если в --labels есть заведомо медленные метки (render, engine, e2e, compat, build, bench) —
      без -LE slow: у render_tests метки «render;slow», и рубежный --labels "render|bench" иначе
      не запустил бы ни одного теста рендера.
Логи configure.log и build.log пишутся по ходу (хвост виден во время сборки, обрыв оставляет лог).
Окружение cmake: VCPKG_ROOT — OTTISK_VCPKG_ROOT или C:\\Tools\\vcpkg, если он пуст или указывает
на vcpkg из Visual Studio (как в tools\\vcenv.cmd); QT_ROOT, MUPDF_ROOT, OTTISK_PYTHON — по умолчанию.
Печатает строку на каждый шаг (ok/FAIL/skip, секунды) и вывод ТОЛЬКО упавших: для сборки — строки
«error C», «error LNK», «FAILED» и хвост 60 строк build.log. Код 0 — всё зелёное, 1 — что-то упало,
2 — ошибка запуска. Если cl.exe нет в PATH, перезапускается через cmd /c "tools\\vcenv.cmd && …".
"""

from __future__ import annotations

import argparse
import datetime as dt
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PRESET_DIRS = {
    "msvc-debug": "debug",
    "msvc-release": "release",
    "msvc-asan": "asan",
    "msvc-fuzz": "fuzz",
    "msvc-analyze": "analyze",
}
# (имя шага, скрипт) — ctest-имена unit.guard.<имя> регистрирует cmake/tests.cmake.
PY_GUARDS = [
    ("guard.i18n", "i18n_guard.py"),
    ("guard.fztry", "fztry_guard.py"),
    ("guard.boundaries", "boundaries_guard.py"),
    ("guard.scope", "scope_check.py"),
    ("guard.encoding", "encoding_guard.py"),
    ("guard.no_registry", "no_registry_guard.py"),
    ("guard.format", "format_guard.py"),
]
PS_GUARDS = [
    ("guard.reset_parse", "test_reset_parse.ps1"),
    ("guard.queue_guard", "test_guard.ps1"),
]
CONFIGURE_INPUTS = ["CMakeLists.txt", "vcpkg.json", "CMakePresets.json", "CMakeUserPresets.json"]
DEFAULT_PYTHON = r"C:\Tools\venv-test\Scripts\python.exe"
DEFAULT_QT_ROOT = r"C:\Qt\6.12.0\msvc2022_64"
SKIP_CODE = 77
REEXEC_FLAG = "OTTISK_VERIFY_VCENV"
BUILD_ERROR_RE = re.compile(r"error C\d+|error LNK\d+|\bFAILED\b|fatal error|CMake Error|ninja: build stopped")
# При /WX (COMPILE_WARNING_AS_ERROR) причина — строка «warning C…» рядом с «error C2220».
BUILD_WARNING_RE = re.compile(r"warning C\d+")
# Метки, тесты которых помечены ещё и «slow» (cmake/tests.cmake): при явном запросе -LE slow не ставится.
SLOW_CLASS_LABELS = {"render", "engine", "e2e", "compat", "build", "bench", "slow"}
DEFAULT_MUPDF_ROOT = r"C:\src\mupdf"
CTEST_PASSED_RE = re.compile(r"^\s*\d+/\d+ Test\s+#\d+: .*\.\s*Passed\s")

_print_lock = threading.Lock()


@dataclass
class Step:
    name: str
    status: str  # ok | FAIL | skip
    seconds: float = 0.0
    note: str = ""
    output: list[str] = field(default_factory=list)


def say(line: str = "") -> None:
    with _print_lock:
        print(line, flush=True)


def report(step: Step) -> None:
    note = f"  — {step.note}" if step.note else ""
    lines = [f"{step.status:4} {step.name:22} {step.seconds:7.1f} с{note}"]
    if step.status == "FAIL" and step.output:
        lines.append("-" * 100)
        lines.extend(step.output)
        lines.append("-" * 100)
    say("\n".join(lines))


def run_capture(cmd: list[str] | str, env: dict[str, str], timeout: float | None = None) -> tuple[int, str]:
    try:
        proc = subprocess.run(
            cmd,
            cwd=ROOT,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout,
        )
        return proc.returncode, proc.stdout or ""
    except subprocess.TimeoutExpired as exc:
        out = exc.stdout if isinstance(exc.stdout, str) else (exc.stdout or b"").decode("utf-8", "replace")
        return -1, out + f"\n[verify] таймаут {timeout:.0f} с"
    except OSError as exc:
        return -2, f"[verify] не запустилось: {exc}"


def run_logged(cmd: list[str], env: dict[str, str], log: Path, timeout: float) -> tuple[int, str]:
    """Как run_capture, но вывод сразу пишется в log (хвост виден по ходу, обрыв оставляет лог)."""
    log.parent.mkdir(parents=True, exist_ok=True)
    with open(log, "wb") as fh:
        try:
            proc = subprocess.Popen(cmd, cwd=ROOT, env=env, stdout=fh, stderr=subprocess.STDOUT)
        except OSError as exc:
            msg = f"[verify] не запустилось: {exc}"
            fh.write(msg.encode("utf-8"))
            return -2, msg
        try:
            code = proc.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
            fh.write(f"\n[verify] таймаут {timeout:.0f} с\n".encode("utf-8"))
            code = -1
    return code, log.read_bytes().decode("utf-8", "replace")


def tail(text: str, n: int) -> list[str]:
    return text.rstrip().splitlines()[-n:]


# ---------------------------------------------------------------- перезапуск через vcenv.cmd


def maybe_reexec(argv: list[str], need_msvc: bool) -> int | None:
    if not need_msvc or shutil.which("cl"):
        return None
    if os.environ.get(REEXEC_FLAG):
        print("verify: cl.exe не появился в PATH даже после tools\\vcenv.cmd — проверьте Build Tools 2022")
        return 2
    vcenv = ROOT / "tools" / "vcenv.cmd"
    if not vcenv.is_file():
        print("verify: cl.exe нет в PATH и нет tools\\vcenv.cmd")
        return 2
    # Каждый аргумент в кавычках: иначе «unit|ui» cmd.exe понял бы как конвейер.
    quoted = " ".join(f'"{a}"' for a in [sys.executable, str(Path(__file__).resolve()), *argv])
    inner = f'call "{vcenv}" && {quoted}'
    env = dict(os.environ)
    env[REEXEC_FLAG] = "1"
    print("verify: cl.exe нет в PATH — перезапуск через tools\\vcenv.cmd", flush=True)
    comspec = os.environ.get("ComSpec", r"C:\Windows\System32\cmd.exe")
    proc = subprocess.run(f'"{comspec}" /d /s /c "{inner}"', cwd=ROOT, env=env)
    return proc.returncode


# ---------------------------------------------------------------- стражи


def python_for_guards() -> tuple[str, str]:
    value = os.environ.get("OTTISK_PYTHON") or DEFAULT_PYTHON
    if Path(value).is_file():
        return value, ""
    return sys.executable, f"OTTISK_PYTHON ({value}) нет — стражи на {sys.executable}"


def run_py_guard(name: str, script: str, python: str, env: dict[str, str]) -> Step:
    path = ROOT / "tools" / script
    if not path.is_file():
        return Step(name, "skip", note=f"нет tools/{script}")
    started = time.monotonic()
    code, out = run_capture([python, "-X", "utf8", "-I", str(path), "--root", str(ROOT)], env, timeout=900)
    seconds = time.monotonic() - started
    last = out.strip().splitlines()[-1] if out.strip() else ""
    if code == 0:
        return Step(name, "ok", seconds)
    if code == SKIP_CODE:
        return Step(name, "skip", seconds, note=last or "инструмента нет")
    return Step(name, "FAIL", seconds, note=f"код {code}", output=out.rstrip().splitlines()[:300])


def run_ps_guard(name: str, script: str, env: dict[str, str]) -> Step:
    path = ROOT / "tools" / script
    if not path.is_file():
        return Step(name, "skip", note=f"нет tools/{script} — пропуск")
    started = time.monotonic()
    cmd = ["powershell.exe", "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", str(path)]
    code, out = run_capture(cmd, env, timeout=900)
    seconds = time.monotonic() - started
    if code == 0:
        return Step(name, "ok", seconds)
    return Step(name, "FAIL", seconds, note=f"код {code}", output=tail(out, 120))


# ---------------------------------------------------------------- configure / build / ctest


def needs_configure(build_dir: Path) -> tuple[bool, str]:
    cache = build_dir / "CMakeCache.txt"
    if not cache.is_file():
        return True, "нет CMakeCache.txt"
    cache_time = cache.stat().st_mtime
    inputs = [ROOT / n for n in CONFIGURE_INPUTS] + sorted((ROOT / "cmake").glob("*.cmake"))
    newer = [p.relative_to(ROOT).as_posix() for p in inputs if p.is_file() and p.stat().st_mtime > cache_time]
    if newer:
        return True, "новее кэша: " + ", ".join(newer[:5])
    return False, ""


def _kernel32():
    import ctypes
    from ctypes import wintypes

    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.OpenProcess.restype = wintypes.HANDLE
    k32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    k32.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
    k32.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
    k32.CloseHandle.argtypes = [wintypes.HANDLE]
    return k32


def _process_start_ticks(pid: int | None = None) -> int | None:
    """Время старта процесса в тиках .NET (как StartTime.ToUniversalTime().Ticks в build_lock.ps1)."""
    try:
        import ctypes
        from ctypes import wintypes

        k32 = _kernel32()
        handle = k32.OpenProcess(0x1000, False, pid if pid is not None else os.getpid())  # QUERY_LIMITED
        if not handle:
            return None
        try:
            times = [wintypes.FILETIME() for _ in range(4)]
            if not k32.GetProcessTimes(handle, *(ctypes.byref(t) for t in times)):
                return None
            created = (times[0].dwHighDateTime << 32) | times[0].dwLowDateTime
            return created + 504911232000000000  # 1601-01-01 → 0001-01-01
        finally:
            k32.CloseHandle(handle)
    except (OSError, AttributeError, ImportError):
        return None


def _process_alive(pid: int) -> bool:
    try:
        import ctypes
        from ctypes import wintypes

        k32 = _kernel32()
        handle = k32.OpenProcess(0x1000, False, pid)
        if not handle:
            return ctypes.get_last_error() == 5  # нет доступа — процесс есть (чужой сеанс)
        try:
            code = wintypes.DWORD()
            return bool(k32.GetExitCodeProcess(handle, ctypes.byref(code))) and code.value == 259  # STILL_ACTIVE
        finally:
            k32.CloseHandle(handle)
    except (OSError, AttributeError, ImportError):
        return True  # не можем проверить — считаем живым и ждём


class BuildLock:
    """Замок .queue/build.lock — тот же протокол, что у tools/build_lock.ps1.

    Файл создаётся атомарно (O_EXCL) и содержит строки key=value: pid, pidStart (тики .NET времени
    старта процесса), taken, target, preset, owner. Живой чужой замок ждём до wait_seconds (код ошибки
    шага, не вечное ожидание); замок мёртвого процесса (PID нет или pidStart не совпал) убираем — так
    же поступает build_lock.ps1. Свой замок снимаем, только если в файле всё ещё наш PID.
    """

    def __init__(self, target: str, preset: str, wait_seconds: float) -> None:
        self.path = ROOT / ".queue" / "build.lock"
        self.target = target
        self.preset = preset
        self.wait_seconds = wait_seconds
        self.held = False

    def _read(self) -> dict[str, str]:
        info: dict[str, str] = {}
        try:
            for line in self.path.read_text(encoding="utf-8-sig", errors="replace").splitlines():
                m = re.match(r"^(\w+)=(.*)$", line.strip())
                if m:
                    info[m.group(1)] = m.group(2)
        except OSError:
            pass
        return info

    def _alive(self, info: dict[str, str]) -> bool:
        pid = info.get("pid", "")
        if not pid.isdigit():
            return False
        if not _process_alive(int(pid)):
            return False
        ticks = _process_start_ticks(int(pid))
        return not (ticks is not None and info.get("pidStart") and str(ticks) != info["pidStart"])

    def acquire(self) -> str:
        self.path.parent.mkdir(parents=True, exist_ok=True)
        deadline = time.monotonic() + self.wait_seconds
        last_note = 0.0
        while True:
            try:
                fd = os.open(self.path, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
            except FileExistsError:
                info = self._read()
                if not info:
                    time.sleep(0.5)  # файл только что создан, ещё не записан
                    info = self._read()
                holder = f"PID {info.get('pid', '?')} собирает {info.get('target', '?')} ({info.get('preset', '?')}) с {info.get('taken', '?')}"
                if not self._alive(info):
                    say(f"note замок .queue/build.lock от мёртвого процесса ({holder}) — снят")
                    try:
                        self.path.unlink()
                    except OSError:
                        pass
                    continue
                if time.monotonic() >= deadline:
                    return f"не дождался .queue/build.lock за {self.wait_seconds / 60:.0f} мин: {holder}"
                if time.monotonic() - last_note > 60:
                    say(f"wait сборка ждёт .queue/build.lock: {holder}")
                    last_note = time.monotonic()
                time.sleep(5)
                continue
            lines = [
                f"pid={os.getpid()}",
                f"pidStart={_process_start_ticks() or ''}",
                f"taken={dt.datetime.now().strftime('%Y-%m-%dT%H:%M:%S')}",
                f"target={self.target}",
                f"preset={self.preset}",
                "owner=verify.py",
            ]
            os.write(fd, ("\r\n".join(lines) + "\r\n").encode("ascii"))
            os.close(fd)
            self.held = True
            return ""

    def release(self) -> None:
        if self.held:
            if self._read().get("pid") == str(os.getpid()):
                try:
                    self.path.unlink()
                except OSError:
                    pass
            self.held = False


def step_configure(preset: str, build_dir: Path, env: dict[str, str]) -> Step:
    need, why = needs_configure(build_dir)
    if not need:
        return Step("configure", "skip", note="кэш свежий")
    started = time.monotonic()
    code, out = run_logged(["cmake", "--preset", preset], env, build_dir / "configure.log", timeout=7200)
    seconds = time.monotonic() - started
    if code == 0:
        return Step("configure", "ok", seconds, note=why)
    errors = [ln for ln in out.splitlines() if "CMake Error" in ln or "error" in ln.lower()][:40]
    return Step("configure", "FAIL", seconds, note=f"код {code}, лог build/{build_dir.name}/configure.log",
                output=errors + ["…"] + tail(out, 60))


def step_build(preset: str, build_dir: Path, env: dict[str, str], lock_wait: float) -> Step:
    lock = BuildLock("all", preset, lock_wait)
    problem = lock.acquire()
    if problem:
        return Step("build", "FAIL", note=problem)
    try:
        started = time.monotonic()
        code, out = run_logged(["cmake", "--build", "--preset", preset], env, build_dir / "build.log", timeout=7200)
        seconds = time.monotonic() - started
    finally:
        lock.release()
    if code == 0:
        return Step("build", "ok", seconds)
    with_warnings = "error C2220" in out
    errors = [ln for ln in out.splitlines()
              if BUILD_ERROR_RE.search(ln) or (with_warnings and BUILD_WARNING_RE.search(ln))]
    shown = errors[:80] + ([f"… ещё {len(errors) - 80} строк с ошибками"] if len(errors) > 80 else [])
    return Step("build", "FAIL", seconds, note=f"код {code}, лог build/{build_dir.name}/build.log",
                output=shown + ["--- хвост build.log (60 строк) ---"] + tail(out, 60))


def ctest_env(env: dict[str, str]) -> dict[str, str]:
    e = dict(env)
    e["QT_QPA_PLATFORM"] = "offscreen"
    e["QT_QPA_FONTDIR"] = "C:/Windows/Fonts"
    e["QT_FORCE_STDERR_LOGGING"] = "1"
    e.setdefault("OTTISK_INSTANCE", "test")
    qt_bin = Path(e.get("QT_ROOT") or DEFAULT_QT_ROOT) / "bin"
    if qt_bin.is_dir():
        e["PATH"] = f"{qt_bin}{os.pathsep}{e.get('PATH', '')}"
    return e


def step_ctest(name: str, build_dir: Path, args: list[str], junit: Path, env: dict[str, str]) -> Step:
    if not (build_dir / "CTestTestfile.cmake").is_file():
        return Step(name, "skip", note=f"нет build/{build_dir.name}/CTestTestfile.cmake — сборки ещё нет")
    cmd = ["ctest", "--test-dir", str(build_dir), *args, "--output-on-failure", "--no-tests=error",
           "--output-junit", str(junit)]
    started = time.monotonic()
    code, out = run_capture(cmd, ctest_env(env), timeout=7200)
    seconds = time.monotonic() - started
    m = re.search(r"(\d+)% tests passed, (\d+) tests failed out of (\d+)", out)
    summary = f"{m.group(3)} тестов, упало {m.group(2)}" if m else ""
    if code == 0:
        return Step(name, "ok", seconds, note=summary)
    lines = [ln for ln in out.splitlines() if not CTEST_PASSED_RE.match(ln)]
    return Step(name, "FAIL", seconds, note=summary or f"код {code}", output=lines[-400:])


def tool_env() -> dict[str, str]:
    """Окружение cmake/ctest/стражей: UTF-8 для Python и пути инструментов (как tools\\vcenv.cmd)."""
    env = dict(os.environ)
    env["PYTHONUTF8"] = "1"
    env["PYTHONIOENCODING"] = "utf-8"
    vcpkg = env.get("VCPKG_ROOT", "")
    if not vcpkg or "microsoft visual studio" in vcpkg.lower():
        # vcvars64 «Developer Prompt» ставит VCPKG_ROOT на vcpkg из Build Tools — у «АкроБати» свой клон.
        own = env.get("OTTISK_VCPKG_ROOT") or r"C:\Tools\vcpkg"
        if Path(own).is_dir():
            env["VCPKG_ROOT"] = own
    if not env.get("QT_ROOT") and Path(DEFAULT_QT_ROOT).is_dir():
        env["QT_ROOT"] = DEFAULT_QT_ROOT
    if not env.get("MUPDF_ROOT") and Path(DEFAULT_MUPDF_ROOT).is_dir():
        env["MUPDF_ROOT"] = DEFAULT_MUPDF_ROOT
    if not env.get("OTTISK_PYTHON") and Path(DEFAULT_PYTHON).is_file():
        env["OTTISK_PYTHON"] = DEFAULT_PYTHON
    return env


def anchor_labels(labels: str) -> str:
    """ctest -L — регулярное выражение по каждой метке теста: «unit|ui» совпало бы и с меткой build
    (тест установки build.install). Без ^/$ — оборачиваем в ^(…)$."""
    return labels if labels.startswith("^") or labels.endswith("$") else f"^({labels})$"


def wants_slow(labels: str) -> bool:
    """В метках есть render/engine/e2e/…: их тесты помечены и «slow», -LE slow их бы выкинул."""
    return any(tok.lower() in SLOW_CLASS_LABELS for tok in re.findall(r"[A-Za-z_]+", labels))


# ---------------------------------------------------------------- main


def main(argv: list[str]) -> int:
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser(description="Сборка, стражи и ctest «АкроБати» одной командой")
    ap.add_argument("--preset", default="msvc-debug", choices=sorted(PRESET_DIRS))
    ap.add_argument("--fast", action="store_true", help="без ctest")
    ap.add_argument("--full", action="store_true", help='ещё ctest -L "render|engine|compat|e2e"')
    ap.add_argument("--labels", default="unit|ui", help='метки ctest (по умолчанию "unit|ui")')
    ap.add_argument("--no-build", action="store_true", help="без configure и сборки")
    ap.add_argument("--no-configure", action="store_true", help="без configure (сборка по готовому кэшу)")
    ap.add_argument("--no-guards", action="store_true", help="без стражей")
    ap.add_argument("--lock-wait", type=float, default=30, help="сколько минут ждать .queue/build.lock")
    args = ap.parse_args(argv)

    need_msvc = not (args.fast and args.no_build)
    code = maybe_reexec(argv, need_msvc)
    if code is not None:
        return code

    env = tool_env()
    build_dir = ROOT / "build" / PRESET_DIRS[args.preset]
    python, py_note = python_for_guards()
    started_all = time.monotonic()
    say(f"verify: пресет {args.preset} → build/{build_dir.name}; метки «{args.labels}»"
        + (" + full" if args.full else "") + (" (fast: без ctest)" if args.fast else ""))
    if py_note:
        say(f"note {py_note}")

    steps: list[Step] = []

    def build_chain() -> list[Step]:
        chain: list[Step] = []
        if args.no_build:
            for nm in ("configure", "build"):
                s = Step(nm, "skip", note="--no-build")
                report(s)
                chain.append(s)
        else:
            if args.no_configure:
                s = Step("configure", "skip", note="--no-configure")
                if not (build_dir / "CMakeCache.txt").is_file():
                    s.note = f"--no-configure, а build/{build_dir.name}/CMakeCache.txt нет"
                report(s)
                chain.append(s)
            else:
                s = step_configure(args.preset, build_dir, env)
                report(s)
                chain.append(s)
                if s.status == "FAIL":
                    return chain
            if not (build_dir / "CMakeCache.txt").is_file():
                s = Step("build", "skip", note=f"нет build/{build_dir.name}/CMakeCache.txt — сначала configure")
                report(s)
                chain.append(s)
                return chain
            s = step_build(args.preset, build_dir, env, args.lock_wait * 60)
            report(s)
            chain.append(s)
            if s.status == "FAIL":
                return chain
        if args.fast:
            return chain
        slow = wants_slow(args.labels)
        # Повтор упавшего один раз: тесты интерфейса на время (ui.shots, ui.modes, отмена рендера) изредка падают
        # только под параллельной нагрузкой -j 12 и отдельно зелёные (журналы 1.4, 4.2, 5.5); настоящая поломка
        # падает оба раза. Так же — прогон --full ниже.
        labels = (["-L", anchor_labels(args.labels)] + ([] if slow else ["-LE", "^slow$"])
                  + ["--timeout", "60", "-j", "12", "--repeat", "until-pass:2"])
        s = step_ctest("ctest", build_dir, labels, build_dir / "ctest.xml", env)
        if slow:
            s.note = (s.note + "; " if s.note else "") + "без -LE slow (в метках есть медленные)"
        report(s)
        chain.append(s)
        if args.full:
            full = ["-L", anchor_labels("render|engine|compat|e2e"), "-j", "6", "--repeat", "until-pass:2"]
            s = step_ctest("ctest full", build_dir, full, build_dir / "ctest-full.xml", env)
            report(s)
            chain.append(s)
        return chain

    def guard_job(job: tuple[str, str, str]) -> Step:
        kind, name, script = job
        s = run_py_guard(name, script, python, env) if kind == "py" else run_ps_guard(name, script, env)
        report(s)
        return s

    jobs: list[tuple[str, str, str]] = []
    if not args.no_guards:
        jobs = [("py", n, s) for n, s in PY_GUARDS] + [("ps", n, s) for n, s in PS_GUARDS]
    with ThreadPoolExecutor(max_workers=1 + max(1, min(len(jobs), 6))) as pool:
        chain_future = pool.submit(build_chain)
        guard_steps = list(pool.map(guard_job, jobs))
        steps.extend(chain_future.result())
    steps.extend(guard_steps)

    failed = [s.name for s in steps if s.status == "FAIL"]
    skipped = [s.name for s in steps if s.status == "skip"]
    total = time.monotonic() - started_all
    if failed:
        say(f"verify: красный — упало: {', '.join(failed)} ({total:.0f} с)")
        return 1
    tail_note = f"; пропущено: {', '.join(skipped)}" if skipped else ""
    say(f"verify: зелёный ({total:.0f} с{tail_note})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
