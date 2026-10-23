"""Страж формата (ctest unit.guard.format, эталон 9.5).

    "%OTTISK_PYTHON%" -X utf8 -I tools/format_guard.py --root <репозиторий> [--fix] [файлы…]

clang-format --dry-run --Werror по *.h/*.hpp/*.cpp/*.cc/*.inl в src/, tests/, fuzz/, tools/imgcmp/
и tools/render_refs.cpp (стиль — .clang-format в корне). clang-format ищется в PATH, затем в
Build Tools 2022 (VC\\Tools\\Llvm\\x64\\bin); нужна версия 19.1.x (другая версия форматирует иначе).
--fix — переформатировать на месте (clang-format -i) вместо проверки.
Код 0 — зелёный, 1 — красный, 77 — clang-format 19.1.x не найден (ctest SKIP_RETURN_CODE 77).
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

REQUIRED_MAJOR_MINOR = (19, 1)
CPP_EXT = {".h", ".hpp", ".hxx", ".cpp", ".cc", ".cxx", ".inl"}
SCAN_DIRS = ("src", "tests", "fuzz", "tools/imgcmp")
SCAN_FILES = ("tools/render_refs.cpp",)
SKIP_PARTS = {"fixtures", "data", "corpus", "expected"}
CHUNK = 40
VS_EDITIONS = ("BuildTools", "Community", "Professional", "Enterprise")


def candidates() -> list[Path]:
    found: list[Path] = []
    which = shutil.which("clang-format")
    if which:
        found.append(Path(which))
    vcinstall = os.environ.get("VCINSTALLDIR")
    if vcinstall:
        found.append(Path(vcinstall) / "Tools" / "Llvm" / "x64" / "bin" / "clang-format.exe")
    for base in (os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"), os.environ.get("ProgramFiles", r"C:\Program Files")):
        for edition in VS_EDITIONS:
            found.append(
                Path(base) / "Microsoft Visual Studio" / "2022" / edition / "VC" / "Tools" / "Llvm" / "x64" / "bin" / "clang-format.exe"
            )
    unique: list[Path] = []
    for p in found:
        if p.is_file() and all(not p.samefile(q) for q in unique):
            unique.append(p)
    return unique


def version_of(exe: Path) -> tuple[int, int, int] | None:
    try:
        out = subprocess.run([str(exe), "--version"], capture_output=True, text=True, timeout=30).stdout
    except (OSError, subprocess.SubprocessError):
        return None
    m = re.search(r"version (\d+)\.(\d+)\.(\d+)", out)
    return (int(m.group(1)), int(m.group(2)), int(m.group(3))) if m else None


def find_clang_format() -> tuple[Path | None, list[str]]:
    seen: list[str] = []
    for exe in candidates():
        ver = version_of(exe)
        seen.append(f"{exe} → {'.'.join(map(str, ver)) if ver else '?'}")
        if ver and ver[:2] == REQUIRED_MAJOR_MINOR:
            return exe, seen
    return None, seen


def collect(root: Path) -> list[Path]:
    files: list[Path] = []
    for sub in SCAN_DIRS:
        base = root / sub
        if not base.is_dir():
            continue
        for dirpath, dirnames, filenames in os.walk(base):
            rel_parts = Path(dirpath).relative_to(root).parts
            dirnames[:] = sorted(d for d in dirnames if not d.startswith(".") and d not in SKIP_PARTS)
            if any(part in SKIP_PARTS for part in rel_parts):
                continue
            files.extend(Path(dirpath) / n for n in sorted(filenames) if Path(n).suffix.lower() in CPP_EXT)
    for rel in SCAN_FILES:
        p = root / rel
        if p.is_file():
            files.append(p)
    return files


def main(argv: list[str]) -> int:
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser(description="clang-format --dry-run --Werror по исходникам C++")
    ap.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    ap.add_argument("--fix", action="store_true", help="переформатировать на месте")
    ap.add_argument("files", nargs="*", type=Path, help="только эти файлы (по умолчанию — все)")
    args = ap.parse_args(argv)
    root = args.root.resolve()

    exe, seen = find_clang_format()
    if exe is None:
        print("format: пропуск — clang-format 19.1.x не найден (tools/vcenv.cmd кладёт его в PATH)")
        for line in seen:
            print(f"  найден: {line}")
        return 77

    files = [p.resolve() for p in args.files] if args.files else collect(root)
    if not files:
        print("format: файлов C++ нет — проверять нечего")
        return 0
    if not (root / ".clang-format").is_file():
        print(".clang-format:1: файла нет — стиль не задан")
        return 1

    mode = ["-i"] if args.fix else ["--dry-run", "--Werror"]
    bad_lines: dict[str, list[int]] = {}
    report: list[str] = []
    failed_runs = 0
    for i in range(0, len(files), CHUNK):
        chunk = files[i : i + CHUNK]
        proc = subprocess.run(
            [str(exe), *mode, "--style=file", *map(str, chunk)],
            cwd=root,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
        )
        out = (proc.stdout or "") + (proc.stderr or "")
        if proc.returncode != 0:
            failed_runs += 1
        for line in out.splitlines():
            m = re.match(r"^(.*?):(\d+):(\d+): (?:error|warning): (.*)$", line)
            if m:
                try:
                    rel = Path(m.group(1)).resolve().relative_to(root).as_posix()
                except ValueError:
                    rel = m.group(1)
                lines = bad_lines.setdefault(rel, [])
                if int(m.group(2)) not in lines:
                    lines.append(int(m.group(2)))
            elif re.match(r"^(?:error|Error|clang-format|YAML|Configuration file|.*Invalid argument)", line):
                report.append(line)  # не нарушение стиля, а ошибка самого clang-format (конфиг, файл)

    ver = version_of(exe)
    ver_s = ".".join(map(str, ver)) if ver else "?"
    if args.fix:
        print(f"format: переформатировано файлов {len(files)} (clang-format {ver_s})")
        return 0 if failed_runs == 0 else 1
    if failed_runs or bad_lines:
        for rel, lines in bad_lines.items():
            more = f" и ещё {len(lines) - 12}" if len(lines) > 12 else ""
            print(f"{rel}:{lines[0]}: не по .clang-format, строки {', '.join(map(str, lines[:12]))}{more}")
        if report:
            print("\n".join(report[:50]))
        print(
            f"format: красный — не по стилю файлов {len(bad_lines)} из {len(files)} "
            f"(clang-format {ver_s}; исправить: tools/format_guard.py --fix)"
        )
        return 1
    print(f"format: зелёный (файлов {len(files)}, clang-format {ver_s})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
