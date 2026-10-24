"""Сверка вердиктов валидатора подписей с pyHanko на корпусе EU DSS (план 4.4, п. 5; эталон 3.8 «Acrobat», 9.4).

Три режима:

  survey   — pyHanko 0.38 над файлами тегов sig-b/t/lt/lta корпуса dss (index.json корпуса): по каждой подписи
             intact, valid, trusted, покрытие, уровень изменений, метка времени, уровень PAdES — JSON.
             Якоря — самоподписанные сертификаты самого файла (CMS подписей и меток, /DSS /Certs): у корпуса DSS
             чужие корни, доверие проверяет построение цепочки; отзыв — retroactive (--retroactive-revinfo),
             без сети, soft-fail; время проверки — метка подписи, если она цела, иначе /M (--validation-time
             claimed) — как у валидатора программы (время подписи, иначе заявленное).
  expect   — нормализованная таблица ожиданий из survey (файл → подписи {field, kind, intact, trusted,
             modified, level}); правки человека/сессии вносятся в JSON руками, поле note — почему.
  compare  — наши вердикты (JSON engine.sig.dss) против ожиданий и pyHanko: доля совпадающих файлов, отчёт
             расхождений (markdown), код 1 — доля ниже --min.

Запуск — "$env:OTTISK_PYTHON" -X utf8 -I tools/compare_pyhanko.py <режим> …; нет pyHanko или корпуса — код 77.
"""

from __future__ import annotations

import argparse
import json
import logging
import os
import sys
from pathlib import Path

TAGS = ("sig-b", "sig-t", "sig-lt", "sig-lta")
SKIP = 77


def corpus_root() -> Path:
    return Path(os.environ.get("OTTISK_CORPUS", r"C:\Tools\ottisk\corpus"))


def dss_files(root: Path) -> list[tuple[str, Path]]:
    """(имя относительно validation/, путь) файлов DSS из тегов подписей index.json."""
    index_path = root / "index.json"
    if not index_path.exists():
        return []
    index = json.loads(index_path.read_text(encoding="utf-8"))
    out: dict[str, Path] = {}
    marker = "/validation/"
    for tag in TAGS:
        for rec in index.get("tags", {}).get(tag, []):
            if not str(rec.get("entry", "")).startswith("dss"):
                continue
            rel = rec["rel"]
            name = rel.split(marker, 1)[1] if marker in rel else rel
            out[name] = Path(rec["path"])
    return sorted(out.items())


# --- pyHanko ---------------------------------------------------------------------------------------------------


def _embedded_certs(reader, sigs) -> tuple[list, list, list]:
    """Сертификаты, OCSP и CRL файла: CMS подписей и их меток, /DSS."""
    from asn1crypto import cms, ocsp, x509 as ax509

    certs: dict[bytes, object] = {}
    ocsps: list = []
    crls: list = []

    def add_signed_data(sd) -> None:
        for c in sd["certificates"] or []:
            if c.name == "certificate":
                certs.setdefault(c.chosen.dump(), c.chosen)
        for si in sd["signer_infos"]:
            ua = si["unsigned_attrs"]
            if ua.native is None:
                continue
            for attr in ua:
                if attr["type"].native in ("signature_time_stamp_token",):
                    for v in attr["values"]:
                        try:
                            add_signed_data(v["content"])
                        except Exception:  # noqa: BLE001
                            pass

    for s in sigs:
        try:
            add_signed_data(s.signed_data)
        except Exception:  # noqa: BLE001
            pass
    try:
        dss = reader.root.get("/DSS")
    except Exception:  # noqa: BLE001
        dss = None
    if dss is not None:
        for key, kind in (("/Certs", "cert"), ("/OCSPs", "ocsp"), ("/CRLs", "crl")):
            try:
                arr = dss.get(key) or []
            except Exception:  # noqa: BLE001
                arr = []
            for ref in arr:
                try:
                    data = ref.get_object().data
                    if kind == "cert":
                        c = ax509.Certificate.load(data)
                        certs.setdefault(c.dump(), c)
                    elif kind == "ocsp":
                        ocsps.append(ocsp.OCSPResponse.load(data))
                    else:
                        from asn1crypto import crl

                        crls.append(crl.CertificateList.load(data))
                except Exception:  # noqa: BLE001
                    pass
    _ = cms
    return list(certs.values()), ocsps, crls


def _self_signed(cert) -> bool:
    try:
        return cert.self_signed in ("yes", "maybe")
    except Exception:  # noqa: BLE001
        return False


def _revision_of(reader, obj_ref) -> int | None:
    try:
        return reader.xrefs.get_last_change(obj_ref)
    except Exception:  # noqa: BLE001
        return None


def _dss_revision(reader) -> int | None:
    """Ревизия, в которой последний раз менялся /DSS (или каталог с прямым /DSS); None — /DSS нет или пуст."""
    from pyhanko.pdf_utils import generic

    root_ref = reader.trailer.raw_get("/Root")
    root = reader.root
    if "/DSS" not in root:
        return None
    raw = root.raw_get("/DSS")
    dss = root["/DSS"]
    if not any(len(dss.get(k) or []) for k in ("/Certs", "/OCSPs", "/CRLs")):
        return None
    if isinstance(raw, generic.IndirectObject):
        return _revision_of(reader, raw.reference)
    return _revision_of(reader, root_ref.reference) if isinstance(root_ref, generic.IndirectObject) else None


def _sig_has_ts(sig) -> bool:
    try:
        for si in sig.signed_data["signer_infos"]:
            ua = si["unsigned_attrs"]
            if ua.native is None:
                continue
            for attr in ua:
                if attr["type"].native == "signature_time_stamp_token":
                    return True
    except Exception:  # noqa: BLE001
        pass
    return False


def pades_level(sig, sig_rev: int | None, dts_revs: list[int], dss_rev: int | None) -> str:
    """Уровень по структуре (правило валидатора, verifier.cpp): B — подпись; T — метка в подписи или метка
    документа после неё; LT — T и /DSS с данными, изменённый после подписи; LTA — LT и метка документа не раньше
    ревизии /DSS."""
    if sig_rev is None:
        return "B"
    later_dts = [r for r in dts_revs if r > sig_rev]
    if not (_sig_has_ts(sig) or later_dts):
        return "B"
    if dss_rev is None or dss_rev <= sig_rev:
        return "T"
    if any(r >= dss_rev for r in later_dts):
        return "LTA"
    return "LT"


def modified_of(status) -> str:
    """none — подпись покрывает файл; ltv/form/annot — только разрешённые изменения (уровень pyHanko);
    illegal — изменения вне разрешённых или покрытие не по ревизии."""
    from pyhanko.sign.diff_analysis import ModificationLevel
    from pyhanko.sign.validation.status import SignatureCoverageLevel

    cov = getattr(status, "coverage", None)
    if cov == SignatureCoverageLevel.ENTIRE_FILE:
        return "none"
    if cov != SignatureCoverageLevel.ENTIRE_REVISION:
        return "illegal"
    if not getattr(status, "docmdp_ok", False):
        return "illegal"
    level = getattr(status, "modification_level", None)
    return {
        ModificationLevel.NONE: "none",
        ModificationLevel.LTA_UPDATES: "ltv",
        ModificationLevel.FORM_FILLING: "form",
        ModificationLevel.ANNOTATIONS: "annot",
    }.get(level, "illegal")


def survey_file(path: Path) -> dict:
    from pyhanko.pdf_utils.reader import PdfFileReader
    from pyhanko.sign.validation import validate_pdf_signature, validate_pdf_timestamp
    from pyhanko_certvalidator import ValidationContext

    out: dict = {"signatures": []}
    with path.open("rb") as fh:
        try:
            reader = PdfFileReader(fh, strict=False)
            if reader.encrypted:
                out["error"] = "encrypted"
                return out
            sigs = list(reader.embedded_signatures)
        except Exception as exc:  # noqa: BLE001
            out["error"] = f"{type(exc).__name__}: {exc}"
            return out
        certs, ocsps, crls = _embedded_certs(reader, sigs)
        roots = [c for c in certs if _self_signed(c)]
        others = [c for c in certs if not _self_signed(c)]
        out["anchors"] = len(roots)
        dss_rev = None
        try:
            dss_rev = _dss_revision(reader)
        except Exception:  # noqa: BLE001
            pass
        dts_revs = []
        for s in sigs:
            try:
                if s.sig_object_type == "/DocTimeStamp":
                    dts_revs.append(s.signed_revision)
            except Exception:  # noqa: BLE001
                pass

        def vc(moment=None):
            kw = dict(
                trust_roots=roots,
                other_certs=others,
                ocsps=ocsps,
                crls=crls,
                allow_fetching=False,
                retroactive_revinfo=True,
                revocation_mode="soft-fail",
            )
            if moment is not None:
                kw["moment"] = moment
            return ValidationContext(**kw)

        for s in sigs:
            rec: dict = {"field": s.field_name}
            try:
                rec["subfilter"] = str(s.sig_object.get("/SubFilter", ""))
                kind = "dts" if s.sig_object_type == "/DocTimeStamp" else "sig"
                rec["kind"] = kind
                sig_rev = getattr(s, "signed_revision", None)
                if kind == "dts":
                    # Цепочка службы времени — на момент самой метки (как у валидатора программы).
                    st = validate_pdf_timestamp(s, validation_context=vc())
                    if st.intact and st.valid and getattr(st, "timestamp", None) is not None:
                        st = validate_pdf_timestamp(s, validation_context=vc(st.timestamp))
                    rec["level"] = "DTS"
                else:
                    claimed = s.self_reported_timestamp
                    st = validate_pdf_signature(s, vc(claimed), ts_validation_context=vc())
                    ts = getattr(st, "timestamp_validity", None)
                    if ts is not None:
                        rec["timestamp"] = {
                            "intact": bool(ts.intact),
                            "valid": bool(ts.valid),
                            "trusted": bool(ts.trusted),
                            "time": ts.timestamp.isoformat() if ts.timestamp else None,
                        }
                        if ts.intact and ts.valid and ts.timestamp is not None:
                            st = validate_pdf_signature(s, vc(ts.timestamp), ts_validation_context=vc())
                    sig_rev2 = sig_rev if sig_rev is not None else None
                    rec["level"] = pades_level(s, sig_rev2, dts_revs, dss_rev)
                    rec["claimed"] = claimed.isoformat() if claimed else None
                    try:
                        lvl = s.docmdp_level
                        rec["docmdp"] = lvl.value if lvl is not None else None
                    except Exception:  # noqa: BLE001
                        pass
                rec["intact"] = bool(st.intact)
                rec["valid"] = bool(st.valid)
                rec["trusted"] = bool(st.trusted)
                rec["revoked"] = bool(getattr(st, "revoked", False))
                rec["modified"] = modified_of(st)
                rec["summary"] = st.summary()
            except Exception as exc:  # noqa: BLE001
                rec["error"] = f"{type(exc).__name__}: {exc}"[:300]
            out["signatures"].append(rec)
    return out


def cmd_survey(args) -> int:
    try:
        import pyhanko  # noqa: F401
    except ImportError:
        print("SKIP: нет pyHanko")
        return SKIP
    files = dss_files(corpus_root())
    if not files:
        print("SKIP: нет корпуса dss (index.json, теги sig-*)")
        return SKIP
    for name in ("pyhanko", "pyhanko_certvalidator", "asyncio"):
        logging.getLogger(name).setLevel(logging.CRITICAL)
    result: dict = {}
    only = set(args.only or [])
    for name, path in files:
        if only and name not in only:
            continue
        result[name] = survey_file(path)
        if args.verbose:
            print(name, json.dumps(result[name], ensure_ascii=False)[:300])
    Path(args.out).parent.mkdir(parents=True, exist_ok=True)
    Path(args.out).write_text(json.dumps(result, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"pyHanko: {len(result)} файлов → {args.out}")
    return 0


# --- ожидания и сверка ------------------------------------------------------------------------------------------


def normalize(sig: dict) -> dict:
    """Поля сравнения подписи: intact — целостность (структура, хэш, подпись), trusted, modified, level."""
    if "error" in sig and "intact" not in sig:
        return {"field": sig.get("field"), "kind": sig.get("kind", "sig"), "error": True}
    return {
        "field": sig.get("field"),
        "kind": sig.get("kind", "sig"),
        "intact": bool(sig.get("intact")) and bool(sig.get("valid", True)),
        "trusted": bool(sig.get("trusted")),
        "modified": sig.get("modified"),
        "level": sig.get("level"),
    }


def dump_table(table: dict) -> str:
    """Таблица ожиданий: подпись — одной строкой (файл ≤ 50 КБ, дифф по подписи)."""
    one = lambda v: json.dumps(v, ensure_ascii=False)  # noqa: E731
    out = ["{", f' "about": {one(table["about"])},', ' "files": {']
    names = sorted(table["files"])
    for i, name in enumerate(names):
        entry = table["files"][name]
        out.append(f"  {one(name)}: {{")
        extra = [k for k in entry if k != "signatures"]
        for k in extra:
            out.append(f"   {one(k)}: {one(entry[k])},")
        out.append('   "signatures": [')
        sigs = entry["signatures"]
        for j, s in enumerate(sigs):
            out.append("    " + one(s) + ("," if j + 1 < len(sigs) else ""))
        out.append("   ]")
        out.append("  }" + ("," if i + 1 < len(names) else ""))
    out.append(" }")
    out.append("}")
    return "\n".join(out) + "\n"


def cmd_expect(args) -> int:
    survey = json.loads(Path(args.survey).read_text(encoding="utf-8"))
    table: dict = {
        "about": "Ожидания валидатора на корпусе EU DSS (план 4.4, п. 5): retroactive, якоря — самоподписанные "
        "сертификаты файла, время — метка подписи, иначе /M. Сначала — pyHanko 0.38 (tools/compare_pyhanko.py "
        "expect), затем правки с причиной в note. modified: none — подпись покрывает файл; ltv/form/annot — "
        "разрешённые изменения; illegal — запрещённые или покрытие не по ревизии.",
        "files": {},
    }
    for name, rec in survey.items():
        sigs = []
        for s in rec.get("signatures", []):
            n = normalize(s)
            if n.get("error"):
                n["note"] = "pyHanko: " + s.get("error", "")[:120]
            elif n["kind"] == "dts":
                # Изменения после метки документа pyHanko оценивает строже подписи (DSS и следующая метка —
                # OTHER): у меток сравниваются целостность и доверие.
                n.pop("modified", None)
            sigs.append(n)
        entry: dict = {"signatures": sigs}
        if rec.get("error"):
            entry["error"] = rec["error"]
        table["files"][name] = entry
    Path(args.out).write_text(dump_table(table), encoding="utf-8")
    print(f"ожидания: {len(table['files'])} файлов → {args.out}")
    return 0


COARSE = {"none": "none", "ltv": "allowed", "form": "allowed", "annot": "allowed", "illegal": "illegal"}


def sig_matches(want: dict, got: dict | None) -> list[str]:
    if got is None:
        return ["нет подписи"]
    if want.get("error"):
        return []  # ожидание «не разобрать» — любое поведение без падения
    diffs = []
    for key in ("kind", "intact", "trusted", "level"):
        if key in want and want[key] != got.get(key):
            diffs.append(f"{key}: ждали {want[key]}, у нас {got.get(key)}")
    w, g = COARSE.get(want.get("modified"), want.get("modified")), COARSE.get(got.get("modified"), got.get("modified"))
    if "modified" in want and w != g:
        diffs.append(f"modified: ждали {want.get('modified')}, у нас {got.get('modified')}")
    return diffs


def file_diffs(want_sigs: list[dict], got_sigs: list[dict]) -> list[str]:
    diffs = []
    by_field = {s.get("field"): s for s in got_sigs}
    if len(got_sigs) != len(want_sigs):
        diffs.append(f"подписей: ждали {len(want_sigs)}, у нас {len(got_sigs)}")
    for w in want_sigs:
        for d in sig_matches(w, by_field.get(w.get("field"))):
            diffs.append(f"{w.get('field')}: {d}")
    return diffs


def cmd_compare(args) -> int:
    ours_path = Path(args.ours)
    if not ours_path.exists():
        print(f"SKIP: нет {ours_path} (engine_tests sig.dss)")
        return SKIP
    ours = json.loads(ours_path.read_text(encoding="utf-8"))
    ours_files = ours.get("files", ours)
    expect = json.loads(Path(args.expect).read_text(encoding="utf-8"))["files"] if args.expect else {}
    pyh = {}
    if args.pyhanko:
        if Path(args.pyhanko).exists():
            pyh = json.loads(Path(args.pyhanko).read_text(encoding="utf-8"))
        else:
            rc = cmd_survey(argparse.Namespace(out=args.pyhanko, only=None, verbose=False))
            if rc == SKIP:
                return SKIP
            pyh = json.loads(Path(args.pyhanko).read_text(encoding="utf-8"))
    lines = ["# Валидатор ↔ pyHanko на корпусе EU DSS", ""]
    total = ok_expect = ok_pyh = 0
    rows = []
    for name in sorted(ours_files):
        got = [s for s in ours_files[name].get("signatures", [])]
        total += 1
        d_exp = file_diffs(expect[name]["signatures"], got) if name in expect else ["нет в ожиданиях"]
        d_pyh = file_diffs([normalize(s) for s in pyh[name]["signatures"]], got) if name in pyh else []
        ok_expect += not d_exp
        ok_pyh += not d_pyh
        if d_exp or d_pyh:
            rows.append((name, d_exp, d_pyh))
    share_exp = ok_expect / total if total else 0.0
    share_pyh = ok_pyh / total if total and pyh else 0.0
    lines.append(f"- файлов: {total}; совпадений с ожиданиями: {ok_expect} ({share_exp:.1%})")
    if pyh:
        lines.append(f"- совпадений с pyHanko: {ok_pyh} ({share_pyh:.1%})")
    lines.append("")
    for name, d_exp, d_pyh in rows:
        lines.append(f"## {name}")
        for d in d_exp:
            lines.append(f"- ожидания: {d}")
        for d in d_pyh:
            lines.append(f"- pyHanko: {d}")
        lines.append("")
    report = "\n".join(lines) + "\n"
    if args.report:
        Path(args.report).parent.mkdir(parents=True, exist_ok=True)
        Path(args.report).write_text(report, encoding="utf-8")
    print(lines[2])
    if pyh:
        print(lines[3])
    if share_exp < args.min:
        print(f"доля совпадений с ожиданиями {share_exp:.1%} < {args.min:.0%}")
        for name, d_exp, _ in rows[:30]:
            if d_exp:
                print(f"  {name}: {'; '.join(d_exp)}")
        return 1
    return 0


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("survey")
    s.add_argument("--out", required=True)
    s.add_argument("--only", nargs="*")
    s.add_argument("--verbose", action="store_true")
    e = sub.add_parser("expect")
    e.add_argument("--survey", required=True)
    e.add_argument("--out", required=True)
    c = sub.add_parser("compare")
    c.add_argument("--ours", required=True)
    c.add_argument("--expect")
    c.add_argument("--pyhanko", help="JSON survey; нет файла — посчитать и записать")
    c.add_argument("--report")
    c.add_argument("--min", type=float, default=0.9)
    args = ap.parse_args(argv)
    return {"survey": cmd_survey, "expect": cmd_expect, "compare": cmd_compare}[args.cmd](args)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
