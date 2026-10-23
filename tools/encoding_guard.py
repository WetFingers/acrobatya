"""Страж кодировок (ctest unit.guard.encoding, эталон 5.6).

    "%OTTISK_PYTHON%" -X utf8 -I tools/encoding_guard.py --root <репозиторий>

  * *.ps1, *.psm1, *.psd1, *.iss — UTF-8 с BOM (Windows PowerShell 5.1 и Inno Setup без BOM читают
    кириллицу в ANSI-кодировке и ломают разбор) и корректный UTF-8 после BOM;
  * *.cmd, *.bat — только ASCII (cmd.exe читает их в OEM-кодировке).
Не проверяются каталоги build/, dist/, .queue/, .git/ и прочие скрытые, кроме .claude/.
Исправить BOM: py -3.14 tools/add_bom.py <файл>.
Код 0 — зелёный, 1 — красный (строки «файл:строка: что»).
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

BOM = b"\xef\xbb\xbf"
BOM_EXT = {".ps1", ".psm1", ".psd1", ".iss"}
ASCII_EXT = {".cmd", ".bat"}
SKIP_DIRS = {"build", "dist", ".queue", ".git", "node_modules", "__pycache__"}
KEEP_HIDDEN = {".claude"}


def iter_files(root: Path):
    for dirpath, dirnames, filenames in os.walk(root):
        rel_dir = Path(dirpath).relative_to(root)
        dirnames[:] = sorted(
            d
            for d in dirnames
            if not (len(rel_dir.parts) == 0 and d in SKIP_DIRS)
            and d not in {".git", "node_modules", "__pycache__"}
            and (not d.startswith(".") or d in KEEP_HIDDEN)
        )
        for name in sorted(filenames):
            ext = Path(name).suffix.lower()
            if ext in BOM_EXT or ext in ASCII_EXT:
                yield Path(dirpath) / name, ext


def check_file(path: Path, rel: str, ext: str) -> list[str]:
    data = path.read_bytes()
    if ext in BOM_EXT:
        if not data.startswith(BOM):
            return [f"{rel}:1: нет UTF-8 BOM (py -3.14 tools/add_bom.py {rel})"]
        try:
            data[len(BOM):].decode("utf-8")
        except UnicodeDecodeError as exc:
            line = data[: exc.start].count(b"\n") + 1
            return [f"{rel}:{line}: не UTF-8 после BOM (байт 0x{data[exc.start]:02x})"]
        return []
    for lineno, raw in enumerate(data.split(b"\n"), 1):
        bad = next((b for b in raw if b > 0x7F), None)
        if bad is not None:
            return [f"{rel}:{lineno}: не ASCII (байт 0x{bad:02x}) — .cmd/.bat пишутся только ASCII"]
    return []


def main(argv: list[str]) -> int:
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser(description="BOM у .ps1/.iss, ASCII у .cmd/.bat")
    ap.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    args = ap.parse_args(argv)
    root = args.root.resolve()
    errors: list[str] = []
    count = 0
    for path, ext in iter_files(root):
        count += 1
        errors.extend(check_file(path, path.relative_to(root).as_posix(), ext))
    if errors:
        print("\n".join(errors))
        print(f"encoding: красный, нарушений {len(errors)} (файлов {count})")
        return 1
    print(f"encoding: зелёный (файлов {count})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
