#!/usr/bin/env python3
"""Корпуса тестовых PDF «Оттиска»: загрузка и сверка по tests/corpus/manifest.json.

Запуск — только через venv тестов, в режиме UTF-8 и с изоляцией:
    "%OTTISK_PYTHON%" -X utf8 -I tools/fetch_corpus.py [--only tag:a,b | id:x,y] [--verify] [--list]
                                                        [--files tag:x] [--repin] [--strict]

Каталог корпусов: --root, иначе %OTTISK_CORPUS%, иначе поле "root" манифеста
(%LOCALAPPDATA%/ottisk/corpus). Каждая запись манифеста живёт в своём подкаталоге <root>/<id>/;
подкаталог gen/ принадлежит генераторам tools/gen/ и этим скриптом не трогается.

Виды записей манифеста (эталон 9.2):
  git    — {"git": url, "rev": sha, "sparse": [образцы], "cone": false}: git clone --depth 1
           --filter=blob:none --sparse, затем checkout ровно rev; сверка — rev, чистый
           git status, число и общий размер файлов ("count", "size");
  url    — {"url", "file", "sha256", "size"}: curl.exe; "unpack": "zip" — распаковать в files/
           ("unpacked_count", "unpacked_size"); "mirror" — копия в репозитории на случай, если
           источник сменил файл; "mutable": true — источник обновляется, sha256 — снимок "snapshot";
  items  — набор файлов одного источника: {"items": [{"url", "file", "sha256", "size", "tags"?,
           "expect"?, "mirror"?}]}, "pace" — пауза между запросами (секунды);
  from   — выборка из скачанной записи: {"from": id, "license" (= у источника), "include": [glob], "files": [пути],
           "exclude": [glob], "count": N, "expect_files": {путь: expect}} — ничего не качает;
  generated — {"generated": true, "generator": "tools/gen/…", "paths": ["repo:…", "corpus:gen/…"]}:
           файлы делают генераторы; нет файлов — «ожидает генератор» (ошибка только при --strict).

После загрузки и сверки пишется <root>/index.json: {"tags": {тег: [{"path", "rel", "entry",
"expect", "license"}]}, "entries": {id: {"status", "files"}}} — его читают тесты engine.*,
render.* и скрипты рубежей; то же по одному тегу печатает --files tag:<тег>.

--verify сверяет sha256/rev каждой записи и подтверждает каждый тег, упомянутый в docs/plan.md
(три образца: «tag:a,b», «корпус `…`», «тег/теги/тега/тегам `…`»), с тегами манифеста; печатает
таблицу «тег → записей, файлов, скачано». Коды выхода: 0 — всё сошлось; 1 — есть ошибки;
2 — неверные аргументы или манифест.
"""

from __future__ import annotations

import argparse
import datetime as _dt
import fnmatch
import glob
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
import urllib.parse
import zipfile

# Теги корпусов — ровно список эталона design.md 9.2 (тега render нет и не будет).
TAGS_92 = [
    "annots", "forms", "xfa",
    "enc-r2", "enc-r3", "enc-r4", "enc-r5", "enc-r6",
    "sig-b", "sig-t", "sig-lt", "sig-lta",
    "pdfa-pass", "pdfa-fail",
    "big", "cyr", "fuzz", "office", "scan", "text-edit", "tagged", "trust",
]
FORBIDDEN_TAGS = {"render"}
EXPECT_RE = re.compile(r"^(open|broken-ok|data|password:.*|sig:[A-Z_]+(\|[A-Z_]+)*)$")
USER_AGENT = "ottisk-corpus/1 (test corpus fetcher; curl)"
DEFAULT_MAX_BYTES = 3_000_000_000  # этап 0: «не больше ~3 ГБ» на одну загрузку
UNZIP_LIMIT = 4_000_000_000
INVALID_WIN_CHARS = re.compile(r'[<>:"|?*\x00-\x1f]')

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


# ----------------------------------------------------------------------------- утилиты

def say(*parts: object) -> None:
    print(*parts, flush=True)


def human(n: int | None) -> str:
    if n is None:
        return "?"
    v = float(n)
    for unit in ("Б", "КБ", "МБ", "ГБ"):
        if v < 1024 or unit == "ГБ":
            return f"{v:.0f} {unit}" if unit == "Б" else f"{v:.1f} {unit}"
        v /= 1024
    return str(n)


def sha256_of(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def find_curl() -> str:
    system = os.path.join(os.environ.get("SystemRoot", r"C:\Windows"), "System32", "curl.exe")
    found = shutil.which("curl.exe") or shutil.which("curl")
    if os.path.isfile(system):
        return system
    if found:
        return found
    raise SystemExit("fetch_corpus: curl.exe не найден")


def run(cmd: list[str], cwd: str | None = None) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, encoding="utf-8", errors="replace")


def git(args: list[str], cwd: str | None = None) -> subprocess.CompletedProcess:
    base = ["git", "-c", "core.longpaths=true", "-c", "core.autocrlf=false", "-c", "advice.detachedHead=false"]
    return run(base + args, cwd=cwd)


def walk_files(top: str, skip_git: bool = True) -> list[str]:
    """Все файлы под top (относительные пути через «/», по алфавиту), без .git."""
    out = []
    for dp, dn, fn in os.walk(top):
        if skip_git:
            dn[:] = [d for d in dn if d != ".git"]
        for f in fn:
            out.append(os.path.relpath(os.path.join(dp, f), top).replace(os.sep, "/"))
    return sorted(out)


def tree_stats(top: str) -> tuple[int, int]:
    files = walk_files(top)
    return len(files), sum(os.path.getsize(os.path.join(top, f)) for f in files)


# ----------------------------------------------------------------------------- манифест

def entry_kind(e: dict) -> str:
    if e.get("generated"):
        return "generated"
    if "from" in e:
        return "from"
    if "git" in e:
        return "git"
    if "items" in e:
        return "items"
    if "url" in e:
        return "url"
    return "?"


def load_manifest(path: str) -> dict:
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def save_manifest(path: str, m: dict) -> None:
    text = json.dumps(m, ensure_ascii=False, indent=2) + "\n"
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    os.replace(tmp, path)


def validate_manifest(m: dict) -> list[str]:
    errs = []
    if m.get("version") != 1:
        errs.append("version должен быть 1")
    ids = {}
    for i, e in enumerate(m.get("entries", [])):
        eid = e.get("id")
        where = f"entries[{i}] ({eid})"
        if not eid or not re.match(r"^[a-z0-9][a-z0-9._-]*$", eid):
            errs.append(f"{where}: id пуст или не из [a-z0-9._-]")
        if eid in ids:
            errs.append(f"{where}: id повторяется")
        if eid == "gen":
            errs.append(f"{where}: id gen занят каталогом генераторов")
        ids[eid] = e
        kind = entry_kind(e)
        if kind == "?":
            errs.append(f"{where}: не понять вид записи (нужно git/url/items/from/generated)")
        if not e.get("license"):
            errs.append(f"{where}: нет license")
        tags = list(e.get("tags", []))
        for it in e.get("items", []):
            tags += it.get("tags", [])
        for t in tags:
            if t in FORBIDDEN_TAGS:
                errs.append(f"{where}: тег {t} запрещён (эталон 9.2)")
            elif t not in TAGS_92:
                errs.append(f"{where}: тег {t} не из списка эталона 9.2")
        expects = [e.get("expect", "open")] + list(e.get("expect_files", {}).values())
        expects += [it.get("expect", "open") for it in e.get("items", [])]
        for x in expects:
            if not EXPECT_RE.match(str(x)):
                errs.append(f"{where}: непонятное expect {x!r}")
        if kind == "git" and not re.match(r"^[0-9a-f]{40}$", e.get("rev", "")):
            errs.append(f"{where}: rev должен быть полным sha коммита")
        if kind == "url" and not e.get("file"):
            errs.append(f"{where}: нет file")
        if kind == "items":
            names = [it.get("file") for it in e["items"]]
            if len(set(names)) != len(names) or not all(names):
                errs.append(f"{where}: file у items пусты или повторяются")
    for eid, e in ids.items():
        if entry_kind(e) == "from":
            src = ids.get(e["from"])
            if src is None:
                errs.append(f"{eid}: from → нет записи {e['from']}")
            elif entry_kind(src) not in ("git", "url", "items"):
                errs.append(f"{eid}: from → {e['from']} не скачиваемая запись")
            elif e.get("license") != src.get("license"):
                errs.append(f"{eid}: license {e.get('license')!r} ≠ license источника {src.get('license')!r}")
            if not e.get("include") and not e.get("files"):
                errs.append(f"{eid}: у выборки нет include/files")
    return errs


def resolve_root(arg_root: str | None, m: dict) -> str:
    if arg_root:
        return os.path.abspath(arg_root)
    env = os.environ.get("OTTISK_CORPUS")
    if env:
        return os.path.abspath(env)
    raw = m.get("root", "%LOCALAPPDATA%/ottisk/corpus")
    expanded = os.path.expandvars(raw)
    if "%" in expanded or "$" in expanded:
        raise SystemExit(f"fetch_corpus: не раскрыть root {raw!r}")
    return os.path.abspath(expanded)


# ----------------------------------------------------------------------------- выбор записей

def parse_only(specs: list[str] | None, m: dict) -> tuple[set[str] | None, set[str] | None, list[str]]:
    """--only tag:a,b / id:x,y → (ids, tags, ошибки). None — выбрано всё."""
    if not specs:
        return None, None, []
    ids: set[str] = set()
    tags: set[str] = set()
    errs = []
    by_id = {e["id"]: e for e in m["entries"]}
    for spec in specs:
        for part in spec.split(";"):
            part = part.strip()
            if not part:
                continue
            if part.startswith("tag:"):
                for t in part[4:].split(","):
                    t = t.strip()
                    if t:
                        tags.add(t)
            elif part.startswith("id:"):
                for i in part[3:].split(","):
                    i = i.strip()
                    if i and i not in by_id:
                        errs.append(f"--only: нет записи {i}")
                    elif i:
                        ids.add(i)
            else:
                errs.append(f"--only: ожидается tag:… или id:…, а не {part!r}")
    for t in tags:
        if t not in TAGS_92:
            errs.append(f"--only: тег {t} не из списка эталона 9.2")
    return ids, tags, errs


def entry_tags(e: dict) -> set[str]:
    t = set(e.get("tags", []))
    for it in e.get("items", []):
        t |= set(it.get("tags", []))
    return t


def selected_entries(m: dict, ids: set[str] | None, tags: set[str] | None) -> list[dict]:
    if ids is None and tags is None:
        return list(m["entries"])
    by_id = {e["id"]: e for e in m["entries"]}
    chosen: set[str] = set(ids or ())
    for e in m["entries"]:
        if tags and entry_tags(e) & tags:
            chosen.add(e["id"])
    for eid in list(chosen):  # выборке нужен её источник
        e = by_id[eid]
        if entry_kind(e) == "from":
            chosen.add(e["from"])
    return [e for e in m["entries"] if e["id"] in chosen]


# ----------------------------------------------------------------------------- загрузка

class Fetcher:
    def __init__(self, root: str, curl: str, dry: bool) -> None:
        self.root = root
        self.curl = curl
        self.dry = dry
        self.last_hit: dict[str, float] = {}
        self.downloaded = 0

    # --- url ---
    def _pace(self, url: str, pace: float) -> None:
        host = urllib.parse.urlsplit(url).hostname or ""
        if pace <= 0:
            return
        wait = self.last_hit.get(host, 0) + pace - time.time()
        if wait > 0:
            time.sleep(wait)
        self.last_hit[host] = time.time()

    def download(self, url: str, dest: str, pace: float = 0.0) -> None:
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        part = dest + ".part"
        hdr = dest + ".hdr"
        for attempt in range(1, 6):
            self._pace(url, pace)
            resume = os.path.exists(part) and os.path.getsize(part) > (1 << 20)
            cmd = [self.curl, "-L", "-sS", "--connect-timeout", "30", "--retry", "3", "--retry-delay", "5",
                   "-A", USER_AGENT, "-D", hdr, "-o", part, "-w", "%{http_code}"]
            if resume:
                cmd += ["-C", "-"]
            cmd.append(url)
            r = run(cmd)
            code = (r.stdout or "").strip()[-3:]
            if code in ("200", "206") and r.returncode == 0:
                break
            retry_after = 0
            try:
                with open(hdr, encoding="latin-1") as f:
                    for line in f:
                        if line.lower().startswith("retry-after:"):
                            v = line.split(":", 1)[1].strip()
                            retry_after = int(v) if v.isdigit() else 60
            except OSError:
                pass
            if code == "416" and resume:  # уже докачано целиком
                break
            if os.path.exists(part) and (resume or code not in ("200", "206")):
                os.remove(part)
            if code in ("429", "503") and attempt < 5:
                wait = min(max(retry_after, 30 * attempt), 900)
                say(f"    {code} от сервера, жду {wait} с (попытка {attempt})")
                time.sleep(wait)
                continue
            if attempt < 3 and code not in ("404", "403", "410"):
                time.sleep(5 * attempt)
                continue
            raise RuntimeError(f"curl: HTTP {code or '—'} код {r.returncode} {r.stderr.strip()[:200]}")
        if os.path.exists(hdr):
            os.remove(hdr)
        os.replace(part, dest)
        self.downloaded += os.path.getsize(dest)

    def fetch_file(self, eid: str, spec: dict, dest: str, pace: float, mutable: bool) -> str:
        """Скачать один файл и сверить sha256. Возвращает итог для печати."""
        want = spec.get("sha256")
        if os.path.exists(dest) and want and sha256_of(dest) == want:
            return "совпадает"
        if self.dry:
            return "будет скачан"
        err = None
        try:
            self.download(spec["url"], dest, pace)
        except RuntimeError as ex:
            err = str(ex)
        got = sha256_of(dest) if os.path.exists(dest) else None
        if err is None and (not want or got == want):
            return "скачан" if want else "скачан (sha256 не закреплён)"
        mirror = spec.get("mirror")
        if mirror:
            mp = os.path.join(REPO, mirror)
            if os.path.isfile(mp) and sha256_of(mp) == want:
                shutil.copyfile(mp, dest)
                return f"взят из копии {mirror} (источник: {err or 'другой sha256'})"
        if err:
            raise RuntimeError(err)
        bad = dest + ".new"
        os.replace(dest, bad)
        hint = (" — источник обновляется; принять новый снимок: --repin --only id:" + eid) if mutable else ""
        raise RuntimeError(f"sha256 не совпадает: ждали {want[:12]}…, пришёл {got[:12]}… (сохранён {bad}){hint}")

    # --- zip ---
    @staticmethod
    def unzip(zpath: str, outdir: str) -> tuple[int, int]:
        tmp = outdir + ".tmp"
        if os.path.exists(tmp):
            shutil.rmtree(tmp)
        with zipfile.ZipFile(zpath) as z:
            infos = [i for i in z.infolist() if not i.is_dir()]
            total = sum(i.file_size for i in infos)
            if total > UNZIP_LIMIT:
                raise RuntimeError(f"архив раскрывается в {human(total)} — больше предела {human(UNZIP_LIMIT)}")
            seen = set()
            for info in infos:
                name = info.filename.replace("\\", "/")
                parts = [p for p in name.split("/") if p not in ("", ".")]
                if not parts or any(p == ".." for p in parts) or re.match(r"^[A-Za-z]:", name):
                    raise RuntimeError(f"подозрительное имя в архиве: {info.filename!r}")
                parts = [INVALID_WIN_CHARS.sub("_", p).rstrip(" .") or "_" for p in parts]
                rel = "/".join(parts)
                if rel.lower() in seen:
                    rel += f".dup{len(seen)}"
                seen.add(rel.lower())
                target = os.path.join(tmp, *rel.split("/"))
                os.makedirs(os.path.dirname(target), exist_ok=True)
                with z.open(info) as src, open(target, "wb") as dst:
                    shutil.copyfileobj(src, dst, 1 << 20)
        if os.path.exists(outdir):
            shutil.rmtree(outdir)
        for attempt in range(20):  # антивирус Windows ненадолго держит только что записанные файлы
            try:
                os.replace(tmp, outdir)
                break
            except PermissionError:
                if attempt == 19:
                    raise
                time.sleep(3)
        return tree_stats(outdir)

    # --- git ---
    def fetch_git(self, e: dict, d: str) -> str:
        url, rev = e["git"], e["rev"]
        sparse = e.get("sparse") or []
        cone = bool(e.get("cone", False))
        if os.path.isdir(os.path.join(d, ".git")):
            head = git(["rev-parse", "HEAD"], cwd=d).stdout.strip()
            cur_sparse = git(["sparse-checkout", "list"], cwd=d).stdout.split("\n")
            cur_sparse = [s for s in (x.strip() for x in cur_sparse) if s]
            clean = git(["status", "--porcelain", "--untracked-files=no"], cwd=d).stdout.strip() == ""
            if head == rev and cur_sparse == sparse and clean:
                return "совпадает"
            if self.dry:
                return "будет обновлён"
        else:
            if os.path.exists(d) and os.listdir(d):
                raise RuntimeError(f"{d} не пуст и не git — уберите вручную")
            if self.dry:
                return "будет склонирован"
            os.makedirs(os.path.dirname(d), exist_ok=True)
            r = git(["clone", "--depth", "1", "--filter=blob:none", "--sparse", "--no-checkout", url, d])
            if r.returncode != 0:
                raise RuntimeError("git clone: " + r.stderr.strip()[-300:])
            # никаких преобразований концов строк: PDF с «текстовым» видом иначе ломается
            with open(os.path.join(d, ".git", "info", "attributes"), "w", encoding="ascii") as f:
                f.write("* -text\n")
            for k, v in (("core.autocrlf", "false"), ("core.longpaths", "true")):
                git(["config", k, v], cwd=d)
        args = ["sparse-checkout", "set"] + ([] if cone else ["--no-cone"]) + sparse
        r = git(args, cwd=d)
        if r.returncode != 0:
            raise RuntimeError("git sparse-checkout: " + r.stderr.strip()[-300:])
        if git(["cat-file", "-e", rev + "^{commit}"], cwd=d).returncode != 0:
            r = git(["fetch", "--depth", "1", "--filter=blob:none", "origin", rev], cwd=d)
            if r.returncode != 0:
                raise RuntimeError(f"git fetch {rev[:12]}: " + r.stderr.strip()[-300:])
        r = git(["checkout", "--force", "--detach", rev], cwd=d)
        if r.returncode != 0:
            raise RuntimeError(f"git checkout {rev[:12]}: " + r.stderr.strip()[-300:])
        return "получен rev " + rev[:12]

    # --- запись целиком ---
    def fetch_entry(self, e: dict) -> str:
        kind = entry_kind(e)
        d = os.path.join(self.root, e["id"])
        if kind == "git":
            before = tree_stats(d)[1] if os.path.isdir(d) else 0
            res = self.fetch_git(e, d)
            if not self.dry and res != "совпадает":
                self.downloaded += max(0, tree_stats(d)[1] - before)
            return res
        if kind == "url":
            dest = os.path.join(d, e["file"])
            res = self.fetch_file(e["id"], e, dest, float(e.get("pace", 0)), bool(e.get("mutable")))
            if e.get("unpack") == "zip" and not self.dry:
                out = os.path.join(d, "files")
                n_want = e.get("unpacked_count")
                if res != "совпадает" or not os.path.isdir(out) or tree_stats(out)[0] != n_want:
                    n, size = self.unzip(dest, out)
                    res += f"; распакован: {n} файлов, {human(size)}"
            return res
        if kind == "items":
            results = []
            for it in e["items"]:
                dest = os.path.join(d, it["file"])
                results.append(self.fetch_file(e["id"], it, dest, float(e.get("pace", 0)), bool(e.get("mutable"))))
            summary = {}
            for r in results:
                summary[r] = summary.get(r, 0) + 1
            return ", ".join(f"{k}: {v}" for k, v in summary.items())
        if kind == "from":
            return "выборка из " + e["from"]
        if kind == "generated":
            return "делает генератор " + e.get("generator", "?")
        raise RuntimeError("неизвестный вид записи")


# ----------------------------------------------------------------------------- файлы записей и сверка

def glob_rel(base: str, patterns: list[str]) -> list[str]:
    out: set[str] = set()
    for p in patterns:
        for hit in glob.glob(p, root_dir=base, recursive=True):
            full = os.path.join(base, hit)
            if os.path.isfile(full):
                out.add(hit.replace(os.sep, "/"))
    return sorted(out)


def source_dir(root: str, e: dict) -> str:
    return os.path.join(root, e["id"])


def entry_files(root: str, e: dict, by_id: dict) -> list[tuple[str, str]]:
    """Файлы записи: [(абсолютный путь, путь от корня корпуса или repo:…)]."""
    return [(os.path.normpath(full), rel) for full, rel in _entry_files(root, e, by_id)]


def _entry_files(root: str, e: dict, by_id: dict) -> list[tuple[str, str]]:
    kind = entry_kind(e)
    if kind == "url":
        d = source_dir(root, e)
        if e.get("unpack") == "zip":
            out = os.path.join(d, "files")
            files = walk_files(out) if os.path.isdir(out) else []
            return [(os.path.join(out, f), f"{e['id']}/files/{f}") for f in files]
        return [(os.path.join(d, e["file"]), f"{e['id']}/{e['file']}")]
    if kind == "items":
        d = source_dir(root, e)
        return [(os.path.join(d, it["file"]), f"{e['id']}/{it['file']}") for it in e["items"]]
    if kind == "git":
        d = source_dir(root, e)
        if not os.path.isdir(d):
            return []
        rels = glob_rel(d, e["include"]) if e.get("include") else [f for f in walk_files(d) if f.lower().endswith(".pdf")]
        return [(os.path.join(d, r), f"{e['id']}/{r}") for r in rels]
    if kind == "from":
        src = by_id[e["from"]]
        d = source_dir(root, src)
        if not os.path.isdir(d):
            return []
        rels = set(glob_rel(d, e.get("include", [])))
        for f in e.get("files", []):
            rels.add(f)
        if e.get("exclude"):
            rels = {r for r in rels if not any(fnmatch.fnmatch(r, x) for x in e["exclude"])}
        return [(os.path.join(d, r), f"{src['id']}/{r}") for r in sorted(rels)]
    if kind == "generated":
        out = []
        for p in e.get("paths", []):
            if p.startswith("repo:"):
                base, pat, pref = REPO, p[5:], "repo:"
            elif p.startswith("corpus:"):
                base, pat, pref = root, p[7:], ""
            else:
                continue
            for r in glob_rel(base, [pat]):
                out.append((os.path.join(base, r), pref + r))
        return out
    return []


def file_records(root: str, e: dict, by_id: dict) -> list[dict]:
    """Файлы записи с тегами и ожиданием (для index.json и --files)."""
    recs = []
    lic = e.get("license") or by_id.get(e.get("from", ""), {}).get("license", "")
    exp_files = e.get("expect_files", {})
    items = {it["file"]: it for it in e.get("items", [])}
    for full, rel in entry_files(root, e, by_id):
        tags = set(e.get("tags", []))
        expect = e.get("expect", "open")
        short = rel.split("/", 1)[1] if "/" in rel and not rel.startswith("repo:") else rel
        it = items.get(short)
        if it:
            tags |= set(it.get("tags", []))
            expect = it.get("expect", expect)
        expect = exp_files.get(short, expect)
        if tags:
            rec = {"path": full, "rel": rel, "entry": e["id"], "tags": sorted(tags), "expect": expect, "license": lic}
            if e.get("edge"):
                rec["edge"] = True  # граничный случай: ожидание по спецификации, движок может не справиться
            recs.append(rec)
    return recs


def verify_entry(root: str, e: dict, by_id: dict, cache: dict) -> tuple[str, str, int]:
    """→ (статус ok|missing|bad|pending, пояснение, число файлов)."""
    if e["id"] in cache:
        return cache[e["id"]]
    kind = entry_kind(e)
    d = source_dir(root, e)
    res: tuple[str, str, int]
    try:
        if kind == "url":
            p = os.path.join(d, e["file"])
            if not os.path.isfile(p):
                res = ("missing", "не скачано", 0)
            elif e.get("sha256") and sha256_of(p) != e["sha256"]:
                res = ("bad", "sha256 не совпадает", 0)
            elif e.get("unpack") == "zip":
                out = os.path.join(d, "files")
                n, size = tree_stats(out) if os.path.isdir(out) else (0, 0)
                if n != e.get("unpacked_count") or size != e.get("unpacked_size"):
                    res = ("bad", f"распаковано {n} файлов / {size} Б, ждали {e.get('unpacked_count')} / {e.get('unpacked_size')}", n)
                else:
                    res = ("ok", f"sha256 сошёлся, {n} файлов", n)
            else:
                res = ("ok", "sha256 сошёлся", 1)
        elif kind == "items":
            missing = bad = 0
            for it in e["items"]:
                p = os.path.join(d, it["file"])
                if not os.path.isfile(p):
                    missing += 1
                elif it.get("sha256") and sha256_of(p) != it["sha256"]:
                    bad += 1
            n = len(e["items"])
            if bad:
                res = ("bad", f"sha256 не совпадает у {bad} из {n}", n - bad - missing)
            elif missing:
                res = ("missing", f"не скачано {missing} из {n}", n - missing)
            else:
                res = ("ok", f"sha256 сошёлся у {n}", n)
        elif kind == "git":
            if not os.path.isdir(os.path.join(d, ".git")):
                res = ("missing", "не склонировано", 0)
            else:
                head = git(["rev-parse", "HEAD"], cwd=d).stdout.strip()
                dirty = git(["status", "--porcelain", "--untracked-files=no"], cwd=d).stdout.strip()
                n, size = tree_stats(d)
                if head != e["rev"]:
                    res = ("bad", f"HEAD {head[:12]} ≠ rev {e['rev'][:12]}", n)
                elif dirty:
                    res = ("bad", "рабочее дерево изменено: " + dirty.splitlines()[0][:80], n)
                elif ("count" in e and n != e["count"]) or ("size" in e and size != e["size"]):
                    res = ("bad", f"{n} файлов / {size} Б, ждали {e.get('count')} / {e.get('size')}", n)
                else:
                    res = ("ok", f"rev {head[:12]}, {n} файлов", n)
        elif kind == "from":
            st, _, _ = verify_entry(root, by_id[e["from"]], by_id, cache)
            files = entry_files(root, e, by_id)
            present = [f for f, _ in files if os.path.isfile(f)]
            if st != "ok":
                res = ("missing" if st == "missing" else "bad", f"источник {e['from']}: {st}", len(present))
            elif len(present) != len(files):
                res = ("bad", f"нет {len(files) - len(present)} файлов из списка", len(present))
            elif "count" in e and len(present) != e["count"]:
                res = ("bad", f"выбрано {len(present)}, ждали {e['count']}", len(present))
            elif not present:
                res = ("bad", "выборка пуста", 0)
            else:
                res = ("ok", f"{len(present)} файлов из {e['from']}", len(present))
        elif kind == "generated":
            n = len(entry_files(root, e, by_id))
            need = int(e.get("min_count", 1))
            if n >= need:
                res = ("ok", f"{n} файлов генератора", n)
            else:
                res = ("pending", f"ожидает генератор {e.get('generator', '?')} ({n} из ≥{need})", n)
        else:
            res = ("bad", "неизвестный вид записи", 0)
    except Exception as ex:  # сверка не должна падать на одной записи
        res = ("bad", f"ошибка сверки: {ex}", 0)
    cache[e["id"]] = res
    return res


# ----------------------------------------------------------------------------- теги из плана

TAG_TOKEN = re.compile(r"^[a-z][a-z0-9]*(?:-[a-z0-9*]+)*$")


def expand_token(tok: str) -> list[str] | None:
    """Разворачивает «enc-r2…enc-r6», «enc-r2..r6», «sig-b/t/lt/lta», «enc-*»; None — не тег."""
    tok = tok.strip().strip(",.;:()")
    if not tok:
        return None
    m = re.match(r"^([a-z][a-z0-9-]*?)(\d+)(?:…|\.\.\.?)[a-z-]*?(\d+)$", tok)
    if m:
        a, b = int(m.group(2)), int(m.group(3))
        if a <= b and b - a < 20:
            return [f"{m.group(1)}{i}" for i in range(a, b + 1)]
    if "/" in tok:
        head, *rest = tok.split("/")
        if TAG_TOKEN.match(head) and "-" in head and all(re.match(r"^[a-z0-9]+$", r) for r in rest):
            pref = head.rsplit("-", 1)[0] + "-"
            return [head] + [pref + r for r in rest]
        return None
    if "*" in tok:
        if not TAG_TOKEN.match(tok.replace("*", "x")):
            return None
        return [t for t in TAGS_92 if fnmatch.fnmatch(t, tok)] or [tok]
    return [tok] if TAG_TOKEN.match(tok) else None


def plan_tags(plan_path: str) -> dict[str, int]:
    """Теги, упомянутые в плане по трём образцам → номер первой строки."""
    found: dict[str, int] = {}
    with open(plan_path, encoding="utf-8") as f:
        lines = f.read().splitlines()
    pat_tag = re.compile(r"tag:([A-Za-z0-9,*…._-]+)")
    pat_corpus = re.compile(r"корпус[а-яё]*\s+(?:по\s+)?(?:тег[а-яё]*\s+)?`([^`]+)`")
    pat_teg = re.compile(r"тег(?:и|а|у|ам|ом|ов|ами|ах)?\s+\(?((?:`[^`]+`[-а-яё]*(?:,\s*|\s+и\s+|\s*)?)+)")
    for ln, line in enumerate(lines, 1):
        groups: list[tuple[list[str], int]] = []
        for m in pat_tag.finditer(line):
            groups.append((m.group(1).split(","), m.end()))
        for m in pat_corpus.finditer(line):
            groups.append((re.split(r"[\s,]+", m.group(1)), m.end()))
        for m in pat_teg.finditer(line):
            for g in re.finditer(r"`([^`]+)`", m.group(1)):
                groups.append((re.split(r"[\s,]+", g.group(1)), m.start(1) + g.end()))
        for toks, end in groups:
            if re.match(r"\s+нет\b", line[end:end + 8]):  # «тега `render` нет»
                continue
            expanded: list[str] = []
            ok = True
            for t in toks:
                if not t.strip():
                    continue
                x = expand_token(t)
                if x is None:
                    ok = False
                    break
                expanded += x
            if not ok or not expanded:
                continue  # «тег `v0.1.0`», «корпус `…`», «тег `git tag -f …`» — не корпусные теги
            for t in expanded:
                found.setdefault(t, ln)
    return found


# ----------------------------------------------------------------------------- индекс

def write_index(root: str, m: dict, manifest_path: str, results: dict) -> str:
    by_id = {e["id"]: e for e in m["entries"]}
    tags: dict[str, list] = {t: [] for t in TAGS_92}
    entries = {}
    for e in m["entries"]:
        st, why, n = results[e["id"]]
        entries[e["id"]] = {"status": st, "detail": why, "files": n, "kind": entry_kind(e)}
        if st != "ok":
            continue
        for rec in file_records(root, e, by_id):
            for t in rec["tags"]:
                tags.setdefault(t, []).append({k: rec[k] for k in ("path", "rel", "entry", "expect", "license", "edge") if k in rec})
    for t in tags:
        tags[t].sort(key=lambda r: r["rel"])
    idx = {
        "version": 1,
        "generated": _dt.datetime.now().astimezone().isoformat(timespec="seconds"),
        "manifest": os.path.relpath(manifest_path, REPO).replace(os.sep, "/"),
        "manifest_sha256": sha256_of(manifest_path),
        "root": root,
        "entries": entries,
        "tags": tags,
    }
    path = os.path.join(root, "index.json")
    os.makedirs(root, exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        json.dump(idx, f, ensure_ascii=False, indent=1)
    os.replace(tmp, path)
    return path


# ----------------------------------------------------------------------------- repin

def repin(root: str, m: dict, sel: list[dict], fetcher: Fetcher, save=None) -> list[str]:
    """Закрепить sha256/size/count по тому, что лежит в корпусе; чего нет — скачать.

    Расходящийся с манифестом файл перекачивается и закрепляется заново (так принимается новый
    снимок изменчивого источника). Манифест сохраняется после каждой записи (save)."""
    by_id = {e["id"]: e for e in m["entries"]}
    today = _dt.date.today().isoformat()
    notes = []
    order = [e for e in sel if entry_kind(e) != "from"] + [e for e in sel if entry_kind(e) == "from"]
    for e in order:
        kind = entry_kind(e)
        d = source_dir(root, e)
        if kind in ("url", "items"):
            specs = [e] if kind == "url" else e["items"]
            for s in specs:
                dest = os.path.join(d, s["file"])
                if s.get("sha256") and os.path.isfile(dest) and sha256_of(dest) != s["sha256"]:
                    os.remove(dest)
                if not os.path.isfile(dest):
                    fetcher.download(s["url"], dest, float(e.get("pace", 0)))
                h, size = sha256_of(dest), os.path.getsize(dest)
                if s.get("sha256") != h or s.get("size") != size:
                    notes.append(f"{e['id']}/{s['file']}: sha256 {h[:12]}…, {size} Б")
                    s["sha256"], s["size"] = h, size
                    if e.get("mutable") or s.get("mutable"):
                        e["snapshot"] = today
            if kind == "url" and e.get("unpack") == "zip":
                n, size = Fetcher.unzip(os.path.join(d, e["file"]), os.path.join(d, "files"))
                e["unpacked_count"], e["unpacked_size"] = n, size
                notes.append(f"{e['id']}: распаковано {n} файлов, {size} Б")
        elif kind == "git":
            fetcher.fetch_git(e, d)
            n, size = tree_stats(d)
            if e.get("count") != n or e.get("size") != size:
                notes.append(f"{e['id']}: {n} файлов, {size} Б")
            e["count"], e["size"] = n, size
        elif kind == "from":
            files = [f for f, _ in entry_files(root, e, by_id) if os.path.isfile(f)]
            if e.get("count") != len(files):
                notes.append(f"{e['id']}: {len(files)} файлов")
            e["count"] = len(files)
        if save:
            save()
    return notes


# ----------------------------------------------------------------------------- main

def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Загрузка и сверка корпусов по tests/corpus/manifest.json")
    ap.add_argument("--manifest", default=os.path.join(REPO, "tests", "corpus", "manifest.json"))
    ap.add_argument("--plan", default=os.path.join(REPO, "docs", "plan.md"))
    ap.add_argument("--root", help="каталог корпусов (иначе %%OTTISK_CORPUS%% или root манифеста)")
    ap.add_argument("--only", action="append", help="tag:a,b или id:x,y (можно несколько раз)")
    ap.add_argument("--verify", action="store_true", help="только сверить скачанное и теги плана")
    ap.add_argument("--list", action="store_true", help="перечислить записи манифеста")
    ap.add_argument("--files", metavar="tag:ТЕГ", help="напечатать пути файлов тега (из скачанного)")
    ap.add_argument("--repin", action="store_true", help="закрепить sha256/size/count по скачанному (правит манифест)")
    ap.add_argument("--strict", action="store_true", help="«ожидает генератор» считать ошибкой")
    ap.add_argument("--dry-run", action="store_true", help="показать, что будет скачано")
    ap.add_argument("--max-bytes", type=int, default=DEFAULT_MAX_BYTES, help="предел новой загрузки, байт")
    ap.add_argument("--missing-code", type=int, default=1,
                    help="код выхода, если ошибки только «не скачано» (для ctest-фикстуры: 77 — пропуск)")
    args = ap.parse_args(argv)

    try:
        m = load_manifest(args.manifest)
    except (OSError, ValueError) as ex:
        say(f"fetch_corpus: манифест {args.manifest}: {ex}")
        return 2
    errs = validate_manifest(m)
    if errs:
        for x in errs:
            say(f"{os.path.relpath(args.manifest, REPO)}: {x}")
        return 2
    root = resolve_root(args.root, m)
    by_id = {e["id"]: e for e in m["entries"]}
    only_ids, only_tags, errs = parse_only(args.only, m)
    if args.files:
        if not args.files.startswith("tag:"):
            say("--files: нужно tag:<тег>")
            return 2
        only_tags = set(t for t in args.files[4:].split(",") if t)
    if errs:
        for x in errs:
            say(x)
        return 2
    sel = selected_entries(m, only_ids, only_tags)

    if args.list:
        say(f"Каталог корпусов: {root}")
        say(f"{'id':32} {'вид':9} {'размер':>9} {'файлов':>7}  лицензия / теги")
        for e in sel:
            kind = entry_kind(e)
            size = e.get("size") or sum(it.get("size", 0) for it in e.get("items", [])) or None
            cnt = e.get("count") or e.get("unpacked_count") or (len(e["items"]) if "items" in e else None) or (1 if kind == "url" else None)
            tags = ",".join(sorted(entry_tags(e))) or "—"
            say(f"{e['id']:32} {kind:9} {human(size) if size else '—':>9} {cnt if cnt else '—':>7}  "
                f"{e.get('license') or 'от ' + e.get('from', '?')} / {tags}")
        return 0

    cache: dict = {}
    if args.files:
        rc = 0
        for e in m["entries"]:
            if not (entry_tags(e) & only_tags):
                continue
            st, why, _ = verify_entry(root, e, by_id, cache)
            if st != "ok":
                print(f"# {e['id']}: {why}", file=sys.stderr)
                if st != "pending":  # не дождались генератора — не ошибка выдачи списка
                    rc = 1
                continue
            for rec in file_records(root, e, by_id):
                if set(rec["tags"]) & only_tags:
                    print(rec["path"])
        return rc

    curl = find_curl()
    fetcher = Fetcher(root, curl, args.dry_run)
    failures: list[str] = []
    t0 = time.time()

    if args.repin:
        if not args.only:
            say("--repin только вместе с --only (что именно перезакрепить)")
            return 2
        os.makedirs(root, exist_ok=True)
        notes = repin(root, m, sel, fetcher, save=lambda: save_manifest(args.manifest, m))
        for n in notes:
            say("  repin " + n)
        say(f"Манифест обновлён: {len(notes)} изменений")

    if not args.verify and not args.repin:
        # предел объёма: сколько придётся качать впервые
        need = 0
        for e in sel:
            st, _, _ = verify_entry(root, e, by_id, {})
            if st in ("missing", "bad") and entry_kind(e) in ("git", "url", "items"):
                need += int(e.get("size") or 0) + int(e.get("unpacked_size") or 0)
                need += sum(int(it.get("size") or 0) for it in e.get("items", []))
        say(f"Каталог корпусов: {root}; записей: {len(sel)}; к загрузке до {human(need)}")
        if need > args.max_bytes:
            say(f"Отказ: загрузка {human(need)} больше предела {human(args.max_bytes)} (--max-bytes)")
            return 1
        os.makedirs(root, exist_ok=True)
        for e in sel:
            kind = entry_kind(e)
            if kind in ("from", "generated"):
                continue
            t1 = time.time()
            say(f"[{e['id']}] {kind} …")
            try:
                res = fetcher.fetch_entry(e)
                say(f"[{e['id']}] {res} ({time.time() - t1:.0f} с)")
            except Exception as ex:
                failures.append(f"{e['id']}: {ex}")
                say(f"[{e['id']}] ОШИБКА: {ex}")
        say(f"Скачано за этот запуск: {human(fetcher.downloaded)} за {time.time() - t0:.0f} с")

    # сверка — всегда после загрузки; печать по выбранным
    results = {e["id"]: verify_entry(root, e, by_id, cache) for e in m["entries"]}
    sel_ids = {e["id"] for e in sel}
    hard = bool(failures)  # ошибки загрузки — всегда «жёсткие»
    say("")
    say(f"{'запись':34} {'статус':8} пояснение")
    for e in m["entries"]:
        if e["id"] not in sel_ids:
            continue
        st, why, n = results[e["id"]]
        mark = {"ok": "ok", "missing": "НЕТ", "bad": "ОШИБКА", "pending": "ожидает"}[st]
        say(f"{e['id']:34} {mark:8} {why}")
        if st in ("missing", "bad") or (st == "pending" and args.strict):
            failures.append(f"{e['id']}: {why}")
            hard = hard or st != "missing"

    # теги: манифест ↔ план ↔ эталон 9.2
    ptags = plan_tags(args.plan) if os.path.isfile(args.plan) else {}
    if not ptags:
        failures.append(f"{args.plan}: теги плана не найдены")
        hard = True
    # что проверяем строго: выбранные теги; без --only — все теги 9.2 и плана; при --only id: — только показ
    if only_tags:
        check_tags = set(only_tags)
    elif only_ids:
        check_tags = set()
    else:
        check_tags = set(TAGS_92) | set(ptags)
    shown = [t for t in TAGS_92 if (not only_tags or t in only_tags)]
    say("")
    say(f"{'тег':10} {'план':>5} {'записей':>8} {'скачано':>8} {'файлов':>7}  статус")
    for t in shown + sorted(t for t in ptags if t not in TAGS_92):
        ents = [e for e in m["entries"] if t in entry_tags(e)]
        corpus = [e for e in ents if entry_kind(e) != "generated"]
        gens = [e for e in ents if entry_kind(e) == "generated"]
        ok_c = [e for e in corpus if results[e["id"]][0] == "ok"]
        ok_g = [e for e in gens if results[e["id"]][0] == "ok"]
        nfiles = 0
        for e in ok_c + ok_g:
            nfiles += sum(1 for r in file_records(root, e, by_id) if t in r["tags"])
        in_plan = t in ptags
        check = t in check_tags
        bad = None
        only_missing = False
        if t not in TAGS_92:
            bad = f"тег из плана (строка {ptags[t]}) не из списка эталона 9.2"
        elif not ents:
            if in_plan:
                bad = "нет записей в манифесте"
            status = "нет записей"
        elif len(ok_c) < len(corpus):
            bad = "не скачаны/не сошлись: " + ", ".join(e["id"] for e in corpus if e not in ok_c)
            only_missing = all(results[e["id"]][0] == "missing" for e in corpus if e not in ok_c)
        elif nfiles == 0 and gens:
            status = "ожидает генератор: " + ", ".join(e["id"] for e in gens)
            if args.strict and in_plan and check:
                failures.append(f"тег {t}: ожидает генератор")
                hard = True
        elif nfiles == 0:
            bad = "0 файлов"
        if bad:
            status = ("ОШИБКА: " if check else "нет: ") + bad
            if check:
                failures.append(f"тег {t}: {bad}")
                hard = hard or not only_missing
        elif ents and nfiles:
            status = "ok"
            pend = [e["id"] for e in gens if e not in ok_g]
            if pend:
                status += " (+ ожидает генератор: " + ", ".join(pend) + ")"
        say(f"{t:10} {'да' if in_plan else '—':>5} {len(ents):>8} {len(ok_c) + len(ok_g):>8} {nfiles:>7}  {status}")

    if not args.dry_run:
        idx = write_index(root, m, args.manifest, results)
        say(f"\nИндекс тегов: {idx}")
    if failures:
        say(f"\nИТОГ: ошибок {len(failures)}")
        for f in failures:
            say("  " + f)
        return 1 if hard else args.missing_code
    say("\nИТОГ: всё сошлось")
    return 0


if __name__ == "__main__":
    sys.exit(main())
