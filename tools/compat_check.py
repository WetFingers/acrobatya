"""Совместимость PDF без Acrobat (эталон 9.4): косвенный критерий «Acrobat откроет без ремонта».

    "%OTTISK_PYTHON%" -X utf8 -I tools/compat_check.py <файл.pdf | каталог> […] [--json out.json]
        [--password PWD] [--forms] [--expect-sig INTACT|TRUSTED|…] [--trust cert.pem] [--pdfa] [--tagged]
        [--expect-text ожидания.json]

Каталог — все *.pdf в нём (по имени; пустой каталог — ошибка). Проверки по каждому файлу:
  * qpdf --check                  — код 0 (3 = предупреждения — тоже красный);
  * pdfcpu validate -m strict     — код 0;
  * mutool clean <файл> <tmp>     — без «repair» в выводе и с кодом 0 (во временный файл);
  * pypdfium2                     — рендер первой страницы (движок Chrome/Edge), --forms — с полями;
  * --expect-sig S                — pyHanko: каждая подпись проверяется без сети, в сводке
                                    (summary, например «INTACT:UNTRUSTED,UNTOUCHED») есть все токены S
                                    («INTACT», «TRUSTED», «INTACT,UNTOUCHED»…); --trust — доверенный корень;
  * --pdfa                        — verapdf -f 0 (автоопределение части/уровня) — соответствует;
  * --tagged                      — tools/structcheck.py, если он уже есть (иначе пропуск с пометкой);
  * --expect-text F.json          — pypdfium2 извлекает текст первой страницы, и в нём (без пробелов и
                                    переводов строк) есть каждая строка из F.json {"имя.pdf": ["…", …]}.
JSON (--json): [{"file", "ok", "checks": [{"tool", "version", "exit", "ok", "skipped", "warnings": [...]}]}].
Код 0 — все файлы прошли, 1 — нет, 77 — нет qpdf (ctest SKIP_RETURN_CODE 77), 2 — неверные аргументы.
Инструменты: OTTISK_TOOLS (по умолчанию C:\\Tools)\\{qpdf\\bin\\qpdf.exe, pdfcpu\\pdfcpu.exe,
verapdf\\verapdf.bat}; mutool — OTTISK_MUTOOL или C:\\src\\mupdf\\platform\\win32\\x64\\Release\\mutool.exe;
или из PATH.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SKIP_CODE = 77
TIMEOUT = 600


def tools_dir() -> Path:
    return Path(os.environ.get("OTTISK_TOOLS") or r"C:\Tools")


def find_tool(env_var: str | None, candidates: list[Path], names: list[str]) -> Path | None:
    if env_var and os.environ.get(env_var):
        p = Path(os.environ[env_var])
        if p.is_file():
            return p
    for p in candidates:
        if p.is_file():
            return p
    for n in names:
        w = shutil.which(n)
        if w:
            return Path(w)
    return None


def run(cmd: list[str], timeout: int = TIMEOUT) -> tuple[int, str]:
    try:
        proc = subprocess.run(
            cmd, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=timeout
        )
        return proc.returncode, (proc.stdout or "") + (proc.stderr or "")
    except subprocess.TimeoutExpired:
        return -1, f"таймаут {timeout} с"
    except OSError as exc:
        return -2, f"не запустилось: {exc}"


_versions: dict[str, str] = {}


def tool_version(key: str, cmd: list[str], pattern: str) -> str:
    if key not in _versions:
        _, out = run(cmd, timeout=120)
        m = re.search(pattern, out)
        _versions[key] = m.group(1) if m else (out.strip().splitlines() or ["?"])[0][:60]
    return _versions[key]


def result(tool: str, version: str | None, code: int | None, ok: bool, warnings: list[str], skipped: bool = False) -> dict:
    return {"tool": tool, "version": version, "exit": code, "ok": ok, "skipped": skipped, "warnings": warnings}


def interesting(out: str, limit: int = 20, pdf: Path | None = None) -> list[str]:
    if pdf is not None:  # полный путь в каждой строке — шум; оставляем имя файла
        out = out.replace(str(pdf), pdf.name)
    lines = [ln.strip() for ln in out.splitlines() if ln.strip()]
    return lines[:limit]


# ------------------------------------------------------------------ проверки


def check_qpdf(qpdf: Path, pdf: Path, password: str | None) -> dict:
    ver = tool_version("qpdf", [str(qpdf), "--version"], r"qpdf version (\S+)")
    cmd = [str(qpdf), "--check"]
    if password:
        cmd.append(f"--password={password}")
    code, out = run([*cmd, str(pdf)])
    warnings = [ln for ln in interesting(out, 50, pdf) if re.search(r"^WARNING|^qpdf:|damaged|attempting to reconstruct", ln, re.I)]
    if code != 0 and not warnings:
        warnings = interesting(out, 20, pdf)
    return result("qpdf --check", ver, code, code == 0, warnings)


def check_pdfcpu(pdfcpu: Path | None, pdf: Path, password: str | None) -> dict:
    if pdfcpu is None:
        return result("pdfcpu validate -m strict", None, None, True, ["pdfcpu не найден — пропуск"], skipped=True)
    ver = tool_version("pdfcpu", [str(pdfcpu), "version"], r"version:\s*(\S+)")
    cmd = [str(pdfcpu), "validate", "-m", "strict"]
    if password:
        cmd += ["--upw", password, "--opw", password]
    code, out = run([*cmd, str(pdf)])
    ok = code == 0 and "validation ok" in out.lower()
    warnings = [] if ok else interesting(out, 20, pdf)
    return result("pdfcpu validate -m strict", ver, code, ok, warnings)


def check_mutool(mutool: Path | None, pdf: Path, password: str | None) -> dict:
    if mutool is None:
        return result("mutool clean", None, None, True, ["mutool не найден — пропуск"], skipped=True)
    ver = tool_version("mutool", [str(mutool), "-v"], r"version (\S+)")
    with tempfile.TemporaryDirectory(prefix="ottisk-compat-") as tmp:
        out_pdf = Path(tmp) / "clean.pdf"
        cmd = [str(mutool), "clean"]
        if password:
            cmd += ["-p", password]
        code, out = run([*cmd, str(pdf), str(out_pdf)])
    repairs = [ln for ln in interesting(out, 200, pdf) if re.search(r"repair", ln, re.I)]
    other = [ln for ln in interesting(out, 50, pdf) if ln not in repairs and re.search(r"warning|error", ln, re.I)]
    ok = code == 0 and not repairs
    return result("mutool clean", ver, code, ok, repairs + other)


def check_pdfium(pdf: Path, password: str | None, forms: bool) -> dict:
    try:
        import pypdfium2 as pdfium
    except ImportError as exc:
        return result("pypdfium2", None, None, False, [f"нет pypdfium2 ({exc}) — запускайте через OTTISK_PYTHON"])
    try:
        from importlib.metadata import version as pkg_version

        ver = pkg_version("pypdfium2")
    except Exception:  # noqa: BLE001
        ver = "?"
    build = getattr(pdfium, "PDFIUM_INFO", None)
    if build:
        ver = f"{ver} (pdfium {build})"
    try:
        doc = pdfium.PdfDocument(str(pdf), password=password)
        try:
            if forms:
                doc.init_forms()
            n = len(doc)
            if n == 0:
                return result("pypdfium2 render", str(ver), 1, False, ["0 страниц"])
            page = doc[0]
            bitmap = page.render(scale=1, may_draw_forms=forms)
            img = bitmap.to_pil()
            w, h = img.size
            extrema = img.convert("L").getextrema()
            notes = [f"страниц {n}, первая {w}×{h} px"]
            if extrema[0] == extrema[1]:
                notes.append("первая страница одноцветная (пустая?)")
            return result("pypdfium2 render" + (" --draw-forms" if forms else ""), str(ver), 0, True, notes)
        finally:
            doc.close()
    except Exception as exc:  # noqa: BLE001 — любой отказ pdfium — красный
        return result("pypdfium2 render", str(ver), 1, False, [f"{type(exc).__name__}: {exc}"])


def check_text(pdf: Path, password: str | None, expected: list[str]) -> dict:
    import pypdfium2 as pdfium

    def squeeze(s: str) -> str:
        return "".join(ch for ch in s if not ch.isspace())

    try:
        doc = pdfium.PdfDocument(str(pdf), password=password)
        try:
            text = squeeze(doc[0].get_textpage().get_text_range())
        finally:
            doc.close()
    except Exception as exc:  # noqa: BLE001
        return result("pypdfium2 text", None, 1, False, [f"{type(exc).__name__}: {exc}"])
    missing = [e for e in expected if squeeze(e) not in text]
    return result("pypdfium2 text", None, 0 if not missing else 1, not missing,
                  [f"нет «{m}»" for m in missing] or [f"строк {len(expected)}"])


def check_signatures(pdf: Path, expect: str, trust: list[Path]) -> dict:
    try:
        from importlib.metadata import version as pkg_version

        from pyhanko.pdf_utils.reader import PdfFileReader
        from pyhanko.sign.validation import validate_pdf_signature
        from pyhanko_certvalidator import ValidationContext
    except ImportError as exc:
        return result("pyhanko sign validate", None, None, False, [f"нет pyHanko ({exc})"])
    ver = pkg_version("pyhanko")
    import logging

    for name in ("pyhanko", "pyhanko_certvalidator"):  # самоподписанный и т. п. — не трассировки в stderr
        logging.getLogger(name).setLevel(logging.CRITICAL)
    wanted = [t for t in re.split(r"[:,\s]+", expect.upper()) if t]
    warnings: list[str] = []
    ok = True
    try:
        roots = []
        if trust:
            from asn1crypto import pem, x509

            for p in trust:
                data = p.read_bytes()
                for _, _, der in pem.unarmor(data, multiple=True) if pem.detect(data) else [(None, None, data)]:
                    roots.append(x509.Certificate.load(der))
        vc = ValidationContext(trust_roots=roots or None, allow_fetching=False)
        with pdf.open("rb") as fh:
            reader = PdfFileReader(fh, strict=False)
            sigs = list(reader.embedded_signatures)
            if not sigs:
                return result("pyhanko sign validate", ver, 1, False, ["подписей в файле нет"])
            for sig in sigs:
                status = validate_pdf_signature(sig, vc)
                summary = status.summary()
                tokens = set(re.split(r"[:,]", summary))
                missing = [t for t in wanted if t not in tokens]
                warnings.append(f"{sig.field_name}: {summary}")
                if missing:
                    ok = False
                    warnings.append(f"{sig.field_name}: нет {', '.join(missing)} (ожидалось {expect})")
    except Exception as exc:  # noqa: BLE001
        return result("pyhanko sign validate", ver, 1, False, [f"{type(exc).__name__}: {exc}"])
    return result("pyhanko sign validate", ver, 0 if ok else 1, ok, warnings)


def check_verapdf(verapdf: Path | None, pdf: Path) -> dict:
    if verapdf is None:
        return result("verapdf -f 0", None, None, False, ["veraPDF не найден, а --pdfa задан"])
    ver = tool_version("verapdf", [str(verapdf), "--version"], r"veraPDF\s+([\d.]+)")
    code, out = run([str(verapdf), "-f", "0", "--format", "text", str(pdf)])
    passed = bool(re.search(r"^\s*PASS\b", out, re.M))
    failed = bool(re.search(r"^\s*FAIL\b", out, re.M))
    ok = code == 0 and passed and not failed
    return result("verapdf -f 0", ver, code, ok, [] if ok else interesting(out, 20, pdf))


def check_tagged(pdf: Path) -> dict:
    script = ROOT / "tools" / "structcheck.py"
    if not script.is_file():
        return result("structcheck", None, None, True, ["tools/structcheck.py ещё нет (сессия 8.4) — пропуск"], skipped=True)
    code, out = run([sys.executable, "-X", "utf8", "-I", str(script), str(pdf)])
    return result("structcheck", None, code, code == 0, [] if code == 0 else interesting(out))


# ------------------------------------------------------------------ main


def main(argv: list[str]) -> int:
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser(description="qpdf/pdfcpu/mutool/pdfium/pyHanko/veraPDF по PDF")
    ap.add_argument("files", nargs="+", type=Path, help="PDF или каталог с PDF")
    ap.add_argument("--json", type=Path, help="записать результат в JSON")
    ap.add_argument("--password", help="пароль (пользователя или владельца) зашифрованного файла")
    ap.add_argument("--forms", action="store_true", help="pypdfium2 рендерит с полями форм (--draw-forms)")
    ap.add_argument("--draw-forms", dest="forms", action="store_true", help=argparse.SUPPRESS)
    ap.add_argument("--expect-sig", help="ожидаемые токены сводки pyHanko: INTACT, TRUSTED, UNTOUCHED…")
    ap.add_argument("--trust", type=Path, action="append", default=[], help="доверенный корень (PEM/DER)")
    ap.add_argument("--pdfa", action="store_true", help="файл заявлен как PDF/A — veraPDF")
    ap.add_argument("--tagged", action="store_true", help="файл тегирован — tools/structcheck.py")
    ap.add_argument("--quiet", action="store_true", help="печатать только итог и упавшее")
    ap.add_argument("--expect-text", type=Path, help="JSON {имя файла: [строки]} — текст первой страницы (pypdfium2)")
    args = ap.parse_args(argv)
    files: list[Path] = []
    for p in args.files:
        if p.is_dir():
            found = sorted(p.glob("*.pdf"))
            if not found:
                print(f"compat: в каталоге {p} нет PDF")
                return 1
            files.extend(found)
        else:
            files.append(p)
    args.files = files
    expected_text: dict[str, list[str]] = {}
    if args.expect_text:
        expected_text = json.loads(args.expect_text.read_text(encoding="utf-8"))

    t = tools_dir()
    qpdf = find_tool(None, [t / "qpdf" / "bin" / "qpdf.exe"], ["qpdf"])
    if qpdf is None:
        print("compat: qpdf не найден (C:\\Tools\\qpdf\\bin\\qpdf.exe) — пропуск")
        return SKIP_CODE
    pdfcpu = find_tool(None, [t / "pdfcpu" / "pdfcpu.exe"], ["pdfcpu"])
    mutool = find_tool(
        "OTTISK_MUTOOL",
        [Path(os.environ.get("MUPDF_ROOT") or r"C:\src\mupdf") / "platform" / "win32" / "x64" / "Release" / "mutool.exe"],
        ["mutool"],
    )
    verapdf = find_tool(None, [t / "verapdf" / "verapdf.bat"], ["verapdf"]) if args.pdfa else None

    reports = []
    all_ok = True
    for pdf in args.files:
        pdf = pdf.resolve()
        started = time.monotonic()
        if not pdf.is_file():
            reports.append({"file": str(pdf), "ok": False, "checks": [result("file", None, None, False, ["файла нет"])]})
            all_ok = False
            print(f"FAIL {pdf}: файла нет")
            continue
        checks = [
            check_qpdf(qpdf, pdf, args.password),
            check_pdfcpu(pdfcpu, pdf, args.password),
            check_mutool(mutool, pdf, args.password),
            check_pdfium(pdf, args.password, args.forms),
        ]
        if args.expect_sig:
            checks.append(check_signatures(pdf, args.expect_sig, args.trust))
        if args.pdfa:
            checks.append(check_verapdf(verapdf, pdf))
        if args.tagged:
            checks.append(check_tagged(pdf))
        if pdf.name in expected_text:
            checks.append(check_text(pdf, args.password, expected_text[pdf.name]))
        ok = all(c["ok"] for c in checks)
        all_ok = all_ok and ok
        reports.append({"file": str(pdf), "ok": ok, "seconds": round(time.monotonic() - started, 2), "checks": checks})
        print(f"{'ok  ' if ok else 'FAIL'} {pdf.name}")
        for c in checks:
            state = "skip" if c["skipped"] else ("ok" if c["ok"] else "FAIL")
            if args.quiet and state != "FAIL":
                continue
            ver = f" {c['version']}" if c["version"] else ""
            print(f"  {state:4} {c['tool']}{ver} (код {c['exit']})")
            if state == "FAIL" or not args.quiet:
                for w in c["warnings"][:20]:
                    print(f"       {w}")

    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(json.dumps(reports, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    print(f"compat: {'зелёный' if all_ok else 'красный'} (файлов {len(args.files)})")
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
