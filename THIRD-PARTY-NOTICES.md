# Сторонние компоненты «АкроБати»

«АкроБатя» — настольный PDF-редактор для личного использования. **Программа не продаётся.**
В поставку (папка программы `%LOCALAPPDATA%\Programs\АкроБатя`) входят перечисленные ниже
сторонние компоненты; тексты их лицензий лежат рядом, в папке `licenses\` (в репозитории —
`installer/licenses/`, сторонние части MuPDF — в `licenses\mupdf-thirdparty\`).

## MuPDF и GNU AGPL-3.0

Работа с PDF (рендер, текст, аннотации, формы, страницы, запись, OCR-рендер) построена на
библиотеке **MuPDF 1.28.5** © Artifex Software, Inc., распространяемой по лицензии
**GNU Affero General Public License версии 3** (`licenses\AGPL-3.0.txt`). MuPDF собран из
официального исходного архива без изменений и связан с программой статически.

- Исходный код MuPDF 1.28.5: <https://mupdf.com/releases> (архив `mupdf-1.28.5-source.tar.gz`),
  Git: <https://github.com/ArtifexSoftware/mupdf/tree/1.28.5>
  (зеркало <https://git.ghostscript.com/?p=mupdf.git;a=tag;h=refs/tags/1.28.5>).
- Сторонние части MuPDF (папка `thirdparty/` того же архива) перечислены в таблице ниже.
- По условиям AGPL-3.0 (раздел 6) тот, кто передаёт копию «АкроБати» другому лицу, обязан
  передать вместе с ней и соответствующий исходный код программы на тех же условиях.

## Qt и GNU LGPL-3.0

Интерфейс построен на **Qt 6.12.0** © The Qt Company Ltd. и другие авторы — по лицензии
**GNU Lesser General Public License версии 3** (`licenses\LGPL-3.0.txt`; LGPL-3.0 дополняет
GNU GPL-3.0, её текст — `licenses\GPL-3.0.txt`). Qt подключён динамически: файлы `Qt6*.dll` и
плагины в папке `plugins\` можно заменить своей совместимой сборкой Qt 6.12 — программа
загрузит её без перекомпиляции. Исходный код Qt 6.12.0:
<https://download.qt.io/official_releases/qt/6.12/6.12.0/single/> и
<https://code.qt.io/cgit/qt/qtbase.git/tag/?h=v6.12.0> (модули qtbase, qtsvg, qtimageformats,
qttools, qttranslations). Сторонние компоненты внутри Qt:
<https://doc.qt.io/qt-6/licenses-used-in-qt.html>.

## Таблица компонентов

| Компонент | Версия | Лицензия | Текст | Исходный код |
|---|---|---|---|---|
| MuPDF | 1.28.5 | AGPL-3.0-or-later | `AGPL-3.0.txt` | <https://mupdf.com/releases>, <https://github.com/ArtifexSoftware/mupdf/tree/1.28.5> |
| Tesseract OCR (в составе сборки MuPDF) | 5.5.2 | Apache-2.0 | `Apache-2.0.txt` | `thirdparty/tesseract` архива MuPDF; <https://github.com/tesseract-ocr/tesseract> |
| Leptonica (в составе сборки MuPDF) | 1.87.0 | BSD-2-Clause | `BSD-2-Clause-Leptonica.txt` | `thirdparty/leptonica` архива MuPDF; <https://github.com/DanBloomberg/leptonica> |
| FreeType (в составе MuPDF) | из MuPDF 1.28.5 | FreeType License (FTL) | `mupdf-thirdparty\FreeType-FTL.txt`, `mupdf-thirdparty\FreeType-LICENSE.txt` | `thirdparty/freetype`; <https://freetype.org> |
| HarfBuzz (в составе MuPDF) | из MuPDF 1.28.5 | MIT («Old MIT») | `mupdf-thirdparty\HarfBuzz-MIT.txt` | `thirdparty/harfbuzz`; <https://github.com/harfbuzz/harfbuzz> |
| libjpeg (IJG, в составе MuPDF) | из MuPDF 1.28.5 | IJG License | `IJG-libjpeg.txt` | `thirdparty/libjpeg`; <https://ijg.org> |
| OpenJPEG (в составе MuPDF) | из MuPDF 1.28.5 | BSD-2-Clause | `mupdf-thirdparty\OpenJPEG-BSD-2-Clause.txt` | `thirdparty/openjpeg`; <https://github.com/uclouvain/openjpeg> |
| jbig2dec (в составе MuPDF) | из MuPDF 1.28.5 | AGPL-3.0-or-later | `mupdf-thirdparty\jbig2dec-AGPL.txt`, `AGPL-3.0.txt` | `thirdparty/jbig2dec`; <https://jbig2dec.com> |
| lcms2mt (в составе MuPDF) | из MuPDF 1.28.5 | MIT | `mupdf-thirdparty\lcms2-MIT.txt` | `thirdparty/lcms2` |
| MuJS (в составе MuPDF) | из MuPDF 1.28.5 | ISC | `mupdf-thirdparty\MuJS-ISC.txt` | `thirdparty/mujs`; <https://mujs.com> |
| Gumbo (в составе MuPDF) | из MuPDF 1.28.5 | Apache-2.0 | `Apache-2.0.txt` | `thirdparty/gumbo-parser` |
| Brotli (в составе MuPDF) | из MuPDF 1.28.5 | MIT | `mupdf-thirdparty\Brotli-MIT.txt` | `thirdparty/brotli`; <https://github.com/google/brotli> |
| zlib (в составе MuPDF) | из MuPDF 1.28.5 | zlib | `mupdf-thirdparty\zlib-mupdf.txt` | `thirdparty/zlib`; <https://zlib.net> |
| extract (в составе MuPDF) | из MuPDF 1.28.5 | AGPL-3.0-or-later | `AGPL-3.0.txt` | `thirdparty/extract` |
| cmark-gfm (в составе MuPDF) | из MuPDF 1.28.5 | BSD-2-Clause | `mupdf-thirdparty\cmark-gfm-BSD-2-Clause.txt` | `thirdparty/cmark-gfm` |
| zxing-cpp (в составе MuPDF) | из MuPDF 1.28.5 | Apache-2.0 | `Apache-2.0.txt` | `thirdparty/zxing-cpp`; <https://github.com/zxing-cpp/zxing-cpp> |
| Zint (в составе MuPDF) | из MuPDF 1.28.5 | BSD-3-Clause | `mupdf-thirdparty\Zint-BSD-3-Clause.txt` | `thirdparty/zint`; <https://zint.org.uk> |
| Qt (Core, Gui, Widgets, Svg, PrintSupport, Network, Concurrent; плагины) | 6.12.0 | LGPL-3.0-only | `LGPL-3.0.txt`, `GPL-3.0.txt` | <https://download.qt.io/official_releases/qt/6.12/6.12.0/single/> |
| OpenSSL | 3.5.4 | Apache-2.0 | `Apache-2.0.txt` | <https://github.com/openssl/openssl/releases/tag/openssl-3.5.4> |
| libtiff (без фич `jpeg`, `lzma`, `zip`: в `vcpkg.json` — `"default-features": false`) | 4.7.2 | libtiff (BSD-подобная) | `libtiff.txt` | <https://gitlab.com/libtiff/libtiff/-/tree/v4.7.2> |
| {fmt} | 12.2.0 | MIT | `MIT-fmt.txt` | <https://github.com/fmtlib/fmt/releases/tag/12.2.0> |
| spdlog | 1.17.0 | MIT | `MIT-spdlog.txt` | <https://github.com/gabime/spdlog/releases/tag/v1.17.0> |
| JSON for Modern C++ (nlohmann-json) | 3.12.0 | MIT | `MIT-nlohmann-json.txt` | <https://github.com/nlohmann/json/releases/tag/v3.12.0> |
| SQLite (с FTS5) | 3.54.1 | Public domain | `SQLite-public-domain.txt` | <https://sqlite.org/download.html> (`sqlite-autoconf-3540100`) |
| qpdf (`qpdf.exe`, `qpdf30.dll`; внутри — zlib, libjpeg-turbo, OpenSSL) | 12.4.2 | Apache-2.0 | `Apache-2.0.txt` | <https://github.com/qpdf/qpdf/releases/tag/v12.4.2> |
| libjpeg-turbo (статически в `qpdf30.dll`) | 3.2.0 | IJG + BSD-3-Clause + zlib | `libjpeg-turbo.txt`, `IJG-libjpeg.txt` | <https://github.com/libjpeg-turbo/libjpeg-turbo/releases/tag/3.2.0> |
| zlib (статически в `qpdf30.dll`) | 1.3.2 | zlib | `zlib.txt` | <https://github.com/madler/zlib/releases/tag/v1.3.2> |
| OpenSSL (статически в `qpdf30.dll`, сборка qpdf) | 3.x | Apache-2.0 | `Apache-2.0.txt` | <https://github.com/openssl/openssl> |
| Языковые данные Tesseract `tessdata_fast` (rus, eng, deu) | ветка main, снимок 2026-10-23 (sha256 — в `tools/get-tessdata.ps1`) | Apache-2.0 | `Apache-2.0.txt` | <https://github.com/tesseract-ocr/tessdata_fast> |
| Lucide icons (часть значков — из Feather, MIT) | 1.50 | ISC (+ MIT) | `ISC-Lucide.txt` | <https://github.com/lucide-icons/lucide> |
| Microsoft Visual C++ Runtime (`Microsoft.VC143.CRT`) | 14.44 | условия Visual Studio, «Distributable Code» | — | не открытый код; распространяется по лицензии Microsoft Visual Studio |
| Установщик собран Inno Setup | 6.7.3 | Inno Setup License | — | <https://jrsoftware.org/isinfo.php> |

Обязательные упоминания:

- Portions of this software are copyright © The FreeType Project (<https://freetype.org>). All rights reserved.
- This software is based in part on the work of the Independent JPEG Group.

Сборка: статические библиотеки vcpkg (триплет `x64-windows-static-md-release`, baseline
`857036234d15fbed8f30fed66e898d7725446c0c`, OpenSSL закреплён на 3.5.4), MuPDF и Tesseract/Leptonica —
из `mupdf.sln` (MSVC v143), Qt — официальная сборка `win64_msvc2022_64`.

## Не входят в поставку

| Компонент | Лицензия | Как используется |
|---|---|---|
| LibreOffice 26.8.1 | MPL-2.0 | преобразование Office ↔ PDF; программа вызывает отдельно установленный `soffice`, сама его не содержит |
| Microsoft Word (Microsoft 365) | проприетарная | преобразование PDF ↔ DOCX через COM, если Word установлен |
| Шрифт Segoe UI | проприетарная (часть Windows) | системный шрифт интерфейса, в поставку не копируется |
| GoogleTest, pdfcpu, veraPDF, pypdfium2, pyHanko, pikepdf | разные | только тесты на машине разработки |
