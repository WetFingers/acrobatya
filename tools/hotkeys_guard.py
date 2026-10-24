"""Страж unit.guard.hotkeys (план 2.3; эталон 6.8): реестр команд окна ⊆ таблица горячих клавиш эталона.

Реестр — AcroBatya.exe --commands-list (JSON: id, title, keys в PortableText, when, singleKey, stage), таблица —
раздел «### 6.8 Горячие клавиши» в docs/design.md (парсер ниже: действия и клавиши строки делятся « / »,
однобуквенные — «`H` рука, …»). Сопоставление id → действие таблицы — словарь ACTIONS (сессия, которая
регистрирует команду с клавишей, дописывает её id сюда).

Проверки:
  * у каждой зарегистрированной команды с клавишами есть строка ACTIONS, и все её клавиши — из таблицы для этого
    действия (Ctrl++ ≡ Ctrl+=, Ctrl+Backtab ≡ Ctrl+Shift+Tab — одна физическая клавиша);
  * одна клавиша у двух команд — только при разных when и только если таблица сама даёт клавишу двум действиям
    (Ctrl+Shift+E — «Извлечь» и «стоп» чтения вслух);
  * --complete (сессия 9.2): каждое действие таблицы есть в реестре.

Коды: 0 — зелёный, 1 — красный, 77 — пропуск (exe не собран). Запуск:
  "%OTTISK_PYTHON%" -X utf8 -I tools/hotkeys_guard.py --root . [--exe build/debug/bin/AcroBatya.exe] [--complete]
"""

from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

# id команды → действие таблицы 6.8 (с префиксом строки «Страница: », «Панели: »… — как печатает --table).
ACTIONS: dict[str, str] = {
    "doc.open": "Открыть",
    "doc.close": "Закрыть",
    "doc.save": "Сохранить",
    "doc.saveAs": "Сохранить как",
    "doc.print": "Печать",
    "doc.properties": "Свойства",
    "app.quit": "Выход",
    "app.preferences": "Установки",
    "app.palette": "Палитра команд",
    "help.contents": "Справка",
    "edit.find": "Найти",
    "edit.findNext": "следующее",
    "edit.findPrevious": "предыдущее",
    "edit.advancedSearch": "Расширенный поиск",
    "edit.replace": "Найти и заменить",
    "view.zoomIn": "Масштаб +",
    "view.zoomOut": "−",
    "view.actualSize": "Реальный",
    "view.fitPage": "Страница",
    "view.fitWidth": "Ширина",
    "view.fitVisible": "Видимое",
    "view.reflow": "Перекомпоновка",
    "view.firstPage": "Страница: первая",
    "view.lastPage": "Страница: последняя",
    "view.nextPage": "Страница: следующая",
    "view.previousPage": "Страница: предыдущая",
    "view.goToPage": "Страница: перейти",
    "view.back": "Предыдущий",
    "view.forward": "следующий вид",
    "view.rotateCw": "Повернуть вид по",
    "view.rotateCcw": "против часовой",
    "view.readMode": "Режим чтения",
    "view.fullScreen": "Полный экран",
    "view.exitMode": "выход",
    "view.navPane": "Панели: навигация",
    "view.toolPanel": "Панели: инструмента",
    "view.allPanels": "Панели: все панели",
    "view.menuBar": "Панели: строка меню",
    "window.nextTab": "Вкладки: следующая",
    "window.previousTab": "Вкладки: предыдущая",
    "edit.undo": "Отмена",
    "edit.redo": "повтор",
    "edit.cut": "вырезать",
    "edit.copy": "копировать",
    "edit.paste": "вставить",
    "edit.selectAll": "выделить всё",
    "edit.delete": "удалить",
    "tool.hand": "рука",
    "tool.select": "выделение",
    "tool.marqueeZoom": "масштаб рамкой",
    "tool.zoomCycle": "цикл зума",
    "tool.snapshot": "снимок",
    "tool.editText": "редактировать текст",
    "tool.note": "записка",
    "tool.highlight": "выделить текст",
    "tool.draw": "рисование",
    "comment.note": "Комментарии: записка",
    "comment.toggleAll": "Комментарии: показать-скрыть все",
}

# Одна физическая клавиша — разные имена QKeySequence.
ALIASES = {"Ctrl++": "Ctrl+=", "Ctrl+Shift++": "Ctrl+Shift+=", "Ctrl+Backtab": "Ctrl+Shift+Tab"}
# Имена клавиш таблицы → PortableText QKeySequence.
TABLE_NAMES = {"PgDn": "PgDown", "PgUp": "PgUp", "→": "Right", "←": "Left", "↑": "Up", "↓": "Down"}
NOT_KEYS = ("колесо",)  # «Ctrl+колесо» — жест, не клавиша


def norm_registry(key: str) -> str:
    return ALIASES.get(key, key)


def norm_table(key: str) -> str | None:
    """«Ctrl+Shift+=», «PgDn», «Alt+←» → PortableText QKeySequence; жест (колесо) — None."""
    if any(word in key for word in NOT_KEYS):
        return None
    mods, _, last = key.rpartition("+")
    last = TABLE_NAMES.get(last, last)
    return norm_registry(f"{mods}+{last}" if mods else last)


def table_section(design: Path) -> list[str]:
    lines = design.read_text(encoding="utf-8").splitlines()
    out: list[str] = []
    inside = False
    for line in lines:
        if line.startswith("### 6.8 "):
            inside = True
            continue
        if inside and line.startswith("#"):
            break
        if inside and line.startswith("|"):
            out.append(line)
    return out


def parse_table(design: Path) -> tuple[dict[str, list[str]], list[str]]:
    """Действие → нормализованные клавиши; ошибки разбора."""
    table: dict[str, list[str]] = {}
    errors: list[str] = []
    rows = table_section(design)
    for line in rows[2:]:  # заголовок и разделитель
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        if len(cells) != 2:
            errors.append(f"design.md 6.8: строка не из двух ячеек: {line}")
            continue
        left, right = cells
        if left.startswith("Инструменты (однобуквенные"):
            prefix = ""
            for part in re.split(r"[,;]", right):
                if "Подготовить форму" in part:
                    prefix = "Подготовить форму: "
                m = re.search(r"`([^`]+)`\s+(.+?)\s*$", part)
                if m:
                    table[prefix + m.group(2).strip()] = [norm_table(m.group(1))]
            continue
        labels = [s.strip() for s in left.split(" / ")]
        groups = [s.strip() for s in right.split(" / ")]
        if len(labels) != len(groups):
            errors.append(f"design.md 6.8: действий {len(labels)}, клавиш {len(groups)}: {left}")
            continue
        prefix = ""
        if ":" in labels[0]:
            prefix = labels[0].split(":", 1)[0] + ": "
            labels[0] = labels[0].split(":", 1)[1].strip()
        for label, group in zip(labels, groups):
            keys = [k for k in (norm_table(t) for t in re.findall(r"`([^`]+)`", group)) if k]
            table[prefix + label] = keys
    return table, errors


def find_exe(root: Path, explicit: str | None) -> Path | None:
    if explicit:
        p = Path(explicit)
        return p if p.is_file() else None
    found = [root / "build" / d / "bin" / "AcroBatya.exe" for d in ("debug", "release")]
    found = [p for p in found if p.is_file()]
    return max(found, key=lambda p: p.stat().st_mtime) if found else None


def registry(exe: Path) -> list[dict]:
    env = dict(os.environ)
    env["QT_QPA_PLATFORM"] = "offscreen"
    env["OTTISK_INSTANCE"] = "test"
    env["OTTISK_DATA_DIR"] = tempfile.mkdtemp(prefix="ottisk-hotkeys-")  # переназначения человека не читаются
    qt = env.get("QT_ROOT", "C:/Qt/6.12.0/msvc2022_64")
    env["PATH"] = str(Path(qt) / "bin") + os.pathsep + env.get("PATH", "")
    out = subprocess.run([str(exe), "--commands-list"], capture_output=True, env=env, timeout=120, check=False)
    text = out.stdout.decode("utf-8", errors="replace")
    for line in reversed(text.splitlines()):
        if line.startswith("[{") or line.strip() == "[]":
            return json.loads(line)
    raise RuntimeError(f"--commands-list: нет JSON (код {out.returncode}): {text[-400:]} {out.stderr[-400:]!r}")


def check(table: dict[str, list[str]], commands: list[dict], complete: bool) -> list[str]:
    bad: list[str] = []
    for label in sorted(set(ACTIONS.values())):
        if label not in table:
            bad.append(f"hotkeys_guard.py ACTIONS: действия «{label}» нет в таблице 6.8")
    seen: dict[str, int] = {}
    for keys in table.values():
        for k in keys:
            seen[k] = seen.get(k, 0) + 1
    allowed_conflicts = {k for k, n in seen.items() if n > 1}

    owners: dict[str, list[tuple[str, str]]] = {}
    covered: set[str] = set()
    for c in commands:
        keys = list(dict.fromkeys(norm_registry(k) for k in c.get("keys", [])))  # псевдонимы — одна клавиша
        if not keys:
            continue
        cid = c["id"]
        label = ACTIONS.get(cid)
        if label is None:
            bad.append(f"{cid}: клавиши {', '.join(keys)} — команды нет в ACTIONS (tools/hotkeys_guard.py)")
            continue
        allowed = table.get(label, [])
        for k in keys:
            if k not in allowed:
                bad.append(f"{cid}: клавиша {k} не из таблицы 6.8 для «{label}» (там: {', '.join(allowed) or '—'})")
            owners.setdefault(k, []).append((cid, c.get("when", "")))
        covered.add(label)
    for k, who in owners.items():
        if len(who) < 2:
            continue
        whens = [w for _, w in who]
        if len(set(whens)) != len(whens) or k not in allowed_conflicts:
            bad.append(f"конфликт клавиши {k}: {', '.join(f'{i} [{w or 'окно'}]' for i, w in who)}")
    if complete:
        for label, keys in sorted(table.items()):
            if keys and label not in covered:
                bad.append(f"--complete: в реестре нет «{label}» ({', '.join(keys)})")
    return bad


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--root", default=".")
    ap.add_argument("--exe")
    ap.add_argument("--complete", action="store_true", help="каждое действие таблицы — в реестре (9.2)")
    ap.add_argument("--table", action="store_true", help="напечатать разобранную таблицу 6.8")
    args = ap.parse_args()
    root = Path(args.root).resolve()
    table, errors = parse_table(root / "docs" / "design.md")
    if args.table:
        for label, keys in table.items():
            print(f"{label}: {', '.join(keys)}")
        return 0
    if not table:
        print("hotkeys: таблица 6.8 не найдена в docs/design.md")
        return 1
    exe = find_exe(root, args.exe)
    if exe is None:
        print("hotkeys: SKIP — AcroBatya.exe не собран (build/debug/bin)")
        return 77
    try:
        commands = registry(exe)
    except (RuntimeError, subprocess.TimeoutExpired, json.JSONDecodeError) as e:
        print(f"hotkeys: {e}")
        return 1
    bad = errors + check(table, commands, args.complete)
    for line in bad:
        print(line)
    with_keys = sum(1 for c in commands if c.get("keys"))
    if bad:
        print(f"hotkeys: красный — нарушений {len(bad)} (команд {len(commands)}, с клавишами {with_keys})")
        return 1
    print(f"hotkeys: зелёный — команд {len(commands)}, с клавишами {with_keys}, таблица 6.8: {len(table)} действий")
    return 0


if __name__ == "__main__":
    sys.exit(main())
