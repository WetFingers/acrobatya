"""Покрытие объёма тестами (эталон 1.2, 9.1): какие ID docs/scope.json закрыты хотя бы одним тестом.

    "%OTTISK_PYTHON%" -X utf8 -I tools/scope_report.py --build build/msvc-debug|build/debug [--min-full 95]

Источники (после ctest):
  * build/<пресет>/gtest-xml/*.xml — gtest RecordProperty("feature", id): элемент
    <property name="feature" value="view-10"/> или атрибут feature="…" у <testcase>; значение может
    быть списком через запятую;
  * строки «OT_FEATURE:<id>» в <system-out> build/<пресет>/ctest.xml (QTest пишет только их; ещё
    ctest-full.xml от verify.py --full) и в тексте gtest-xml/*.xml.
Пишет build/<пресет>/features-covered.json, печатает «закрыто N из M (full)» и списки по категориям.
Код 0; с --min-full P — 1, если доля закрытых full ниже P %. Каталога/файлов нет — сообщение и 0
(с --min-full — 1).
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

LISTS = ("full", "partial", "nice", "out")
# Служебные ID самих тестов макроса (unit.features.macro пишет OT_FEATURE("test-00")) — не из каталога.
SERVICE_PREFIXES = ("test-",)
ID_RE = re.compile(r"^[a-z]+-\d{2}$")
OT_LINE_RE = re.compile(r"OT_FEATURE:([a-z]+-\d{2}(?:\s*,\s*[a-z]+-\d{2})*)")
PRESET_DIRS = {
    "msvc-debug": "debug",
    "msvc-release": "release",
    "msvc-asan": "asan",
    "msvc-fuzz": "fuzz",
    "msvc-analyze": "analyze",
}


def split_ids(value: str) -> list[str]:
    return [v.strip() for v in value.split(",") if v.strip()]


def add(covered: dict[str, set[str]], fid: str, test: str) -> None:
    covered.setdefault(fid, set()).add(test)


def scan_gtest_xml(path: Path, covered: dict[str, set[str]]) -> int:
    """gtest XML одного процесса (gtest_discover_tests … XML_OUTPUT_DIR). Возвращает число ID."""
    text = path.read_text(encoding="utf-8", errors="replace")
    before = sum(len(v) for v in covered.values())
    try:
        root = ET.fromstring(text)
    except ET.ParseError:
        root = None
    if root is not None:
        for case in root.iter("testcase"):
            name = f"{case.get('classname', '')}.{case.get('name', '')}".strip(".")
            if case.get("feature"):
                for fid in split_ids(case.get("feature", "")):
                    add(covered, fid, name)
            for prop in case.iter("property"):
                if prop.get("name") == "feature":
                    for fid in split_ids(prop.get("value", "")):
                        add(covered, fid, name)
            for out in list(case.iter("system-out")) + list(case.iter("system-err")):
                for m in OT_LINE_RE.finditer(out.text or ""):
                    for fid in split_ids(m.group(1)):
                        add(covered, fid, name)
    else:
        # Обрезанный XML (тест упал на середине записи) — достаём что можно регулярками.
        for m in re.finditer(r'<property\s+name="feature"\s+value="([^"]*)"', text):
            for fid in split_ids(m.group(1)):
                add(covered, fid, path.stem)
    for m in OT_LINE_RE.finditer(text):
        for fid in split_ids(m.group(1)):
            add(covered, fid, path.stem)
    return sum(len(v) for v in covered.values()) - before


def scan_ctest_junit(path: Path, covered: dict[str, set[str]]) -> int:
    text = path.read_text(encoding="utf-8", errors="replace")
    before = sum(len(v) for v in covered.values())
    try:
        root = ET.fromstring(text)
        for case in root.iter("testcase"):
            name = case.get("name", "?")
            if case.get("status") in ("notrun", "disabled"):
                continue
            for out in list(case.iter("system-out")) + list(case.iter("system-err")):
                for m in OT_LINE_RE.finditer(out.text or ""):
                    for fid in split_ids(m.group(1)):
                        add(covered, fid, name)
    except ET.ParseError:
        for m in OT_LINE_RE.finditer(text):
            for fid in split_ids(m.group(1)):
                add(covered, fid, path.stem)
    return sum(len(v) for v in covered.values()) - before


def resolve_build(root: Path, value: str) -> Path:
    if value in PRESET_DIRS:
        return root / "build" / PRESET_DIRS[value]
    p = Path(value)
    if not p.is_absolute():
        p = root / p
    if not p.exists() and p.name in PRESET_DIRS:
        p = p.parent / PRESET_DIRS[p.name]
    return p


def main(argv: list[str]) -> int:
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser(description="Покрытие ID docs/scope.json тестами (OT_FEATURE)")
    ap.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    ap.add_argument("--build", default="build/debug", help="build/<каталог> или имя пресета (msvc-debug)")
    ap.add_argument("--min-full", type=float, default=None, help="порог доли закрытых full, %%")
    ap.add_argument("--quiet", action="store_true", help="без списков незакрытых ID")
    args = ap.parse_args(argv)
    root = args.root.resolve()
    build = resolve_build(root, args.build)

    scope = json.loads((root / "docs" / "scope.json").read_text(encoding="utf-8"))
    owner = {fid: name for name in LISTS for fid in scope.get(name, [])}

    covered: dict[str, set[str]] = {}
    sources: dict[str, int] = {}
    gtest_dir = build / "gtest-xml"
    xml_files = sorted(gtest_dir.glob("*.xml")) if gtest_dir.is_dir() else []
    for f in xml_files:
        scan_gtest_xml(f, covered)
    sources["gtest-xml"] = len(xml_files)
    junit_files = [p for p in (build / "ctest.xml", build / "ctest-full.xml") if p.is_file()]
    for junit in junit_files:
        scan_ctest_junit(junit, covered)
    sources["ctest.xml"] = len(junit_files)

    if not xml_files and not junit_files:
        print(f"scope_report: в {build} нет gtest-xml/*.xml и ctest.xml — сначала ctest (tools/verify.py)")
        return 1 if args.min_full is not None else 0

    bad_format = sorted(fid for fid in covered if not ID_RE.match(fid))
    service = sorted(fid for fid in covered if fid.startswith(SERVICE_PREFIXES))
    unknown = sorted(fid for fid in covered if ID_RE.match(fid) and fid not in owner and fid not in service)
    by_cat: dict[str, dict[str, list[str]]] = {}
    for name in LISTS:
        ids = scope.get(name, [])
        by_cat[name] = {
            "covered": [fid for fid in ids if fid in covered],
            "missing": [fid for fid in ids if fid not in covered],
        }
    full_total = len(scope.get("full", []))
    full_done = len(by_cat["full"]["covered"])
    full_pct = 100.0 * full_done / full_total if full_total else 100.0

    out = {
        "generated": dt.datetime.now().astimezone().isoformat(timespec="seconds"),
        "build": build.relative_to(root).as_posix() if build.is_relative_to(root) else str(build),
        "sources": sources,
        "full_percent": round(full_pct, 1),
        "summary": {name: {"covered": len(v["covered"]), "total": len(scope.get(name, []))} for name, v in by_cat.items()},
        "categories": by_cat,
        "out_with_tests": by_cat["out"]["covered"],
        "unknown": unknown,
        "service": service,
        "bad_format": bad_format,
        "tests": {fid: sorted(tests) for fid, tests in sorted(covered.items())},
    }
    build.mkdir(parents=True, exist_ok=True)
    target = build / "features-covered.json"
    target.write_text(json.dumps(out, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")

    print(f"закрыто {full_done} из {full_total} (full) — {full_pct:.1f} %")
    for name in ("partial", "nice"):
        print(f"закрыто {len(by_cat[name]['covered'])} из {len(scope.get(name, []))} ({name})")
    print(f"out: {len(scope.get('out', []))} ID вне первой версии — не потеря")
    if not args.quiet:
        for name in ("full", "partial", "nice"):
            missing = by_cat[name]["missing"]
            if missing:
                print(f"без теста ({name}, {len(missing)}): {', '.join(missing)}")
    if by_cat["out"]["covered"]:
        print(f"ВНИМАНИЕ: OT_FEATURE на ID из out (не ставится): {', '.join(by_cat['out']['covered'])}")
    if unknown:
        print(f"ВНИМАНИЕ: ID не из docs/scope.json: {', '.join(unknown)}")
    if bad_format:
        print(f"ВНИМАНИЕ: не похоже на ID каталога: {', '.join(bad_format)}")
    print(f"записано: {target.relative_to(root).as_posix() if target.is_relative_to(root) else target}")
    if args.min_full is not None and full_pct < args.min_full:
        print(f"scope_report: красный — full {full_pct:.1f} % < {args.min_full:g} %")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
