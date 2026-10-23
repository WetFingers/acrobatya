"""Страж реестра (ctest unit.guard.no_registry, эталон 4.7, 10.3–10.4).

    "%OTTISK_PYTHON%" -X utf8 -I tools/no_registry_guard.py --root <репозиторий>

Программа хранит настройки в INI/JSON (%LOCALAPPDATA%\\AcroBatya), а ассоциацию .pdf пишет установщик.
В src/** запрещены (вне комментариев): RegSetValue*, RegSetKeyValue*, RegCreateKey*, RegDeleteKey*,
RegDeleteTree*, RegDeleteValue*, SHSetValue*, SHDeleteKey*, SHDeleteValue*, SHRegSetUSValue*,
SHRegWriteUSValue*, QSettings::NativeFormat, QSettings::Registry32Format/Registry64Format и любое «HKEY_».
Ещё: QSettings без IniFormat в файле допустим, только если где-то в src вызван
QSettings::setDefaultFormat(QSettings::IniFormat) (эталон 4.7) — иначе формат по умолчанию на Windows — реестр.
Исключения — файлы из ALLOWED ниже (glob от корня репозитория); сейчас пусто, позже —
src/services/shell_register.* (проверка ассоциации, HKCU\\Software\\Classes\\.pdf\\UserChoice).
Код 0 — зелёный, 1 — красный (строки «файл:строка: что»). Каталога src нет — зелёный.
"""

from __future__ import annotations

import argparse
import bisect
import fnmatch
import os
import re
import sys
from pathlib import Path

# Файлы, которым реестр разрешён (glob от корня, прямые слэши). Добавлять — только с доводом в журнале.
ALLOWED: list[str] = []

CPP_EXT = {".h", ".hpp", ".hxx", ".cpp", ".cc", ".cxx", ".inl", ".c", ".rc"}
FORBIDDEN_RE = re.compile(
    r"\b(Reg(?:SetValue|SetKeyValue|CreateKey|DeleteKey|DeleteTree|DeleteValue)\w*"
    r"|SH(?:SetValue|DeleteKey|DeleteEmptyKey|DeleteValue|RegSetUSValue|RegWriteUSValue)\w*"
    r"|QSettings\s*::\s*(?:NativeFormat|Registry32Format|Registry64Format)"
    r"|HKEY_\w*)"
)

DEFAULT_INI_RE = re.compile(r"setDefaultFormat\s*\(\s*QSettings\s*::\s*IniFormat\s*\)")
QSETTINGS_RE = re.compile(r"\bQSettings\b")
INCLUDE_LINE_RE = re.compile(r"^[ \t]*#[ \t]*include\b.*$", re.MULTILINE)

# ---- лексер C++: комментарии, строки, символы (копия из i18n_guard.py — правьте все четыре) ----
_IDENT_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")
_NUMBER_RE = re.compile(r"[0-9][0-9A-Za-z_'.]*")
_RAW_RE = re.compile(r'"([^()\\ \t\r\n]{0,16})\(')
_STR_PREFIX = {"u8", "u", "U", "L"}
_RAW_PREFIX = {"R", "u8R", "uR", "UR", "LR"}


def _quoted_end(text: str, i: int, quote: str) -> int:
    j = i + 1
    n = len(text)
    while j < n:
        ch = text[j]
        if ch == "\\":
            j += 2
            continue
        if ch == quote:
            return j + 1
        if ch == "\n":
            return j
        j += 1
    return n


def lex(text: str) -> list[tuple[str, int, int]]:
    """Отрезки (вид, начало, конец): 'comment', 'string', 'char'. Остальное — код."""
    spans: list[tuple[str, int, int]] = []
    i, n = 0, len(text)
    while i < n:
        ch = text[i]
        if ch == "/" and text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            spans.append(("comment", i, j))
            i = j
        elif ch == "/" and text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            spans.append(("comment", i, j))
            i = j
        elif ch == '"':
            j = _quoted_end(text, i, '"')
            spans.append(("string", i, j))
            i = j
        elif ch == "'":
            j = _quoted_end(text, i, "'")
            spans.append(("char", i, j))
            i = j
        elif ch.isascii() and (ch.isalpha() or ch == "_"):
            m = _IDENT_RE.match(text, i)
            assert m
            word, j = m.group(0), m.end()
            if word in _RAW_PREFIX and j < n and text[j] == '"':
                raw = _RAW_RE.match(text, j)
                if raw:
                    close = ")" + raw.group(1) + '"'
                    k = text.find(close, raw.end())
                    k = n if k < 0 else k + len(close)
                    spans.append(("string", i, k))
                    i = k
                    continue
            if word in _STR_PREFIX and j < n and text[j] in "\"'":
                k = _quoted_end(text, j, text[j])
                spans.append(("string" if text[j] == '"' else "char", i, k))
                i = k
                continue
            i = j
        elif ch.isascii() and ch.isdigit():
            m = _NUMBER_RE.match(text, i)
            assert m
            i = m.end()
        else:
            i += 1
    return spans


def mask(text: str, spans: list[tuple[str, int, int]], kinds: set[str]) -> str:
    """Текст той же длины, где отрезки видов kinds заменены пробелами (переводы строк сохранены)."""
    out = list(text)
    for kind, a, b in spans:
        if kind in kinds:
            for k in range(a, b):
                if out[k] != "\n":
                    out[k] = " "
    return "".join(out)


class LineIndex:
    def __init__(self, text: str) -> None:
        self.starts = [0] + [m.end() for m in re.finditer("\n", text)]

    def line(self, pos: int) -> int:
        return bisect.bisect_right(self.starts, pos)


def iter_sources(root: Path, subdir: str, exts: set[str]):
    base = root / subdir
    if not base.is_dir():
        return
    for dirpath, dirnames, filenames in os.walk(base):
        dirnames[:] = sorted(d for d in dirnames if not d.startswith("."))
        for name in sorted(filenames):
            if Path(name).suffix.lower() in exts:
                yield Path(dirpath) / name


def read_source(path: Path) -> str:
    return path.read_bytes().decode("utf-8-sig", errors="replace").replace("\r\n", "\n")


# ---- конец лексера ----


def main(argv: list[str]) -> int:
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser(description="Нет записи в реестр из src (кроме списка исключений)")
    ap.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    args = ap.parse_args(argv)
    root = args.root.resolve()
    errors: list[str] = []
    count = 0
    default_ini = False
    settings_users: list[str] = []  # «файл:строка» с QSettings без IniFormat в том же файле
    for path in iter_sources(root, "src", CPP_EXT):
        rel = path.relative_to(root).as_posix()
        count += 1
        if any(fnmatch.fnmatchcase(rel, pattern) for pattern in ALLOWED):
            continue
        text = read_source(path)
        spans = lex(text)
        code = mask(text, spans, {"comment"})
        lines = LineIndex(text)
        for m in FORBIDDEN_RE.finditer(code):
            token = re.sub(r"\s+", "", m.group(1))
            errors.append(f"{rel}:{lines.line(m.start())}: реестр запрещён вне списка исключений: {token}")
        bare = INCLUDE_LINE_RE.sub(lambda m: " " * len(m.group(0)), mask(text, spans, {"comment", "string", "char"}))
        if DEFAULT_INI_RE.search(bare):
            default_ini = True
        use = QSETTINGS_RE.search(bare)
        if use and "IniFormat" not in bare:
            settings_users.append(f"{rel}:{lines.line(use.start())}")
    if settings_users and not default_ini:
        for where in settings_users:
            errors.append(
                f"{where}: QSettings без IniFormat, а QSettings::setDefaultFormat(QSettings::IniFormat) "
                "в src нигде не вызывается — по умолчанию это реестр (эталон 4.7)"
            )
    if errors:
        print("\n".join(errors))
        print(f"no_registry: красный, нарушений {len(errors)} (файлов {count})")
        return 1
    print(f"no_registry: зелёный (файлов {count})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
