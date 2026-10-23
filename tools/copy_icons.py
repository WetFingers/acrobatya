"""Иконки Lucide (ISC) для «АкроБати» (эталон 5.4, 6.5): копия только нужных SVG по списку.

    "%OTTISK_PYTHON%" -X utf8 -I tools/copy_icons.py [--src <каталог Lucide>] [--list <файл>…] [--check]

Список — одно имя на строку, без .svg; «#» — комментарий. По умолчанию — объединение
assets/icons/icons.txt (базовые иконки этапа 0) и src/ui/theme/icon_names.txt (список `Icons`, сессия 2.1,
эталон 6.5), каких из них есть; --list — только указанные файлы. Источник —
--src, иначе OTTISK_ICONS_SRC, иначе C:/Users/WetFingers/Claude/Projects/bny-studio/app/assets/icons
(lucide-static 1.50, рядом LICENSE-lucide.txt → assets/icons/LICENSE). Копирует только изменившиеся.
--check — ничего не копирует: каждый SVG из списка есть в assets/icons, лишних SVG нет, LICENSE есть
(для стража unit.guard.icons). Код 0 — всё на месте, 1 — нет иконок в источнике / расхождение.
"""

from __future__ import annotations

import argparse
import os
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_SRC = Path("C:/Users/WetFingers/Claude/Projects/bny-studio/app/assets/icons")
LICENSE_NAMES = ("LICENSE-lucide.txt", "LICENSE", "LICENSE.txt")
DEFAULT_LISTS = ("assets/icons/icons.txt", "src/ui/theme/icon_names.txt")


def read_list(path: Path) -> list[str]:
    names: list[str] = []
    for raw in path.read_text(encoding="utf-8-sig").splitlines():
        line = raw.split("#", 1)[0].strip()
        if line:
            name = line.removesuffix(".svg")
            if name not in names:
                names.append(name)
    return names


def copy_if_changed(src: Path, dst: Path) -> bool:
    if dst.is_file() and dst.read_bytes() == src.read_bytes():
        return False
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(src, dst)
    return True


def main(argv: list[str]) -> int:
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser(description="Скопировать нужные иконки Lucide в assets/icons")
    ap.add_argument("--root", type=Path, default=ROOT)
    ap.add_argument("--src", type=Path, default=None)
    ap.add_argument("--list", type=Path, action="append", help="файл со списком имён (можно несколько)")
    ap.add_argument("--check", action="store_true", help="только проверить assets/icons по списку")
    args = ap.parse_args(argv)
    root = args.root.resolve()
    icons_dir = root / "assets" / "icons"
    if args.list:
        list_paths = [p if p.is_absolute() else root / p for p in args.list]
        absent = [p for p in list_paths if not p.is_file()]
        if absent:
            print("\n".join(f"{p}:1: файла нет" for p in absent))
            return 1
    else:
        list_paths = [root / rel for rel in DEFAULT_LISTS if (root / rel).is_file()]
        if not list_paths:
            print(f"{DEFAULT_LISTS[0]}:1: файла нет (и {DEFAULT_LISTS[1]} тоже)")
            return 1
    names: list[str] = []
    for list_path in list_paths:
        names += [n for n in read_list(list_path) if n not in names]

    if args.check:
        problems = [f"assets/icons/{n}.svg:1: нет файла (tools/copy_icons.py)" for n in names if not (icons_dir / f"{n}.svg").is_file()]
        extra = sorted(p.name for p in icons_dir.glob("*.svg") if p.stem not in names)
        shown = ", ".join(p.relative_to(root).as_posix() if p.is_relative_to(root) else str(p) for p in list_paths)
        problems += [f"assets/icons/{n}:1: нет в списке ({shown}) — добавить в список или удалить" for n in extra]
        if not (icons_dir / "LICENSE").is_file():
            problems.append("assets/icons/LICENSE:1: нет лицензии Lucide (ISC)")
        if problems:
            print("\n".join(problems))
            print(f"icons: красный, расхождений {len(problems)}")
            return 1
        print(f"icons: зелёный ({len(names)} иконок)")
        return 0

    src = args.src or Path(os.environ.get("OTTISK_ICONS_SRC") or DEFAULT_SRC)
    if not src.is_dir():
        print(f"copy_icons: нет каталога источника {src}")
        return 1
    missing = [n for n in names if not (src / f"{n}.svg").is_file()]
    copied = sum(copy_if_changed(src / f"{n}.svg", icons_dir / f"{n}.svg") for n in names if n not in missing)
    license_src = next((src / n for n in LICENSE_NAMES if (src / n).is_file()), None)
    if license_src is None:
        print(f"copy_icons: в {src} нет лицензии Lucide ({', '.join(LICENSE_NAMES)})")
        return 1
    lic = copy_if_changed(license_src, icons_dir / "LICENSE")
    print(f"иконок в списке {len(names)}, скопировано/обновлено {copied}, LICENSE {'обновлён' if lic else 'без изменений'}")
    if missing:
        print(f"нет в источнике ({len(missing)}): {', '.join(missing)}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
