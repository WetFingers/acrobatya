"""Страж переводов (ctest unit.guard.i18n, эталон 5.7).

    "%OTTISK_PYTHON%" -X utf8 -I tools/i18n_guard.py --root <репозиторий>

Кириллица в src/**/*.{h,hpp,cpp,cc,inl} допускается только:
  * в комментариях;
  * в строковых литералах внутри tr(…), QT_TR_NOOP(…), QT_TR_N_NOOP(…), QT_TRANSLATE_NOOP(…),
    QT_TRANSLATE_NOOP3(…), QT_TRANSLATE_N_NOOP(…), QCoreApplication::translate(…) (и Q[Gui]Application::).
В src/core — ни одной кириллицы вне комментариев (ядро возвращает коды и английский текст журнала).
Экранированная кириллица («\\u0430») считается кириллицей.
Код 0 — зелёный, 1 — красный (строки «файл:строка: что»). Каталога src нет — зелёный.
"""

from __future__ import annotations

import argparse
import bisect
import os
import re
import sys
from pathlib import Path

CPP_EXT = {".h", ".hpp", ".hxx", ".cpp", ".cc", ".cxx", ".inl"}
TR_CALLS = {
    "tr",
    "QT_TR_NOOP",
    "QT_TR_NOOP_UTF8",
    "QT_TR_N_NOOP",
    "QT_TRANSLATE_NOOP",
    "QT_TRANSLATE_NOOP3",
    "QT_TRANSLATE_NOOP_UTF8",
    "QT_TRANSLATE_N_NOOP",
    "QT_TRANSLATE_N_NOOP3",
}
TRANSLATE_OWNERS = {"QCoreApplication", "QGuiApplication", "QApplication"}
CYR_RE = re.compile(r"[Ѐ-ԯ]|\\u04[0-9a-fA-F]{2}|\\u05[0-2][0-9a-fA-F]|\\U0000(?:04|05[0-2])[0-9a-fA-F]{2}")
CALL_NAME_RE = re.compile(r"(?:([A-Za-z_]\w*)\s*::\s*)?([A-Za-z_]\w*)\s*$")

# ---- лексер C++: комментарии, строки, символы (копии в fztry/boundaries/no_registry_guard.py — правьте все четыре) ----
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


def _snippet(text: str, a: int, b: int) -> str:
    s = text[a:b].replace("\n", " ")
    return s if len(s) <= 60 else s[:57] + "…"


def check_file(path: Path, rel: str, is_core: bool) -> list[str]:
    text = read_source(path)
    spans = lex(text)
    lines = LineIndex(text)
    errors: list[str] = []
    code = mask(text, spans, {"comment", "string", "char"})
    literals = {a: (kind, b) for kind, a, b in spans if kind in ("string", "char")}
    # Стек скобок: True — внутри вызова перевода.
    stack: list[bool] = []
    i, n = 0, len(text)
    while i < n:
        if i in literals:
            kind, end = literals[i]
            if CYR_RE.search(text, i, end):
                inside_tr = any(stack)
                if is_core:
                    errors.append(f"{rel}:{lines.line(i)}: кириллица в src/core вне комментария: «{_snippet(text, i, end)}»")
                elif not inside_tr:
                    errors.append(
                        f"{rel}:{lines.line(i)}: кириллица вне tr()/QT_TR_NOOP()/translate(): «{_snippet(text, i, end)}»"
                    )
            i = end
            continue
        ch = code[i]
        if ch == "(":
            m = CALL_NAME_RE.search(code, max(0, i - 120), i)
            translating = False
            if m:
                owner, name = m.group(1), m.group(2)
                translating = name in TR_CALLS or (name == "translate" and owner in TRANSLATE_OWNERS)
            stack.append(translating)
        elif ch == ")":
            if stack:
                stack.pop()
        elif CYR_RE.match(code, i):
            errors.append(f"{rel}:{lines.line(i)}: кириллица в коде (не строка и не комментарий)")
            j = code.find("\n", i)
            i = n if j < 0 else j
            continue
        i += 1
    return errors


def main(argv: list[str]) -> int:
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser(description="Кириллица в src только в tr() и комментариях")
    ap.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    args = ap.parse_args(argv)
    root = args.root.resolve()
    if not (root / "src").is_dir():
        print("i18n: каталога src нет — проверять нечего")
        return 0
    errors: list[str] = []
    count = 0
    for path in iter_sources(root, "src", CPP_EXT):
        rel = path.relative_to(root).as_posix()
        count += 1
        errors.extend(check_file(path, rel, rel.startswith("src/core/")))
    if errors:
        print("\n".join(errors))
        print(f"i18n: красный, нарушений {len(errors)} (файлов {count})")
        return 1
    print(f"i18n: зелёный (файлов {count})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
