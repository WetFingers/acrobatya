"""Добавить UTF-8 BOM файлам .ps1/.iss (Windows PowerShell 5.1 и Inno Setup читают кириллицу только с BOM).

    py -3.14 tools/add_bom.py <файл> [<файл> …]     # без аргументов — все *.ps1 и *.iss проекта
"""
from __future__ import annotations

import sys
from pathlib import Path

BOM = b"\xef\xbb\xbf"
ROOT = Path(__file__).resolve().parent.parent


def main(argv: list[str]) -> int:
    files = [Path(a) for a in argv] or [
        p for p in ROOT.rglob("*") if p.suffix.lower() in {".ps1", ".iss"} and "build" not in p.parts
    ]
    for p in files:
        data = p.read_bytes()
        if not data.startswith(BOM):
            p.write_bytes(BOM + data)
            print(f"BOM: {p}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
