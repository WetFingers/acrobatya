"""Страж слоёв (ctest unit.guard.boundaries, эталон 5.1).

    "%OTTISK_PYTHON%" -X utf8 -I tools/boundaries_guard.py --root <репозиторий>

По директивам #include (комментарии не считаются):
  * src/core/**     — без Qt (<Q…>, <Qt…>, "Q…") и без заголовков services/ и ui/;
  * src/services/** — без QtWidgets (<QtWidgets/…>, <QWidget>, <QApplication>, виджеты и диалоги)
                      и без заголовков ui/;
  * src/ui/**       — без MuPDF (<mupdf/…>, "mupdf/…") — только через типы-значения core;
  * оболочка Windows (ShellExecute*, QDesktopServices::openUrl) — только в src/services/shell_launcher.cpp
    (план 2.4, эталон 7.3: иначе offscreen-прогоны откроют на машине человека Edge, Word, Параметры);
  * сеть программы (QNetworkAccessManager, OSSL_HTTP_*, OCSP_sendreq_*, WinHTTP, WinINet, URLDownloadToFile) —
    только в src/services/http_fetcher.cpp (план 4.1, эталон 7.4, security-36: единая точка сети с выключателем).
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
INCLUDE_RE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*([<"])([^>"\n]+)[>"]', re.MULTILINE)

# Заголовки QtWidgets (и QtPrintSupport-диалогов, которым нужен Widgets), запрещённые в services.
WIDGET_HEADERS = {
    "QWidget", "QApplication", "QMainWindow", "QDialog", "QDialogButtonBox", "QMessageBox",
    "QFileDialog", "QInputDialog", "QProgressDialog", "QColorDialog", "QFontDialog", "QErrorMessage",
    "QPushButton", "QToolButton", "QRadioButton", "QCheckBox", "QAbstractButton", "QButtonGroup",
    "QLabel", "QLineEdit", "QTextEdit", "QPlainTextEdit", "QTextBrowser", "QComboBox", "QFontComboBox",
    "QSpinBox", "QDoubleSpinBox", "QSlider", "QScrollBar", "QProgressBar", "QDateEdit", "QDateTimeEdit",
    "QMenu", "QMenuBar", "QToolBar", "QStatusBar", "QDockWidget", "QSplitter", "QStackedWidget",
    "QTabWidget", "QTabBar", "QGroupBox", "QFrame", "QScrollArea", "QAbstractScrollArea",
    "QListWidget", "QListView", "QTreeWidget", "QTreeView", "QTableWidget", "QTableView", "QHeaderView",
    "QAbstractItemView", "QStyledItemDelegate", "QItemDelegate", "QLayout", "QBoxLayout", "QVBoxLayout",
    "QHBoxLayout", "QGridLayout", "QFormLayout", "QStackedLayout", "QSizePolicy", "QSpacerItem",
    "QStyle", "QStyleOption", "QStylePainter", "QProxyStyle", "QCommonStyle", "QStyleFactory",
    "QSystemTrayIcon", "QToolTip", "QWhatsThis", "QCompleter", "QWizard", "QWizardPage",
    "QGraphicsView", "QGraphicsScene", "QGraphicsItem", "QRubberBand", "QSizeGrip", "QShortcut",
    "QKeySequenceEdit", "QCalendarWidget", "QLCDNumber", "QMdiArea", "QMdiSubWindow", "QUndoView",
    "QPrintDialog", "QPrintPreviewDialog", "QPrintPreviewWidget", "QPageSetupDialog",
    "QAbstractPrintDialog", "QWidgetAction", "QDesktopWidget", "QOpenGLWidget",
}
LAYER_HEADER_RE = re.compile(r"^(?:\.\./)*(?:src/)?(core|services|ui)/")
# Вызовы оболочки Windows вне ShellLauncher (по коду без комментариев и строк).
SHELL_RE = re.compile(r"\bShellExecute\w*\b|\bQDesktopServices\s*::\s*openUrl\b")
SHELL_OWNER = "src/services/shell_launcher.cpp"
# Сеть программы вне HttpFetcher (по коду без комментариев и строк).
NET_RE = re.compile(r"\bQNetworkAccessManager\b|\bOSSL_HTTP_\w+|\bOCSP_sendreq_\w+|\bWinHttp\w+|"
                    r"\bInternet(?:Open|Connect|ReadFile)\w*|\bURLDownloadTo\w+")
NET_OWNER = "src/services/http_fetcher.cpp"

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


def classify(rel: str) -> str | None:
    for layer in ("core", "services", "ui"):
        if rel.startswith(f"src/{layer}/"):
            return layer
    return None


def violations(layer: str, bracket: str, header: str) -> list[str]:
    h = header.strip().replace("\\", "/")
    base = h.split("/")[-1].removesuffix(".h")
    first = h.split("/")[0]
    out: list[str] = []
    layer_ref = LAYER_HEADER_RE.match(h)
    shown = f"{bracket}{h}{'>' if bracket == '<' else bracket}"
    if layer == "core":
        if first.startswith("Q") and (first.startswith("Qt") or first[1:2].isupper()):
            out.append(f"core включает Qt: {shown}")
        if layer_ref and layer_ref.group(1) in ("services", "ui"):
            out.append(f"core включает слой {layer_ref.group(1)}: {h}")
    elif layer == "services":
        if first == "QtWidgets" or base in WIDGET_HEADERS:
            out.append(f"services включает QtWidgets: {shown}")
        if layer_ref and layer_ref.group(1) == "ui":
            out.append(f"services включает слой ui: {h}")
    elif layer == "ui":
        if first.lower() == "mupdf" or h.lower().startswith("../mupdf/"):
            out.append(f"ui включает MuPDF: {shown} — только типы-значения core")
    return out


def main(argv: list[str]) -> int:
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser(description="Границы слоёв core → services → ui по #include")
    ap.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    args = ap.parse_args(argv)
    root = args.root.resolve()
    errors: list[str] = []
    count = 0
    for path in iter_sources(root, "src", CPP_EXT):
        rel = path.relative_to(root).as_posix()
        layer = classify(rel)
        if layer is None:
            continue
        count += 1
        text = read_source(path)
        code = mask(text, lex(text), {"comment"})
        lines = LineIndex(text)
        for m in INCLUDE_RE.finditer(code):
            for msg in violations(layer, m.group(1), m.group(2)):
                errors.append(f"{rel}:{lines.line(m.start(2))}: {msg}")
        if rel != SHELL_OWNER:
            bare = mask(text, lex(text), {"comment", "string", "char"})
            for m in SHELL_RE.finditer(bare):
                errors.append(f"{rel}:{lines.line(m.start())}: {m.group(0)} — оболочка Windows только через "
                              f"ShellLauncher ({SHELL_OWNER})")
        if rel != NET_OWNER:
            bare = mask(text, lex(text), {"comment", "string", "char"})
            for m in NET_RE.finditer(bare):
                errors.append(f"{rel}:{lines.line(m.start())}: {m.group(0)} — сеть только через HttpFetcher "
                              f"({NET_OWNER})")
    if errors:
        print("\n".join(errors))
        print(f"boundaries: красный, нарушений {len(errors)} (файлов {count})")
        return 1
    print(f"boundaries: зелёный (файлов {count})")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
