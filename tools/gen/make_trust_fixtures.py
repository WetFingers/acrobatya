# -*- coding: utf-8 -*-
"""make_trust_fixtures.py — фикстуры EUTL и pins подписантов LOTL (план 4.5, п. 4; sign-31).

Из снимка корпуса trust (этап 0, только чтение: eu-lotl.xml, TL-DE.xml; сеть не нужна):
  * tests/fixtures/trust/eu-lotl.xml — урезанный LOTL: заголовок SchemeInformation (без лишних языков и
    юридических текстов) с указателями только на DE и сам EU, подпись ds:Signature — как в снимке (её сертификат
    подписанта — pinned); байты XML не пересериализуются — вырезаются куски текста;
  * tests/fixtures/trust/TL-DE.xml — урезанный TL Германии: SchemeInformation и несколько поставщиков (CA/QC
    и TSA/QTST «granted», один целиком «withdrawn»), у служб убрана ServiceHistory, подпись — как в снимке
    (сертификат подписанта TL обязан быть среди сертификатов указателя DE в LOTL — проверяется здесь же);
  * tests/fixtures/trust/expected.json — ожидания: сколько сертификатов якорей даёт фикстура (CA/QC, TSA/QTST,
    TSA, NationalRootCA-QC со статусом granted / recognisedatnationallevel / accredited / undersupervision /
    setbynationallaw; без повторов), их SHA-256, номера и даты списков;
  * tests/fixtures/trust/not-a-list.acrobatsecuritysettings — искусственный «AATL»: PDF с SecuritySettings.xml и
    тремя сертификатами тестовой PKI — только для ветки «не похоже на список» (настоящий tl12 — в корпусе);
  * assets/trust/lotl_pins.json — SHA-256 сертификатов подписантов LOTL: ds:X509Certificate подписи снимка и
    сертификаты указателя EU (опубликованные в OJ C «list of trusted lists signing certificates»), дата снимка
    и адрес LOTL.
Детерминизм: только вырезание текста, сортировка по SHA-256. Размер каждой фикстуры ≤ 50 КБ.
Запуск: "C:\\Tools\\venv-test\\Scripts\\python.exe" -X utf8 -I tools/gen/make_trust_fixtures.py [--out <корень>]
        [--snapshot <каталог trust корпуса>]
"""
import base64
import hashlib
import json
import os
import re
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))

import _genlib as g  # noqa: E402

LOTL_URL = "https://ec.europa.eu/tools/lotl/eu-lotl.xml"
TYPES = {
    "http://uri.etsi.org/TrstSvc/Svctype/CA/QC",
    "http://uri.etsi.org/TrstSvc/Svctype/TSA/QTST",
    "http://uri.etsi.org/TrstSvc/Svctype/TSA",
    "http://uri.etsi.org/TrstSvc/Svctype/NationalRootCA-QC",
}
STATUSES = {
    "http://uri.etsi.org/TrstSvc/TrustedList/Svcstatus/granted",
    "http://uri.etsi.org/TrstSvc/TrustedList/Svcstatus/recognisedatnationallevel",
    "http://uri.etsi.org/TrstSvc/TrustedList/Svcstatus/accredited",
    "http://uri.etsi.org/TrstSvc/TrustedList/Svcstatus/undersupervision",
    "http://uri.etsi.org/TrstSvc/TrustedList/Svcstatus/setbynationallaw",
}


def der_list(xml: str) -> list[bytes]:
    return [base64.b64decode(re.sub(r"\s+", "", m)) for m in re.findall(r"<(?:\w+:)?X509Certificate>([^<]*)<", xml)]


def sha(der: bytes) -> str:
    return hashlib.sha256(der).hexdigest().upper()


def tag(name: str) -> str:
    return rf"<(?:\w+:)?{name}\b[^>]*>.*?</(?:\w+:)?{name}>"


def first(name: str, xml: str) -> str:
    m = re.search(rf"<(?:\w+:)?{name}>(.*?)</(?:\w+:)?{name}>", xml, re.S)
    return m.group(1).strip() if m else ""


def trim_names(xml: str, element: str, keep=("en",)) -> str:
    """У многоязычных элементов (Name, URI) оставить только keep."""
    def repl(m):
        lang = re.search(r'xml:lang="([^"]+)"', m.group(0))
        return m.group(0) if lang is None or lang.group(1) in keep else ""
    return re.sub(rf"\s*<(?:\w+:)?{element} xml:lang=\"[^\"]+\">[^<]*</(?:\w+:)?{element}>", repl, xml)


def drop_languages(xml: str) -> str:
    """Убрать элементы с xml:lang, кроме английского (названия, адреса, тексты)."""
    xml = re.sub(r"\s*<PostalAddress xml:lang=\"(?!en\")[^\"]+\">.*?</PostalAddress>", "", xml, flags=re.S)
    for el in ("Name", "URI", "TSLLegalNotice", "StreetAddress", "Locality", "PostalCode", "CountryName"):
        xml = trim_names(xml, el)
    return xml


def cut_lotl(s: str) -> tuple[str, list[bytes], list[bytes]]:
    pointers = re.findall(tag("OtherTSLPointer"), s, re.S)
    keep = [p for p in pointers if first("SchemeTerritory", p) in ("DE", "EU")]
    head, rest = s.split("<OtherTSLPointer>", 1)
    tail = rest[rest.rindex("</OtherTSLPointer>") + len("</OtherTSLPointer>"):]
    out = head + "\n            ".join(keep).replace("<OtherTSLPointer>", "<OtherTSLPointer>", 1) + tail
    # Заголовок: только английские названия и тексты.
    sig_at = out.index("<ds:Signature")
    body, sig = out[:sig_at], out[sig_at:]
    for el in ("Name", "TSLLegalNotice", "StreetAddress", "Locality", "PostalCode", "CountryName", "URI"):
        body = trim_names(body, el)
    body = re.sub(r"\s*<PostalAddress xml:lang=\"(?!en\")[^\"]+\">.*?</PostalAddress>", "", body, flags=re.S)
    out = body + sig
    signer = der_list(sig)
    eu = [p for p in keep if first("SchemeTerritory", p) == "EU"][0]
    return out, signer, der_list(eu)


def services(tsp: str):
    for sv in re.findall(tag("TSPService"), tsp, re.S):
        info = re.search(tag("ServiceInformation"), sv, re.S).group(0)
        yield sv, first("ServiceTypeIdentifier", info), first("ServiceStatus", info), der_list(
            re.search(tag("ServiceDigitalIdentity"), info, re.S).group(0))


def cut_tl(s: str) -> tuple[str, list[str]]:
    tsps = re.findall(tag("TrustServiceProvider"), s, re.S)
    chosen = []
    want = {"http://uri.etsi.org/TrstSvc/Svctype/CA/QC": 2, "http://uri.etsi.org/TrstSvc/Svctype/TSA/QTST": 1}
    withdrawn_tsp = None
    for t in tsps:
        svs = list(services(t))
        good = [x for x in svs if x[1] in TYPES and x[2] in STATUSES]
        if not good and withdrawn_tsp is None and svs:
            withdrawn_tsp = t
            continue
        for _, typ, _, _ in good:
            if want.get(typ, 0) > 0:
                want[typ] -= 1
                chosen.append(t)
                break
        if not any(want.values()) and withdrawn_tsp is not None:
            break
    if withdrawn_tsp is not None:
        chosen.append(withdrawn_tsp)
    # У каждого поставщика — до двух служб (сначала подходящие), без ServiceHistory.
    small = []
    for t in chosen:
        svs = list(services(t))
        svs.sort(key=lambda x: (not (x[1] in TYPES and x[2] in STATUSES)))
        keep = []
        for x in svs[:2]:
            sv = re.sub(r"\s*" + tag("ServiceHistory"), "", x[0], flags=re.S)
            sv = re.sub(r"\s*" + tag("ServiceInformationExtensions"), "", sv, flags=re.S)
            keep.append(sv)
        list_open = re.search(r"<(?:\w+:)?TSPServices>", t).group(0)
        list_close = re.search(r"</(?:\w+:)?TSPServices>", t).group(0)
        before = t[: t.index(list_open) + len(list_open)]
        after = t[t.index(list_close):]
        before = re.sub(r"\s*" + tag("TSPInformationExtensions"), "", before, flags=re.S)
        before = drop_languages(before)
        small.append(before + "\n" + "\n".join(keep) + "\n" + after)
    head = s[: s.index(tsps[0])]
    tail = s[s.index(tsps[-1]) + len(tsps[-1]):]
    sig_at = head.index("<TrustServiceProviderList")
    # Указатели TL на LOTL (сертификаты EU) фикстуре не нужны.
    head = re.sub(r"\s*" + tag("PointersToOtherTSL"), "", drop_languages(head[:sig_at]), flags=re.S) + head[sig_at:]
    return head + "\n".join(small) + tail, small


def anchors_of(tsps: list[str]) -> list[str]:
    out = set()
    for t in tsps:
        for _, typ, st, ders in services(t):
            if typ in TYPES and st in STATUSES:
                out.update(sha(d) for d in ders)
    return sorted(out)


def not_a_list(out: Path) -> None:
    """Искусственный «список» для ветки отказа AATL: PDF с SecuritySettings.xml, где три сертификата тестовой PKI."""
    import pikepdf
    from pikepdf import Array, Dictionary, Name, String

    pki = g.REPO / "tests" / "fixtures" / "pki"
    certs = []
    for name in ("root.pem", "inter.pem", "tsa.pem"):
        text = (pki / name).read_text(encoding="ascii")
        b64 = "".join(line for line in text.splitlines() if line and not line.startswith("-----"))
        certs.append(b64)
    identities = "".join(
        f"<Identity><ImportAction>1</ImportAction><Certificate>{c}</Certificate>"
        "<Trust><Root>1</Root><CertifiedDocuments>0</CertifiedDocuments></Trust></Identity>" for c in certs)
    xml = f'<?xml version="1.0"?><SecuritySettings><TrustedIdentities>{identities}</TrustedIdentities></SecuritySettings>'
    pdf = pikepdf.new()
    pdf.add_blank_page(page_size=(612, 792))
    stream = pdf.make_stream(xml.encode("utf-8"))
    stream.Type = Name.EmbeddedFile
    stream.Subtype = Name("/text/xml")
    spec = pdf.make_indirect(Dictionary(Type=Name.Filespec, F=String("SecuritySettings.xml"),
                                        UF=String("SecuritySettings.xml"), EF=Dictionary(F=stream)))
    pdf.Root.Names = Dictionary(EmbeddedFiles=Dictionary(Names=Array([String("SecuritySettings.xml"), spec])))
    pdf.save(out / "not-a-list.acrobatsecuritysettings", static_id=True, compress_streams=True,
             object_stream_mode=pikepdf.ObjectStreamMode.disable, min_version="1.7")


def main() -> int:
    args = g.common_args("фикстуры EUTL и pins LOTL",
                         extra=lambda ap: ap.add_argument("--snapshot", default=None, help="каталог trust корпуса"))
    snap = Path(args.snapshot) if args.snapshot else Path(
        os.environ.get("OTTISK_CORPUS", r"C:\Tools\ottisk\corpus")) / "trust"
    lotl_src = (snap / "eu-lotl.xml").read_text(encoding="utf-8")
    tl_src = (snap / "TL-DE.xml").read_text(encoding="utf-8")
    manifest = json.loads((g.REPO / "tests" / "corpus" / "manifest.json").read_text(encoding="utf-8"))
    entry = [e for e in manifest["entries"] if e["id"] == "trust"][0]
    snapshot_date = entry.get("snapshot", "")

    lotl, signer, eu_certs = cut_lotl(lotl_src)
    tl, tsps = cut_tl(tl_src)
    de_ptr = [p for p in re.findall(tag("OtherTSLPointer"), lotl, re.S) if first("SchemeTerritory", p) == "DE"][0]
    tl_signer = der_list(tl[tl.index("<ds:Signature"):] if "<ds:Signature" in tl else tl[tl.rindex("Signature"):])
    declared = {sha(d) for d in der_list(de_ptr)}
    assert tl_signer and sha(tl_signer[0]) in declared, "подписант TL-DE не объявлен в указателе DE"

    out = Path(args.out) / "tests" / "fixtures" / "trust"
    out.mkdir(parents=True, exist_ok=True)
    (out / "eu-lotl.xml").write_text(lotl, encoding="utf-8", newline="")
    (out / "TL-DE.xml").write_text(tl, encoding="utf-8", newline="")
    anchors = anchors_of(tsps)
    g.write_json(out / "expected.json", {
        "about": "make_trust_fixtures.py: урезанные LOTL и TL-DE из снимка корпуса trust; anchors — SHA-256 "
                 "сертификатов служб CA/QC, TSA/QTST, TSA, NationalRootCA-QC с действующим статусом",
        "snapshot": snapshot_date,
        "lotl": {"sequence": int(first("TSLSequenceNumber", lotl)), "issued": first("ListIssueDateTime", lotl),
                 "nextUpdate": first("dateTime", re.search(tag("NextUpdate"), lotl, re.S).group(0)),
                 "pointers": 2, "signer": sha(signer[0])},
        "tl": {"territory": "DE", "sequence": int(first("TSLSequenceNumber", tl)),
               "issued": first("ListIssueDateTime", tl),
               "nextUpdate": first("dateTime", re.search(tag("NextUpdate"), tl, re.S).group(0)),
               "providers": len(tsps), "signer": sha(tl_signer[0]),
               "location": first("TSLLocation", de_ptr)},
        "anchors": anchors,
    })
    pins = []
    seen = set()
    for origin, ders in (("signature", signer), ("EU pointer (OJ C)", eu_certs)):
        for d in ders:
            h = sha(d)
            if h in seen:
                continue
            seen.add(h)
            pins.append({"sha256": h, "from": origin})
    pins_path = Path(args.out) / "assets" / "trust" / "lotl_pins.json"
    pins_path.parent.mkdir(parents=True, exist_ok=True)
    g.write_json(pins_path, {
        "about": "Сертификаты подписантов EU LOTL (ETSI TS 119 612): ds:X509Certificate подписи и сертификаты "
                 "указателя EU — список, опубликованный в Официальном журнале ЕС (OJ C, «list of trusted lists "
                 "signing certificates»). Источник — снимок eu-lotl.xml корпуса trust (этап 0); пересоздать — "
                 "tools/gen/make_trust_fixtures.py; сверка с живым файлом — ctest compat.trust.live "
                 "(OTTISK_LIVE_NET=1).",
        "url": LOTL_URL,
        "snapshot": snapshot_date,
        "sequence": int(first("TSLSequenceNumber", lotl)),
        "pins": pins,
    })
    not_a_list(out)
    for p in (out / "eu-lotl.xml", out / "TL-DE.xml"):
        size = p.stat().st_size
        g.say(f"{p.name}: {size} bytes")
        assert size <= 50 * 1024, f"{p.name} больше 50 КБ"
    g.say(f"anchors: {len(anchors)}, pins: {len(pins)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
