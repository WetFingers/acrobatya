# -*- coding: utf-8 -*-
"""verify_fixtures.py — приёмка сгенерированных фикстур (последний шаг make_all.ps1).

Что проверяет (каталоги tests/data/mini и tests/fixtures/{annots,forms,enc,sig,scans,office,cyr,demo,images}):
  — qpdf --check = 0 (или 3 — только предупреждения) у каждого PDF, кроме truncated.pdf
    (он ремонтируется — проверяется, что mutool его открывает); зашифрованные — с паролем
    из mini.json / passwords.json;
  — pypdfium2 (движок Chrome/Edge) открывает и рендерит первую страницу каждого PDF;
  — размеры: mini ≤ 50 КБ, сканы ≤ 1,5 МБ;
  — mutool draw -F txt: cyr-cid.pdf даёт «Привет, мир», type1-diff.pdf — кириллицу, Word-PDF — «Кириллица»;
  — копии под именами плана байт в байт равны своим образцам (forms-all = fields-all, annots-all = matrix-28);
  — матрица аннотаций: 28 разных подтипов, TrapNet последний; forms-all: Tx/Btn/Ch/Sig, редактируемый
    выпадающий список, /AA /V, /CO, две кнопки SubmitForm, ResetForm, /DV, /Tabs /R; xfa-stub: /XFA;
  — Office: ZIP целы (docx/xlsx/pptx/odt), RTF начинается с {\\rtf1;
  — корпус gen (если есть): big-1200 — 1 200 страниц, images-200 — 200, mutants — 100 файлов.
Печатает строку на файл с ошибкой и итог; код 0 — всё в порядке, 1 — есть нарушения.
Откуда: qpdf 12.4.2, mutool 1.28.5, pypdfium2 5.14.0, pikepdf.
Детерминизм: только чтение.
Запуск: "C:\\Tools\\venv-test\\Scripts\\python.exe" -X utf8 -I tools/gen/verify_fixtures.py [--out <корень>] [--corpus <каталог>]
"""
import json
import sys
import zipfile
from pathlib import Path

sys.dont_write_bytecode = True                     # без __pycache__ в tools/gen
sys.path.insert(0, str(Path(__file__).resolve().parent))

import pypdfium2 as pdfium  # noqa: E402

import _genlib as g  # noqa: E402

DIRS = ["tests/data/mini", "tests/fixtures/annots", "tests/fixtures/forms", "tests/fixtures/enc",
        "tests/fixtures/sig", "tests/fixtures/scans", "tests/fixtures/office", "tests/fixtures/cyr",
        "tests/fixtures/demo", "tests/fixtures/images"]


def passwords(root: Path) -> dict:
    pw = {}
    mini = root / "tests/data/mini/mini.json"
    if mini.exists():
        for name, m in json.loads(mini.read_text(encoding="utf-8"))["files"].items():
            if "user_password" in m:
                pw["tests/data/mini/" + name] = m["user_password"]
    enc = root / "tests/fixtures/enc/passwords.json"
    if enc.exists():
        for m in json.loads(enc.read_text(encoding="utf-8"))["files"]:
            pw["tests/fixtures/enc/" + m["file"]] = m["user_password"]
    return pw


def check_structure(root: Path) -> list:
    """Содержимое, на которое опираются сессии 5.1 (матрица) и 4.4/6.1 (forms-all, xfa-stub)."""
    import pikepdf
    from pikepdf import Name
    bad = []
    p = root / "tests/fixtures/annots/matrix-28.pdf"
    if p.exists():
        with pikepdf.open(p) as pdf:
            st = [str(a.Subtype)[1:] for a in pdf.pages[0].obj.get("/Annots", [])]
            if len(st) != 28 or len(set(st)) != 28 or st[-1] != "TrapNet":
                bad.append((p.relative_to(root).as_posix(), "аннотаций %d, подтипов %d, последняя %s"
                            % (len(st), len(set(st)), st[-1] if st else "-")))
    p = root / "tests/fixtures/forms/forms-all.pdf"
    if p.exists():
        with pikepdf.open(p) as pdf:
            af = pdf.Root.AcroForm
            fields = list(af.Fields)
            types = {str(f.get("/FT")) for f in fields}
            acts = [str(f.A.S) for f in fields if "/A" in f]
            need = {
                "типы Tx/Btn/Ch/Sig": {"/Tx", "/Btn", "/Ch", "/Sig"} <= types,
                "редактируемый выпадающий список": any(str(f.get("/FT")) == "/Ch" and int(f.get("/Ff", 0)) & (1 << 18)
                                                       for f in fields),
                "/AA /V": any("/AA" in f and "/V" in f.AA for f in fields),
                "/CO": len(af.get("/CO", [])) > 0,
                "две SubmitForm": acts.count("/SubmitForm") == 2,
                "ResetForm": "/ResetForm" in acts,
                "/DV": sum("/DV" in f for f in fields) >= 5,
                "/Tabs /R": pdf.pages[0].obj.get("/Tabs") == Name.R,
            }
            for what, ok in need.items():
                if not ok:
                    bad.append((p.relative_to(root).as_posix(), "нет: " + what))
    p = root / "tests/fixtures/forms/xfa-stub.pdf"
    if p.exists():
        with pikepdf.open(p) as pdf:
            if "/XFA" not in pdf.Root.AcroForm:
                bad.append((p.relative_to(root).as_posix(), "нет /AcroForm /XFA"))
    return bad


def main():
    args = g.common_args(__doc__, corpus=True, extra=lambda ap: ap.add_argument(
        "--no-word", action="store_true", help="без файлов Word (make_all.ps1 -SkipWord)"))
    root = Path(args.out)
    pw = passwords(root)
    bad = []
    count = 0
    for d in DIRS:
        for p in sorted((root / d).glob("*")):
            rel = p.relative_to(root).as_posix()
            if p.suffix.lower() == ".pdf":
                count += 1
                password = pw.get(rel)
                if p.name == "truncated.pdf":
                    r = g.run([g.mutool_path(), "draw", "-q", "-F", "txt", "-o", "-", p], check=False)
                    if r.returncode != 0 or "Страница 1 из 3" not in r.stdout.decode("utf-8", "replace"):
                        bad.append((rel, "mutool не открыл с ремонтом"))
                    continue
                cmd = [g.qpdf_path(), "--check"]
                if password:
                    cmd.append("--password=" + password)
                r = g.run(cmd + [p], check=False)
                if r.returncode not in (0, 3):
                    bad.append((rel, "qpdf --check = %d: %s" % (r.returncode, r.stdout.decode("utf-8", "replace")[-200:])))
                try:
                    doc = pdfium.PdfDocument(str(p), password=password or None)
                    page = doc[0]
                    img = page.render(scale=0.25).to_pil()
                    if img.size[0] < 10:
                        bad.append((rel, "pdfium: пустой рендер"))
                    page.close()
                    doc.close()
                except Exception as e:                      # noqa: BLE001
                    bad.append((rel, "pdfium: %s" % e))
                if d == "tests/data/mini" and p.stat().st_size > 50 * 1024:
                    bad.append((rel, "больше 50 КБ"))
                if d == "tests/fixtures/scans" and p.stat().st_size > 1536 * 1024:
                    bad.append((rel, "больше 1,5 МБ"))
            elif p.suffix.lower() in (".docx", ".xlsx", ".pptx", ".odt"):
                count += 1
                try:
                    with zipfile.ZipFile(p) as z:
                        if z.testzip():
                            bad.append((rel, "битая запись ZIP"))
                except zipfile.BadZipFile as e:
                    bad.append((rel, "ZIP: %s" % e))
            elif p.suffix.lower() == ".rtf":
                count += 1
                if not p.read_bytes().startswith(b"{\\rtf1"):
                    bad.append((rel, "не RTF"))
    checks = [("tests/data/mini/cyr-cid.pdf", None, "Привет, мир"),
              ("tests/data/mini/type1-diff.pdf", None, "Привет, мир"),
              ("tests/fixtures/cyr/word-truetype.pdf", None, "Кириллица"),
              ("tests/data/mini/enc-r6.pdf", "user", "Зашифровано")]
    for rel, password, needle in checks:
        if args.no_word and "word-" in rel:
            continue
        p = root / rel
        if not p.exists():
            bad.append((rel, "нет файла"))
            continue
        txt = g.pdf_text(p, password=password)
        if needle not in txt:
            bad.append((rel, "mutool -F txt: нет «%s»" % needle))
    # копии под именами плана
    for a, b in (("tests/fixtures/forms/forms-all.pdf", "tests/fixtures/forms/fields-all.pdf"),
                 ("tests/fixtures/annots/matrix-28.pdf", "tests/fixtures/annots/annots-all.pdf")):
        pa, pb = root / a, root / b
        if not (pa.exists() and pb.exists()):
            bad.append((b, "нет пары %s" % a))
        elif pa.read_bytes() != pb.read_bytes():
            bad.append((b, "не совпадает байт в байт с %s" % a))
    bad.extend(check_structure(root))
    corpus = Path(args.corpus)
    if (corpus / "big-1200.pdf").exists():
        import pikepdf
        for name, n in (("big-1200.pdf", 1200), ("images-200.pdf", 200)):
            with pikepdf.open(corpus / name) as pdf:
                if len(pdf.pages) != n:
                    bad.append((name, "страниц %d, а не %d" % (len(pdf.pages), n)))
        mut = list((corpus / "mutants").glob("m*.pdf"))
        if len(mut) != 100:
            bad.append(("mutants", "файлов %d, а не 100" % len(mut)))
    for rel, why in bad:
        print("%s: %s" % (rel, why))
    print("verify_fixtures: файлов %d, нарушений %d" % (count, len(bad)))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
