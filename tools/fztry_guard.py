"""Страж fz_try (ctest unit.guard.fztry, эталон 4.1, 5.5).

    "%OTTISK_PYTHON%" -X utf8 -I tools/fztry_guard.py --root <репозиторий>

Правила (setjmp/longjmp MuPDF не знает о C++: деструкторы и исключения через кадр fz_try — UB):
  1. fz_try / fz_always / fz_catch в src/** — только в src/core/mu/ (error.h, call.h — обёртка mu::call);
     весь остальной код ходит в MuPDF через mu::call. fuzz/ — отдельная программа, ей можно.
  2. fz_var: локал функции, объявленный до fz_try и меняемый внутри блока fz_try, обязан пройти
     fz_var(имя) до fz_try (иначе после longjmp его значение не определено). Везде: src, tests, tools, fuzz.
  3. Тело лямбды mu::call: нет return / throw / co_return, нет вложенного mu::call и нет объектов
     с деструкторами (std::string и прочие контейнеры, умные указатели, fmt::format, paths::widen…,
     mu::*Handle, MuError) — longjmp их не разрушит. Везде: src, tests, tools, fuzz.
Правила 2–3 эвристические (разбор по скобкам, без компилятора); перед проверкой дерева страж гоняет
самопроверку на встроенных примерах. Комментарии и строки не считаются.
Код 0 — зелёный, 1 — красный (строки «файл:строка: что»). Каталога src нет — зелёный.
"""

from __future__ import annotations

import argparse
import bisect
import os
import re
import sys
from pathlib import Path

CPP_EXT = {".h", ".hpp", ".hxx", ".cpp", ".cc", ".cxx", ".inl", ".c"}
SCAN_DIRS = ("src",)
DEEP_DIRS = ("src", "tests", "tools", "fuzz")
ALLOWED_PREFIXES = ("src/core/mu/",)
FZ_TRY_RE = re.compile(r"\b(fz_try|fz_always|fz_catch)\b")
FZ_TRY_ONLY_RE = re.compile(r"\bfz_try\s*\(")
MU_CALL_RE = re.compile(r"\b(?:ot::)?mu::call\s*\(")
FORBIDDEN_IN_BODY_RE = re.compile(r"\b(return|throw|co_return)\b")
NESTED_CALL_RE = re.compile(r"\b(?:ot::)?mu::call\b")
# Объекты с нетривиальным деструктором (объявления и временные) — эвристический список.
DTOR_RE = re.compile(
    r"\bstd::(?:(?:w|u8|u16|u32)?string(?!_view)|vector|map|multimap|unordered_map|set|unordered_set|list|deque"
    r"|unique_ptr|shared_ptr|make_unique|make_shared|function|optional|variant|any|to_string|to_wstring"
    r"|ostringstream|stringstream|istringstream|lock_guard|scoped_lock|unique_lock|filesystem::path"
    r"|runtime_error|exception)\b"
    r"|\bfmt::format\b|\bpaths::(?:widen|narrow|fromUtf8|toUtf8|readFile)\b|\bmu::\w*Handle\b|\bMuError\b"
)
NOT_FUNCTIONS = {"if", "for", "while", "switch", "catch", "fz_try", "fz_always", "fz_catch", "sizeof", "return"}
NOT_TYPES = {"return", "delete", "goto", "case", "else", "throw", "new", "using", "typedef", "namespace",
             "co_return", "sizeof", "do", "struct", "class", "enum", "operator"}
DECL_RE = re.compile(
    r"(?:^|[;{}(])\s*(?:(?:const|volatile|static|register|unsigned|signed|struct)\s+)*"
    r"([A-Za-z_][\w:]*(?:\s*<[^;{}()]*>)?)(?:\s*\*+\s*(?:const\s*)?|\s*&\s*|\s+)([A-Za-z_]\w*)\s*(?==|;|\[|\{|,)"
)
ASSIGN_RE = re.compile(
    r"(?<![\w.>:])([A-Za-z_]\w*)\s*(?:[+\-*/%&|^]|<<|>>)?=(?!=)"
    r"|(?<![\w.>:])([A-Za-z_]\w*)\s*(?:\+\+|--)|(?:\+\+|--)\s*([A-Za-z_]\w*)"
)

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


def brace_pairs(code: str) -> dict[int, int]:
    """Открывающая фигурная скобка → закрывающая (несбалансированные — до конца текста)."""
    pairs: dict[int, int] = {}
    stack: list[int] = []
    for i, ch in enumerate(code):
        if ch == "{":
            stack.append(i)
        elif ch == "}" and stack:
            pairs[stack.pop()] = i
    for o in stack:
        pairs[o] = len(code)
    return pairs


def match_back(code: str, close: int, open_ch: str, close_ch: str) -> int:
    depth = 0
    for i in range(close, -1, -1):
        if code[i] == close_ch:
            depth += 1
        elif code[i] == open_ch:
            depth -= 1
            if depth == 0:
                return i
    return -1


def match_forward(code: str, open_: int, open_ch: str, close_ch: str) -> int:
    depth = 0
    for i in range(open_, len(code)):
        if code[i] == open_ch:
            depth += 1
        elif code[i] == close_ch:
            depth -= 1
            if depth == 0:
                return i
    return len(code)


def is_function_header(code: str, brace: int) -> bool:
    """Скобка `{` открывает тело функции или лямбды (а не if/for/while/fz_try…)."""
    hdr = re.sub(r"(\s*\b(?:const|noexcept|override|final|mutable)\b)+\s*$", "", code[:brace].rstrip())
    hdr = re.sub(r"\s*->\s*[\w:<>,*&\s]+$", "", hdr)
    if not hdr.endswith(")"):
        return False
    op = match_back(code, len(hdr) - 1, "(", ")")
    if op < 0:
        return False
    before = code[:op].rstrip()
    if before.endswith("]"):
        return True  # лямбда
    m = re.search(r"([A-Za-z_]\w*)\s*$", before)
    return bool(m) and m.group(1) not in NOT_FUNCTIONS


def enclosing_body(code: str, pairs: dict[int, int], pos: int) -> int:
    """Начало тела функции/лямбды, внутри которой стоит pos (0 — не нашли)."""
    for o in sorted((o for o, c in pairs.items() if o < pos < c), reverse=True):
        if is_function_header(code, o):
            return o + 1
    return 0


def check_fz_var(rel: str, code: str, lines: LineIndex) -> list[str]:
    errors = []
    pairs = brace_pairs(code)
    for m in FZ_TRY_ONLY_RE.finditer(code):
        k = match_forward(code, m.end() - 1, "(", ")") + 1
        while k < len(code) and code[k].isspace():
            k += 1
        if k < len(code) and code[k] == "{":
            block = code[k + 1 : pairs.get(k, len(code))]
        else:
            end = code.find(";", k)
            block = code[k : end if end >= 0 else len(code)]
        start = enclosing_body(code, pairs, m.start())
        prefix = code[start : m.start()]
        declared = {d.group(2) for d in DECL_RE.finditer(prefix) if d.group(1).split("<")[0] not in NOT_TYPES}
        protected = set(re.findall(r"\bfz_var\s*\(\s*([A-Za-z_]\w*)\s*\)", prefix))
        assigned = {a.group(1) or a.group(2) or a.group(3) for a in ASSIGN_RE.finditer(block)}
        for name in sorted((assigned & declared) - protected):
            errors.append(
                f"{rel}:{lines.line(m.start())}: {name} меняется внутри fz_try, но объявлен до него без fz_var({name})"
            )
    return errors


def lambda_body(code: str, call_open: int) -> tuple[int, int] | None:
    """(начало, конец) тела лямбды — второго аргумента mu::call(ctx, [..](..){ … })."""
    end = match_forward(code, call_open, "(", ")")
    depth = 0
    seen_comma = False
    i = call_open + 1
    while i < end:
        ch = code[i]
        if ch == "[" and depth == 0 and seen_comma:
            j = match_forward(code, i, "[", "]") + 1
            while j < end and code[j] != "{":
                if code[j] == "(":
                    j = match_forward(code, j, "(", ")")
                j += 1
            if j >= end:
                return None
            return j + 1, match_forward(code, j, "{", "}")
        if ch in "([{":
            depth += 1
        elif ch in ")]}":
            depth -= 1
        elif ch == "," and depth == 0:
            seen_comma = True
        i += 1
    return None


def check_mu_call(rel: str, code: str, lines: LineIndex) -> list[str]:
    errors = []
    for m in MU_CALL_RE.finditer(code):
        body = lambda_body(code, m.end() - 1)
        if body is None:
            continue
        a, b = body
        text = code[a:b]
        for f in FORBIDDEN_IN_BODY_RE.finditer(text):
            errors.append(f"{rel}:{lines.line(a + f.start())}: {f.group(1)} внутри тела mu::call (longjmp, исключения C++)")
        for f in NESTED_CALL_RE.finditer(text):
            errors.append(f"{rel}:{lines.line(a + f.start())}: вложенный mu::call — исключение C++ пересечёт кадр fz_try")
        for f in DTOR_RE.finditer(text):
            errors.append(
                f"{rel}:{lines.line(a + f.start())}: объект с деструктором внутри тела mu::call ({f.group(0)}) — "
                "объявите его снаружи"
            )
    return errors


def check_text(rel: str, text: str, rule_try_location: bool) -> list[str]:
    code = mask(text, lex(text), {"comment", "string", "char"})
    lines = LineIndex(text)
    errors: list[str] = []
    if rule_try_location and not rel.startswith(ALLOWED_PREFIXES):
        for m in FZ_TRY_RE.finditer(code):
            errors.append(f"{rel}:{lines.line(m.start())}: {m.group(1)} вне src/core/mu/ — используйте mu::call")
    errors += check_fz_var(rel, code, lines)
    errors += check_mu_call(rel, code, lines)
    return errors


SELF_TEST = [
    # (имя, код, ожидаемое число нарушений)
    ("return в теле", "void f(fz_context* ctx){ mu::call(ctx, [&]{ if (x) return; fz_foo(ctx); }); }", 1),
    ("throw в теле", "void f(fz_context* ctx){ ot::mu::call(ctx, [&] { throw 1; }); }", 1),
    ("std::string в теле", 'void f(fz_context* c){ mu::call(c, [&]{ std::string s = "a"; g(s.c_str()); }); }', 1),
    ("handle в теле", "void f(fz_context* c){ mu::call(c, [&]() { mu::PageHandle p(c); }); }", 1),
    ("вложенный mu::call", "void f(fz_context* c){ mu::call(c, [&]{ mu::call(c, [&]{ n = 1; }); }); }", 1),
    ("хорошее тело", "void f(fz_context* c){ mu::call(c, [&]{ page.reset(fz_load_page(c, d, 0)); n = 2; "
                     "std::string_view v; }); }", 0),
    ("return после тела", "int f(fz_context* c){ int n = 0; mu::call(c, [&]{ n = 1; }); return n; }", 0),
    ("нет fz_var", "void g(fz_context* ctx){ fz_page* p = NULL; fz_try(ctx){ p = fz_load_page(ctx, d, 0); }"
                   " fz_catch(ctx){ fz_report_error(ctx); } }", 1),
    ("есть fz_var", "void g(fz_context* ctx){ fz_page* p = NULL; fz_var(p); if (a) { fz_try(ctx){ p = load(ctx); }"
                    " fz_catch(ctx){ fz_report_error(ctx); } } }", 0),
    ("меняется только в catch", "void g(fz_context* ctx){ int code = 0; fz_try(ctx){ body(); } fz_catch(ctx){ "
                                "code = fz_caught(ctx); } }", 0),
    ("локал внутри try", "void g(fz_context* ctx){ fz_try(ctx){ float w = 1; w = 2; } fz_catch(ctx){ } }", 0),
    ("инкремент без fz_var", "void g(fz_context* ctx){ int n = 0; fz_try(ctx){ ++n; } fz_catch(ctx){ } }", 1),
    ("сравнение — не запись", "void g(fz_context* ctx){ int n = 0; fz_try(ctx){ if (n == 1 || n <= 2) f(); }"
                              " fz_catch(ctx){ } }", 0),
    ("комментарий не код", "void f(fz_context* c){ mu::call(c, [&]{ /* return std::string */ n = 1; }); }", 0),
]


def self_test() -> list[str]:
    failures = []
    for name, code, want in SELF_TEST:
        got = check_text("self-test.cpp", code, rule_try_location=False)
        if len(got) != want:
            failures.append(f"самопроверка «{name}»: ждали {want}, нашли {len(got)}: {got}")
    return failures


def main(argv: list[str]) -> int:
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser(description="fz_try только в src/core/mu/, fz_var, тела mu::call")
    ap.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    args = ap.parse_args(argv)
    broken = self_test()
    if broken:
        print("\n".join(broken))
        print("fztry: красный — самопроверка стража не прошла")
        return 1
    root = args.root.resolve()
    errors: list[str] = []
    count = 0
    for sub in DEEP_DIRS:
        for path in iter_sources(root, sub, CPP_EXT):
            rel = path.relative_to(root).as_posix()
            count += 1
            errors += check_text(rel, read_source(path), rule_try_location=sub in SCAN_DIRS)
    if errors:
        print("\n".join(errors))
        print(f"fztry: красный, нарушений {len(errors)} (файлов {count})")
        return 1
    print(f"fztry: зелёный (файлов {count}, самопроверка {len(SELF_TEST)} примеров)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
