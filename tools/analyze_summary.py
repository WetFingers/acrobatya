"""Сводка cl /analyze + CppCoreCheck (пресет msvc-analyze, эталон 9.5) и сверка с базой.

    "%OTTISK_PYTHON%" -X utf8 -I tools/analyze_summary.py [--build build/analyze] [--all] [--json out.json]
    "%OTTISK_PYTHON%" -X utf8 -I tools/analyze_summary.py --update-baseline --reason "почему"

Читает build/analyze/**/*.sarif (SARIF 2.1.0 от MSVC), берёт только наши файлы (внутри репозитория,
не build/), убирает повторы (один заголовок в нескольких TU) и печатает «правило · файл · строка · текст».
Сверка с docs/analyze-baseline.json идёт по (правило, файл, текст с числами → #) с учётом количества —
сдвиг строк не делает предупреждение «новым». --update-baseline переписывает базу текущими (причину —
ещё и строкой в docs/progress.md). Код 0 — новых нет, 1 — есть новые, 2 — SARIF нет (не собран msvc-analyze).
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import re
import sys
from collections import Counter
from pathlib import Path
from urllib.parse import unquote, urlparse

ROOT = Path(__file__).resolve().parent.parent


def uri_to_path(uri: str, base: str | None) -> Path | None:
    if not uri:
        return None
    if base and not re.match(r"^[a-zA-Z][a-zA-Z0-9+.-]*:", uri):
        uri = base.rstrip("/") + "/" + uri
    if uri.lower().startswith("file:"):
        parsed = urlparse(uri)
        path = unquote(parsed.path)
        if re.match(r"^/[A-Za-z]:", path):
            path = path[1:]
        return Path(path)
    return Path(unquote(uri))


def normalize(message: str) -> str:
    return re.sub(r"\d+", "#", re.sub(r"\s+", " ", message)).strip()


def collect(build: Path, root: Path) -> tuple[list[dict], int]:
    files = sorted(build.rglob("*.sarif")) if build.is_dir() else []
    seen: set[tuple] = set()
    results: list[dict] = []
    for f in files:
        try:
            data = json.loads(f.read_text(encoding="utf-8-sig"))
        except (json.JSONDecodeError, UnicodeDecodeError) as exc:
            print(f"analyze: не SARIF — пропуск {f}: {exc}")
            continue
        for run in data.get("runs", []):
            bases = {k: (v or {}).get("uri") for k, v in (run.get("originalUriBaseIds") or {}).items()}
            for res in run.get("results", []):
                if res.get("suppressions"):
                    continue
                rule = res.get("ruleId") or (res.get("rule") or {}).get("id") or "?"
                msg = ((res.get("message") or {}).get("text") or "").strip()
                locs = res.get("locations") or []
                if not locs:
                    continue
                phys = (locs[0] or {}).get("physicalLocation") or {}
                art = phys.get("artifactLocation") or {}
                # MSVC /analyze пишет не uri, а ссылку {"index": N} на run.artifacts[N].location
                if not art.get("uri") and isinstance(art.get("index"), int):
                    artifacts = run.get("artifacts") or []
                    if 0 <= art["index"] < len(artifacts):
                        art = (artifacts[art["index"]] or {}).get("location") or {}
                path = uri_to_path(art.get("uri", ""), bases.get(art.get("uriBaseId", "")))
                if path is None:
                    continue
                try:
                    rel = path.resolve().relative_to(root).as_posix()
                except (ValueError, OSError):
                    continue  # Qt, MuPDF, vcpkg, SDK — не наше
                if rel.startswith("build/"):
                    continue
                line = int((phys.get("region") or {}).get("startLine") or 0)
                key = (rule, rel, line, msg)
                if key in seen:
                    continue
                seen.add(key)
                results.append({"rule": rule, "file": rel, "line": line, "message": msg, "level": res.get("level", "warning")})
    results.sort(key=lambda r: (r["file"], r["line"], r["rule"]))
    return results, len(files)


def match_key(entry: dict) -> tuple[str, str, str]:
    return entry.get("rule", "?"), entry.get("file", "?"), normalize(entry.get("message", ""))


def main(argv: list[str]) -> int:
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    ap = argparse.ArgumentParser(description="Сводка SARIF msvc-analyze и сверка с docs/analyze-baseline.json")
    ap.add_argument("--root", type=Path, default=ROOT)
    ap.add_argument("--build", type=Path, default=Path("build/analyze"))
    ap.add_argument("--baseline", type=Path, default=Path("docs/analyze-baseline.json"))
    ap.add_argument("--update-baseline", action="store_true", help="записать текущие предупреждения как базу")
    ap.add_argument("--reason", default="", help="причина обновления базы (обязательна с --update-baseline)")
    ap.add_argument("--all", action="store_true", help="печатать все предупреждения, а не только новые")
    ap.add_argument("--json", type=Path, help="записать сводку в JSON")
    args = ap.parse_args(argv)
    root = args.root.resolve()
    build = args.build if args.build.is_absolute() else root / args.build
    baseline_path = args.baseline if args.baseline.is_absolute() else root / args.baseline

    results, n_files = collect(build, root)
    if n_files == 0:
        print(f"analyze: в {build} нет *.sarif — соберите пресет msvc-analyze (cmake --build --preset msvc-analyze)")
        return 2

    baseline = {"version": 1, "created": dt.date.today().isoformat(), "entries": []}
    if baseline_path.is_file():
        baseline = json.loads(baseline_path.read_text(encoding="utf-8"))
    known = Counter(match_key(e) for e in baseline.get("entries", []))
    remaining = Counter(known)
    new: list[dict] = []
    for r in results:
        k = match_key(r)
        if remaining[k] > 0:
            remaining[k] -= 1
        else:
            new.append(r)
    gone = sum(remaining.values())

    shown = results if args.all else new
    for r in shown:
        mark = "NEW " if r in new else "    "
        print(f"{mark}{r['rule']} · {r['file']} · {r['line']} · {r['message'][:160]}")
    by_rule = Counter(r["rule"] for r in results)
    top = ", ".join(f"{rule} ×{n}" for rule, n in by_rule.most_common(8))
    print(
        f"analyze: SARIF {n_files}, предупреждений {len(results)} (наших), в базе {sum(known.values())}, "
        f"новых {len(new)}, исчезло {gone}" + (f"; чаще всего: {top}" if top else "")
    )

    if args.json:
        args.json.parent.mkdir(parents=True, exist_ok=True)
        args.json.write_text(
            json.dumps({"results": results, "new": new, "gone": gone}, ensure_ascii=False, indent=1) + "\n",
            encoding="utf-8",
        )

    if args.update_baseline:
        if not args.reason.strip():
            print("analyze: --update-baseline требует --reason \"…\" (и строку в docs/progress.md)")
            return 2
        updated = {
            "version": 1,
            "created": baseline.get("created", dt.date.today().isoformat()),
            "updated": dt.date.today().isoformat(),
            "reason": args.reason.strip(),
            "entries": [{k: r[k] for k in ("rule", "file", "line", "message")} for r in results],
        }
        baseline_path.parent.mkdir(parents=True, exist_ok=True)
        baseline_path.write_text(json.dumps(updated, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
        shown = baseline_path.relative_to(root).as_posix() if baseline_path.is_relative_to(root) else baseline_path
        print(f"analyze: база обновлена — {len(results)} записей → {shown}")
        return 0
    if new:
        print("analyze: красный — новые предупреждения (исправить или, с доводом, --update-baseline --reason)")
        return 1
    print("analyze: зелёный — новых предупреждений нет")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
