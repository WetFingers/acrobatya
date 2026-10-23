"""Сравнение замеров производительности с базой (эталон 5.8): «падение» = медиана хуже базы больше чем на 25 %.

    "%OTTISK_PYTHON%" -X utf8 -I tools/bench_compare.py docs/bench/baseline-r1.json build/bench/*.json
        [--threshold 25] [--json build/bench/compare.json] [--write-base docs/bench/baseline-r2.json] [--strict]
    "%OTTISK_PYTHON%" -X utf8 -I tools/bench_compare.py --base docs/bench/baseline-r1.json --current build/bench

Позиционно (как в плане и SKILL.md): первый — база, остальные — текущие замеры; маски («build/bench/*.json»)
раскрываются здесь же — PowerShell их для внешних программ не раскрывает. --base/--current — то же явно.
Каждый источник — файл, каталог (все *.json и *.xml внутри, рекурсивно) или маска. Метрика, встреченная в
нескольких файлах (повторные прогоны bench.exe), берётся медианой по ним. Понимает:
  * плоский JSON bench.exe: {"file": "…", "build": "release", "open_first_ms": 12.3, "next_page_ms": 4.5, …} —
    метрики — числовые ключи с суффиксом _ms/_s/_us/_mb/_kb/_bytes, имя «<имя файла>/<ключ>»;
  * свой формат: {"benchmarks": [{"name": "…", "unit": "ms", "samples": [...] | "median": x | "value": x,
    "higher_is_better": false}]} — медиана считается по samples;
  * Google Benchmark JSON ({"benchmarks": [{"name", "real_time", "time_unit", "aggregate_name"}]});
  * XML QTest (-o file.xml,xml): <TestFunction name><BenchmarkResult metric value iterations/>.
Меньше — лучше (кроме higher_is_better). Код 0 — падений нет (или базы нет), 1 — есть падение
(с --strict ещё: метрика из базы пропала), 2 — текущих замеров нет / не читаются.
"""

from __future__ import annotations

import argparse
import datetime as dt
import glob
import json
import re
import statistics
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

METRIC_SUFFIX_RE = re.compile(r"_(ms|s|sec|us|ns|mb|kb|bytes)$")


class Metric:
    def __init__(self, name: str, value: float, unit: str, higher_is_better: bool = False, source: str = "") -> None:
        self.name = name
        self.value = value
        self.unit = unit
        self.higher_is_better = higher_is_better
        self.source = source


def _median(values: list[float]) -> float:
    return float(statistics.median(values))


def parse_json(path: Path, data: object) -> list[Metric]:
    out: list[Metric] = []
    src = path.name
    if isinstance(data, list):
        for item in data:
            out.extend(parse_json(path, item))
        return out
    if not isinstance(data, dict):
        return out
    benches = data.get("benchmarks")
    if isinstance(benches, list):
        medians = {b.get("run_name") or b.get("name") for b in benches if isinstance(b, dict) and b.get("aggregate_name") == "median"}
        for b in benches:
            if not isinstance(b, dict) or "name" not in b:
                continue
            hib = bool(b.get("higher_is_better", False))
            if "real_time" in b:  # Google Benchmark
                base_name = b.get("run_name") or b["name"]
                if base_name in medians and b.get("aggregate_name") != "median":
                    continue
                if b.get("aggregate_name") not in (None, "median"):
                    continue
                out.append(Metric(base_name, float(b["real_time"]), b.get("time_unit", "ns"), hib, src))
                continue
            if isinstance(b.get("samples"), list) and b["samples"]:
                value = _median([float(x) for x in b["samples"]])
            elif "median" in b:
                value = float(b["median"])
            elif "value" in b:
                value = float(b["value"])
            else:
                continue
            out.append(Metric(str(b["name"]), value, str(b.get("unit", "")), hib, src))
        return out
    # Плоский JSON bench.exe.
    prefix = Path(str(data.get("file") or data.get("name") or path.stem)).name
    for key, value in data.items():
        if isinstance(value, (int, float)) and not isinstance(value, bool) and METRIC_SUFFIX_RE.search(key):
            unit = METRIC_SUFFIX_RE.search(key).group(1)
            out.append(Metric(f"{prefix}/{key}", float(value), unit, False, src))
        elif isinstance(value, dict) and key not in ("meta", "machine"):
            for sub in parse_json(path, {**value, "file": f"{prefix}/{key}"}):
                out.append(sub)
    return out


def parse_qtest_xml(path: Path) -> list[Metric]:
    out: list[Metric] = []
    try:
        root = ET.parse(path).getroot()
    except ET.ParseError:
        return out
    case = root.get("name", path.stem)
    for fn in root.iter("TestFunction"):
        for res in fn.iter("BenchmarkResult"):
            try:
                value = float(res.get("value", "nan"))
            except ValueError:
                continue
            tag = res.get("tag") or ""
            name = f"{case}::{fn.get('name')}" + (f"[{tag}]" if tag else "")
            out.append(Metric(name, value, res.get("metric", ""), False, path.name))
    return out


def expand(targets: list[Path]) -> list[Path]:
    """Файлы замеров из списка файлов/каталогов/масок (без compare.json — вывода самого скрипта)."""
    files: list[Path] = []
    for target in targets:
        text = str(target)
        if any(ch in text for ch in "*?["):
            found = sorted(Path(x) for x in glob.glob(text, recursive=True))
        elif target.is_dir():
            found = sorted(p for p in target.rglob("*") if p.suffix.lower() in (".json", ".xml"))
        else:
            found = [target] if target.is_file() else []
        for f in found:
            if f.is_file() and f.name != "compare.json" and f not in files:
                files.append(f)
    return files


def load(targets: Path | list[Path]) -> dict[str, Metric]:
    files = expand(targets if isinstance(targets, list) else [targets])
    if not files:
        return {}
    collected: dict[str, list[Metric]] = {}
    for f in files:
        if f.suffix.lower() == ".xml":
            items = parse_qtest_xml(f)
        else:
            try:
                items = parse_json(f, json.loads(f.read_text(encoding="utf-8")))
            except (json.JSONDecodeError, UnicodeDecodeError, ValueError):
                print(f"bench_compare: не JSON — пропуск: {f}")
                continue
        for m in items:
            collected.setdefault(m.name, []).append(m)
    metrics: dict[str, Metric] = {}
    for name, group in collected.items():
        first = group[0]
        value = _median([m.value for m in group]) if len(group) > 1 else first.value
        metrics[name] = Metric(name, value, first.unit, first.higher_is_better, first.source)
    return metrics


def write_base(path: Path, current: dict[str, Metric], source: str) -> None:
    data = {
        "version": 1,
        "created": dt.date.today().isoformat(),
        "source": source,
        "benchmarks": [
            {"name": m.name, "value": round(m.value, 4), "unit": m.unit}
            | ({"higher_is_better": True} if m.higher_is_better else {})
            for m in sorted(current.values(), key=lambda m: m.name)
        ],
    }
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")


def main(argv: list[str]) -> int:
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser(description="Сравнить замеры bench с базой (+25 %% медианы — красный)")
    ap.add_argument("sources", nargs="*", type=Path,
                    help="позиционно: <база> <текущие…> (файлы, каталоги или маски build/bench/*.json)")
    ap.add_argument("--base", type=Path, help="база: файл или каталог (docs/bench/baseline-*.json)")
    ap.add_argument("--current", type=Path, action="append", help="текущие замеры: файл, каталог или маска")
    ap.add_argument("--threshold", type=float, default=25.0, help="допуск ухудшения, %% (по умолчанию 25)")
    ap.add_argument("--json", type=Path, help="записать сравнение в JSON")
    ap.add_argument("--write-base", type=Path, help="записать текущие замеры как новую базу")
    ap.add_argument("--strict", action="store_true", help="красный и тогда, когда метрика базы пропала")
    args = ap.parse_args(argv)
    sources = list(args.sources)
    if args.base is None and sources:
        args.base = sources.pop(0)
    current_targets = (args.current or []) + sources or [Path("build/bench")]
    shown_current = " ".join(str(t) for t in current_targets)

    current = load(current_targets)
    if not current:
        print(f"bench_compare: в {shown_current} нет замеров (bench.exe --json …, QTest -o …,xml)")
        return 2
    if args.write_base:
        write_base(args.write_base, current, shown_current)
        print(f"база записана: {args.write_base} ({len(current)} метрик)")

    if not args.base:
        print(f"bench_compare: база не задана — текущих метрик {len(current)}")
        return 0
    base = load(args.base)
    if not base:
        print(f"bench_compare: базы {args.base} нет — сравнивать не с чем (первую базу пишет --write-base)")
        return 0

    rows = []
    regressions = 0
    missing = []
    limit = 1.0 + args.threshold / 100.0
    for name in sorted(set(base) | set(current)):
        b, c = base.get(name), current.get(name)
        if b is None:
            rows.append({"name": name, "base": None, "current": c.value, "unit": c.unit, "status": "new"})
            continue
        if c is None:
            missing.append(name)
            rows.append({"name": name, "base": b.value, "current": None, "unit": b.unit, "status": "missing"})
            continue
        if b.value <= 0:
            ratio = 1.0 if c.value <= 0 else float("inf")
        else:
            ratio = c.value / b.value
        worse = (1.0 / ratio if ratio > 0 else float("inf")) if b.higher_is_better else ratio
        status = "REGRESSION" if worse > limit else ("better" if worse < 1.0 / limit else "ok")
        if status == "REGRESSION":
            regressions += 1
        rows.append({"name": name, "base": b.value, "current": c.value, "unit": c.unit or b.unit,
                     "change_pct": round((ratio - 1.0) * 100.0, 1), "status": status})

    width = max(len(r["name"]) for r in rows)
    print(f"{'метрика':<{width}}  {'база':>10}  {'сейчас':>10}  {'Δ':>8}  итог")
    for r in rows:
        bs = f"{r['base']:.2f}" if r["base"] is not None else "—"
        cs = f"{r['current']:.2f}" if r["current"] is not None else "—"
        ch = f"{r['change_pct']:+.1f}%" if "change_pct" in r else ""
        print(f"{r['name']:<{width}}  {bs:>10}  {cs:>10}  {ch:>8}  {r['status']} {r['unit']}")
    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(
            json.dumps({"threshold_pct": args.threshold, "rows": rows}, ensure_ascii=False, indent=1) + "\n",
            encoding="utf-8",
        )
    failed = regressions > 0 or (args.strict and missing)
    print(
        f"bench_compare: {'красный' if failed else 'зелёный'} — падений {regressions} (порог +{args.threshold:g} %), "
        f"пропало метрик {len(missing)}, всего {len(rows)}"
    )
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
