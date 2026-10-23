"""Страж объёма (ctest unit.guard.scope): docs/scope.json ↔ docs/plan.md ↔ docs/features.json.

    "%OTTISK_PYTHON%" -X utf8 -I tools/scope_check.py --root <репозиторий> [--list]

ID берутся ТОЛЬКО из строк пунктов плана (`- [ ]`, `- [x]`, `- [~]`) и строк с «**Закрывает:**»;
раздел «## Правила работы» не читается (там ID того, что вне объёма). Сокращения раскрываются:
«view-01, 03 (кеш), 04» → view-01, view-03, view-04 (префикс действует до следующего «домен-NN»),
«edit-06/07» → edit-06, edit-07. Диапазоны «…»/«–» НЕ раскрываются: в плане «cloud-15…43 только
упоминаются» значит «перечисленные выше от 15 до 43», а не все номера подряд — берётся только первый ID.

Проверки (эталон 1.2, 9.5): каждый ID плана есть в одном из списков scope.json; каждый ID списков
упомянут хотя бы в одном пункте; все ID списков есть в docs/features.json; списки не пересекаются и
без повторов; размеры full/partial/nice/out = 186/8/28/30.
Код 0 — зелёный, 1 — красный (нарушения печатаются строками «файл:строка: что»).
"""

from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

LISTS = ("full", "partial", "nice", "out")
EXPECTED_COUNTS = {"full": 186, "partial": 8, "nice": 28, "out": 30}
DOMAINS = ("view", "comment", "edit", "create", "forms", "sign", "security", "ocr", "pro", "cloud")
SKIP_SECTIONS = ("Правила работы",)

ID_RE = re.compile(r"(?<![\w-])(" + "|".join(DOMAINS) + r")-(\d{2})(?!\w)")
ITEM_RE = re.compile(r"^\s*[-*] \[[ xX~]\]")
CLOSES_MARK = "**Закрывает:**"
# Продолжение списка после ID: «, 03», «/07». Число не должно быть частью другого числа, версии,
# диапазона или величины («, 12.5», «, 300 dpi», «, 15…43»).
CONT_RE = re.compile(
    r"[ \t]*(?:,|/)[ \t]*(\d{2})(?![\w\-–…]|\.\d|[ \t]*(?:стр\w*|dpi|мс|КБ|МБ|ГБ|%|×)(?!\w))"
)


def _skip_paren(text: str, i: int) -> int:
    """text[i] == '(' → индекс после парной скобки (вложенные скобки учитываются)."""
    depth = 0
    for j in range(i, len(text)):
        if text[j] == "(":
            depth += 1
        elif text[j] == ")":
            depth -= 1
            if depth == 0:
                return j + 1
    return len(text)


def ids_in_text(text: str) -> list[str]:
    """Все ID каталога в строке с раскрытыми сокращениями, в порядке появления (с повторами)."""
    found: list[str] = []
    for m in ID_RE.finditer(text):
        domain = m.group(1)
        found.append(f"{domain}-{m.group(2)}")
        pos = m.end()
        while True:
            k = pos
            while k < len(text) and text[k] in " \t":
                k += 1
            if k < len(text) and text[k] == "(":
                k = _skip_paren(text, k)
            cont = CONT_RE.match(text, k)
            if not cont:
                break
            found.append(f"{domain}-{cont.group(1)}")
            pos = cont.end()
    return found


def plan_ids(plan_text: str) -> dict[str, list[int]]:
    """ID → номера строк plan.md, где он упомянут (только пункты и строки «Закрывает»)."""
    result: dict[str, list[int]] = {}
    skipping = False
    in_fence = False
    for lineno, line in enumerate(plan_text.splitlines(), 1):
        if line.lstrip().startswith("```"):
            in_fence = not in_fence
            continue
        if in_fence:
            continue
        if line.startswith("## "):
            title = line[3:].strip()
            skipping = any(title.startswith(s) for s in SKIP_SECTIONS)
            continue
        if skipping:
            continue
        if not (ITEM_RE.match(line) or CLOSES_MARK in line):
            continue
        for fid in ids_in_text(line):
            lines = result.setdefault(fid, [])
            if lineno not in lines:
                lines.append(lineno)
    return result


def _line_of(text: str, needle: str) -> int:
    idx = text.find(needle)
    return text.count("\n", 0, idx) + 1 if idx >= 0 else 1


def check(root: Path, show_list: bool = False) -> int:
    plan_path = root / "docs" / "plan.md"
    scope_path = root / "docs" / "scope.json"
    features_path = root / "docs" / "features.json"
    errors: list[str] = []
    for p in (plan_path, scope_path, features_path):
        if not p.is_file():
            errors.append(f"{p.relative_to(root).as_posix()}:1: файла нет")
    if errors:
        print("\n".join(errors))
        return 1

    plan_text = plan_path.read_text(encoding="utf-8")
    scope_text = scope_path.read_text(encoding="utf-8")
    try:
        scope = json.loads(scope_text)
    except json.JSONDecodeError as exc:
        print(f"docs/scope.json:{exc.lineno}: не JSON: {exc.msg}")
        return 1
    try:
        catalog = {item["id"] for item in json.loads(features_path.read_text(encoding="utf-8"))}
    except (json.JSONDecodeError, KeyError, TypeError) as exc:
        print(f"docs/features.json:1: не читается как список {{id: …}}: {exc}")
        return 1

    owner: dict[str, str] = {}
    for name in LISTS:
        items = scope.get(name)
        if not isinstance(items, list) or not all(isinstance(x, str) for x in items):
            errors.append(f"docs/scope.json:1: нет списка строк «{name}»")
            continue
        seen: set[str] = set()
        for fid in items:
            line = _line_of(scope_text, '"' + fid + '"')
            if fid in seen:
                errors.append(f"docs/scope.json:{line}: {fid} повторяется в «{name}»")
            seen.add(fid)
            if fid in owner and owner[fid] != name:
                errors.append(f"docs/scope.json:{line}: {fid} сразу в «{owner[fid]}» и «{name}»")
            owner.setdefault(fid, name)
            if fid not in catalog:
                errors.append(f"docs/scope.json:{line}: {fid} ({name}) нет в docs/features.json")
        if len(items) != EXPECTED_COUNTS[name]:
            line = _line_of(scope_text, '"' + name + '"')
            errors.append(
                f"docs/scope.json:{line}: в «{name}» {len(items)} ID, "
                f"по эталону 1.2 — {EXPECTED_COUNTS[name]}"
            )

    in_plan = plan_ids(plan_text)
    for fid in sorted(in_plan):
        if fid not in owner:
            where = in_plan[fid][0]
            extra = "" if fid in catalog else " (и нет в docs/features.json)"
            errors.append(f"docs/plan.md:{where}: {fid} нет ни в одном списке docs/scope.json{extra}")
    for fid in sorted(owner):
        if fid not in in_plan:
            line = _line_of(scope_text, '"' + fid + '"')
            errors.append(
                f"docs/scope.json:{line}: {fid} ({owner[fid]}) не упомянут ни в одном пункте "
                "и ни в одной строке «Закрывает» docs/plan.md"
            )

    if show_list:
        for fid in sorted(in_plan):
            print(f"{fid}\t{owner.get(fid, '-')}\t{','.join(map(str, in_plan[fid]))}")
    counts = ", ".join(f"{n} {len(scope.get(n) or [])}" for n in LISTS)
    print(f"ID плана: {len(in_plan)}, scope.json: {len(owner)} ({counts})")
    if errors:
        print("\n".join(errors))
        print(f"scope: красный, нарушений {len(errors)}")
        return 1
    print("scope: зелёный")
    return 0


def main(argv: list[str]) -> int:
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser(description="Сверка docs/scope.json с планом и каталогом")
    ap.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    ap.add_argument("--list", action="store_true", help="напечатать ID плана: id, список, строки")
    args = ap.parse_args(argv)
    return check(args.root.resolve(), args.list)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
