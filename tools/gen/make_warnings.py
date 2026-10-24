# -*- coding: utf-8 -*-
"""make_warnings.py — фикстура сканера предупреждений перед подписанием (план 4.5, п. 3; sign-17):
tests/fixtures/sig/warnings.pdf.

Что внутри (две страницы A4, у каждой — пометка в своём месте):
  * каталог: /Names /JavaScript (документный сценарий «init»), /OpenAction — JavaScript, /AA /WC — JavaScript,
    /Names /EmbeddedFiles (файл «data.txt»), /AcroForm с /NeedAppearances true и /XFA (пустой пакет);
  * страница 1: /AA /O — JavaScript; ссылка /Launch (calc.exe), ссылка /URI (https://example.com/), ссылка /GoToR
    (other.pdf); текстовое поле «amount» с /AA /K — JavaScript;
  * страница 2: /RichMedia (без содержимого), /FileAttachment (вложение «note.txt»), /Sound, ссылка с цепочкой
    /Next: /GoTo → /JavaScript;
  * закладка «Сценарий» с действием /JavaScript.
Ожидания — tests/unit/warnings_scan_test.cpp. Откуда: pikepdf (venv). Детерминизм: static_id, без /Info и XMP.
Запуск: "C:\\Tools\\venv-test\\Scripts\\python.exe" -X utf8 -I tools/gen/make_warnings.py [--out <корень>]
"""
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))

import _genlib as g  # noqa: E402
import pikepdf  # noqa: E402
from pikepdf import Array, Dictionary, Name, String  # noqa: E402


def js(code: str) -> Dictionary:
    return Dictionary(Type=Name.Action, S=Name.JavaScript, JS=String(code))


def link(pdf: pikepdf.Pdf, rect, action: Dictionary):
    return pdf.make_indirect(Dictionary(Type=Name.Annot, Subtype=Name.Link, Rect=Array(rect), Border=Array([0, 0, 0]),
                                        A=action))


def embedded(pdf: pikepdf.Pdf, name: str, data: bytes) -> Dictionary:
    stream = pdf.make_stream(data)
    stream.Type = Name.EmbeddedFile
    return pdf.make_indirect(Dictionary(Type=Name.Filespec, F=String(name), UF=String(name),
                                        EF=Dictionary(F=stream)))


def build() -> pikepdf.Pdf:
    pdf = pikepdf.new()
    for _ in range(2):
        pdf.add_blank_page(page_size=(595, 842))
    p1, p2 = pdf.pages[0], pdf.pages[1]
    root = pdf.Root

    root.Names = Dictionary(
        JavaScript=Dictionary(Names=Array([String("init"), pdf.make_indirect(js("app.alert('init');"))])),
        EmbeddedFiles=Dictionary(Names=Array([String("data.txt"), embedded(pdf, "data.txt", b"data\n")])),
    )
    root.OpenAction = pdf.make_indirect(js("console.println('open');"))
    root.AA = Dictionary(WC=js("console.println('close');"))

    field = pdf.make_indirect(Dictionary(
        Type=Name.Annot, Subtype=Name.Widget, FT=Name.Tx, T=String("amount"), Rect=Array([72, 600, 272, 624]),
        P=p1.obj, DA=String("/Helv 10 Tf 0 g"), AA=Dictionary(K=js("AFNumber_Keystroke(2, 0, 0, 0, '', true);"))))
    xfa = pdf.make_stream(b'<xdp:xdp xmlns:xdp="http://ns.adobe.com/xdp/"></xdp:xdp>')
    helv = pdf.make_indirect(Dictionary(Type=Name.Font, Subtype=Name.Type1, BaseFont=Name.Helvetica))
    root.AcroForm = Dictionary(Fields=Array([field]), NeedAppearances=True, XFA=xfa, DA=String("/Helv 10 Tf 0 g"),
                               DR=Dictionary(Font=Dictionary(Helv=helv)))

    p1.obj.AA = Dictionary(O=js("console.println('page open');"))
    p1.obj.Annots = Array([
        link(pdf, [72, 700, 272, 720], Dictionary(Type=Name.Action, S=Name.Launch, F=String("calc.exe"))),
        link(pdf, [72, 670, 272, 690], Dictionary(Type=Name.Action, S=Name.URI, URI=String("https://example.com/"))),
        link(pdf, [72, 640, 272, 660], Dictionary(Type=Name.Action, S=Name.GoToR, F=String("other.pdf"),
                                                  D=Array([0, Name.Fit]))),
        field,
    ])

    sound = pdf.make_stream(b"\x00" * 16)
    sound.R = 8000
    p2.obj.Annots = Array([
        pdf.make_indirect(Dictionary(Type=Name.Annot, Subtype=Name.RichMedia, Rect=Array([72, 700, 272, 760]),
                                     RichMediaContent=Dictionary(Configurations=Array([])))),
        pdf.make_indirect(Dictionary(Type=Name.Annot, Subtype=Name.FileAttachment, Rect=Array([300, 700, 320, 720]),
                                     FS=embedded(pdf, "note.txt", b"note\n"), Contents=String("note.txt"))),
        pdf.make_indirect(Dictionary(Type=Name.Annot, Subtype=Name.Sound, Rect=Array([340, 700, 360, 720]),
                                     Sound=sound)),
        link(pdf, [72, 640, 272, 660],
             Dictionary(Type=Name.Action, S=Name.GoTo, D=Array([p1.obj, Name.Fit]),
                        Next=js("console.println('next');"))),
    ])

    outline = pdf.make_indirect(Dictionary(Title=String("Сценарий"), A=js("console.println('bookmark');")))
    outlines = pdf.make_indirect(Dictionary(Type=Name.Outlines, First=outline, Last=outline, Count=1))
    outline.Parent = outlines
    root.Outlines = outlines
    return pdf


def main() -> int:
    args = g.common_args("фикстура сканера предупреждений перед подписанием")
    out = Path(args.out) / "tests" / "fixtures" / "sig" / "warnings.pdf"
    out.parent.mkdir(parents=True, exist_ok=True)
    pdf = build()
    pdf.save(out, static_id=True, compress_streams=True, object_stream_mode=pikepdf.ObjectStreamMode.disable,
             deterministic_id=False, min_version="1.7")
    g.say(f"{out} ({out.stat().st_size} bytes, sha256 {g.sha256_file(out)[:16]})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
