# Сгенерированные фикстуры «Оттиска»

Свои тестовые файлы делают генераторы `tools/gen/` (эталон 9.2, абзац «Генераторы»; план, этап 0,
пункт «Корпуса и фикстуры»). Файлы с чужой лицензией лежат отдельно — `tests/fixtures/pick/` и
`LICENSES.md` (делает `tools/fetch_corpus.py`), здесь их нет.

## Как пересоздать

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\gen\make_all.ps1            # всё + приёмка
powershell -NoProfile -ExecutionPolicy Bypass -File tools\gen\check_determinism.ps1   # два прогона = одни байты
"$env:OTTISK_PYTHON" -X utf8 -I tools/gen/verify_fixtures.py                         # только приёмка
"$env:OTTISK_PYTHON" -X utf8 -I tools/gen/check_scans_ocr.py [01 02 …]               # живой OCR-замер сканов
```

Один генератор: `"$env:OTTISK_PYTHON" -X utf8 -I tools/gen/make_<что>.py [--out <корень>] [--corpus <каталог>]`.
`--out` — корень, под которым создаются `tests/…` (по умолчанию репозиторий), `--corpus` — каталог больших
файлов (по умолчанию `%OTTISK_CORPUS%\gen` или `C:\Tools\ottisk\corpus\gen`; корпус для очереди — только чтение,
поэтому из сессий — `--corpus build\gen`, `make_all.ps1 -Corpus build\gen` или `-SkipBig`; `corpus\gen` пересоздаёт
человек или оператор очереди), временные файлы —
`%TEMP%\ottisk-gen`. Нужны: venv `C:\Tools\venv-test` (pikepdf, pypdfium2, pyHanko, Pillow, numpy,
openpyxl, python-pptx, odfpy), `mutool` из `C:\src\mupdf\platform\win32\x64\Release` (`MUPDF_ROOT`),
`C:\Tools\qpdf\bin\{qpdf,fix-qdf}.exe` (`OTTISK_TOOLS`), шрифты Arial/Times New Roman из `C:\Windows\Fonts`;
для `cyr/` и `office/проба.docx|заметка.rtf` — Microsoft Word (COM; без Word `make_all.ps1` пропускает эти
шаги с предупреждением; шаг Word дольше `-WordTimeout` (300 с) — падает, а не ждёт вечно). Полный прогон —
~80 с (из них `make_big.py` ~45 с), проверка детерминизма — ~170 с. Не запускать `make_all.ps1` и
`check_determinism.ps1` одновременно (общий `%TEMP%\ottisk-gen`) и не держать `office/проба.docx` открытым в Word.

## Детерминизм

Даты — `2026-01-01T12:00:00Z` (`/CreationDate`, `/ModDate`, `/M` аннотаций, XMP, вложения, время подписи),
`/ID` — `deterministic_id` qpdf (исходный `/ID` удаляется перед сохранением), XMP собирается из констант
(UUID — `uuid5` от имени файла), теги подмножеств шрифтов — из хеша набора глифов, случайность — только
`random.Random(1)` / `numpy.default_rng(1)` / `DetRandom(seed)`. Особые случаи:

- **AES-256 R6** делает pyHanko, а не qpdf: qpdf берёт соли `/U /O /UE /OE` и ключ файла из системного ГСЧ
  (`--static-aes-iv` их не фиксирует, `--deterministic-id` с шифрованием не принимается). Соли — из
  `DetRandom(seed)`, IV строк и потоков — из хеша их содержимого, `datetime` pyHanko подменён. RC4-40/128 и
  AES-128 — qpdf `--static-id --static-aes-iv`.
- **Подпись B-B**: ключ RSA 2048 строится из простых чисел, найденных в `random.Random(1)`; подпись PKCS#1
  v1.5 детерминирована; время и `/ID` pyHanko — фиксированы.
- **Word** (`cyr/word-truetype.pdf`, `office/проба.docx`, `office/заметка.rtf`): `tools/gen/normalize_word.py`
  убирает всё сессионное — автора (Word пишет имя пользователя Windows), даты, rsid, `w14:paraId/textId`,
  `wp14:anchorId/editId`, случайные `wp:docPr id`, `docId`, `{\*\datastore}`, WMF-дубль картинки
  `{\nonshppict}`, перенумеровывает шрифты RTF и пересобирает переносы строк RTF. Проверено: два прогона
  совпадают байт в байт (Word 16.0.20430). Если после обновления Word перестанут — внести в
  `tools/gen/nondeterministic.txt` с пометкой «недетерминирован: Word».
- **Что может сдвинуть байты** (тогда `check_determinism.ps1` покажет «репозиторий отличается» — пересоздать
  `make_all.ps1`, посмотреть и закоммитить с причиной): шрифты Windows (`arial.ttf`/`arialbd.ttf` 7.06,
  `times.ttf`/`timesbd.ttf` 7.12), mutool 1.28.5 (сканы, аннотации), версии пакетов venv — закреплённые
  pikepdf 10.17.0, Pillow 13.0.0, numpy 2.5.4, pyHanko 0.38.0 и незакреплённые openpyxl 3.1.5,
  python-pptx 1.0.2, odfpy 1.4.1, cryptography 50.0.2, а также Word.
- Во всех каталогах фикстур — `.gitattributes` `* -text`: `*.txt`, `*.json`, `*.pem` хранятся как есть (LF),
  иначе при `core.autocrlf=true` эталоны и sha256 разошлись бы.

## Имена из плана

Сессии плана называют некоторые фикстуры иначе, чем задание этапа 0. `docs/plan.md` не правится, поэтому:

| План | Здесь |
|---|---|
| `tests/fixtures/forms/forms-all.pdf` (4.4, 4.5, 6.1–6.3) | `forms-all.pdf` = `fields-all.pdf` байт в байт |
| `tests/fixtures/annots/annots-all.pdf` (5.1, 5.2) | `annots-all.pdf` = `matrix-28.pdf` байт в байт |
| `tools/gen/annots_matrix.cpp` (5.1, «этап 0») | `tools/gen/make_annots.py` + `tools/gen/annots.js` (`mutool run`) |
| `tests/fixtures/scans/scan-ru.pdf`, `scan-ru.txt` (8.1) | `scan-01.pdf`, `scan-01.txt` (`scans.json` → `plan_names`) |

Одинаковые файлы Git хранит одним blob — места копии не занимают.

## Что где лежит

### `tests/data/mini/` — `make_mini.py` (каждый ≤ 50 КБ; ожидания — `mini.json`)

| Файл | Что внутри | Кто пользуется |
|---|---|---|
| `blank-a4.pdf` | 1 пустая страница A4 (595,28 × 841,89), без `/Contents` | 1.1, 1.3, 7.1 |
| `cyr-cid.pdf` | Type0 / CIDFontType2, Identity-H, ToUnicode, подмножество Arial; «Привет, мир», «Съешь же ещё…», «ЁЖИК…» | извлечение текста, поиск |
| `type1-diff.pdf` | Type1 Helvetica (base-14, **не встроен**), `/WinAnsiEncoding` + `/Differences`: латиница на кодах 1…N, кириллица cp1251 → `afii100xx`; `/Widths` из метрик Arial | текст по именам глифов |
| `enc-r6.pdf` | AES-256 V5/R6 (PDF 2.0), пользователь `user`, владелец `owner` | пароли 1.3 |
| `truncated.pdf` | 3 страницы, срезаны xref/trailer/startxref и конец последнего объекта; MuPDF: «cannot find startxref → repairing», все 3 страницы читаются | ремонт 1.1 |

### `tests/fixtures/text/` — `make_text.py` + `stext_expected.py` (сессия 1.4; ожидания — `text.json`, `stext_expected.json`)

| Файл | Что внутри | Кто пользуется |
|---|---|---|
| `search.pdf` | 3 страницы, Type0 Identity-H + ToUnicode (Arial, Arial Bold, Arial Italic): «Оттиск», «отти́ск» с U+0301, «оттиск», «оттиска», годы 1999/2026 и 12345, лигатуры U+FB01/U+FB02, жирная/курсивная/красная строки, две строки для выделения; стр. 2 — «оттиск, ОТТИСК и Оттиск», 2027; стр. 3 — без искомого | текстовый слой, поиск, выделение, экспорт |
| `vectors.pdf` | подписи с подчёркиваниями-отрезками (`m l S`, 0,75 pt) ×3, рамка `re S`, заливка `re f` | `TextPage::Options{collectVectors}` |
| `truetype-nonembedded.pdf` | простой TrueType `/Arial` **без программы шрифта**, WinAnsi + `/Differences` (cp1251 → `afii100xx`), без ToUnicode | кириллица при подмене шрифта |
| `text.json` | строки генератора (страница, текст, кегль, базовая линия и x0/x1 в пунктах MuPDF — y вниз), число векторов `vectors.pdf` | |
| `stext_expected.json` | слова и координаты PDFium (pypdfium2 5.14.0): точка пера, чернильная рамка, кегль — для `cyr-cid`, `type1-diff`, `truetype-nonembedded`, `search`; `embedded` — можно ли сравнивать чернила | `unit.stext.words_match_pdfium_reference` |

### `tests/fixtures/view/` — `make_view.py` (сессия 2.2; ожидания раскладки — `layout_expected.json`, вручную)

| Файл | Что внутри | Кто пользуется |
|---|---|---|
| `layout-mixed.pdf` | 5 страниц: Letter; A4 с `/Rotate 90`; A4; 300 × 400; Letter с `/Rotate 100` (MuPDF → 90); на каждой «Страница N», рамка и цветной квадрат в левом верхнем углу; `/PageLayout /TwoColumnRight` | `ui.page_view_layout` (размеры, `/PageLayout`), `ui.page_view_render` |
| `layout-mixed.json` | MediaBox, `/Rotate` и размер на экране каждой страницы | |
| `layout_expected.json` | рамки страниц и размеры содержимого `layoutPages()` для пяти режимов и поворота вида — **посчитаны вручную** по правилам `ui/view/layout.h`, не генератором | `ui.page_view_layout` |

### `tests/fixtures/annots/` — `make_annots.py` (+ `tools/gen/annots.js`)

`annots-all.pdf` — байт в байт копия `matrix-28.pdf` под именем из плана (сессии 5.1, 5.2).

`matrix-28.pdf` — одна страница A4, сетка 4×7, **ровно 28 аннотаций — по одной на подтип** (ISO 32000-2,
табл. 171), PDF 2.0. MuPDF создаёт и рисует `/AP` для Text, FreeText, Line, Square, Circle, Polygon,
PolyLine, Highlight, Underline, Squiggly, StrikeOut, Caret, Ink, FileAttachment (`заметка.txt`), Sound
(поток 8 кГц), Stamp (Approved), Redact (не применена) и Link (`createLink`, URI); Popup — окно Text из
ячейки 1, прямоугольник в ячейке 15. pikepdf дописывает Widget (поле `matrix_text`), Screen, PrinterMark,
TrapNet (последний в `/Annots`), Watermark (`/FixedPrint`), 3D (`/3DD` — заглушка, не U3D), RichMedia
(`clip.mp4` — заглушка), Projection, Movie (`clip.mov` не существует). **Без внешнего вида**: Link, Popup,
Projection (MuPDF при рендере пишет «cannot create appearance stream for Projection» — это ожидаемо).
`annots.json` — для каждой: подтип, `/NM` (`ottisk-NN-<подтип>`), `/Rect`, ячейка, кто создал, `appearance`
(`mupdf` | `custom` | `none`), порядок в `/Annots`.

### `tests/fixtures/forms/` — `make_forms.py` (ожидания — `forms.json`)

- `forms-all.pdf` (имя из плана: 4.4, 4.5, 6.1–6.3) и `fields-all.pdf` (имя из задания этапа 0) — **один и тот
  же файл байт в байт**. Tx (`name`, многострочное `comment`, `comment_ru` шрифтом Type0 `/FRu` с полным набором
  кириллицы, `date` с `AFDate_FormatEx`, `a`/`b` с `AFNumber_*`, у `a` ещё `AFRange_Validate`, `total` =
  `AFSimple_Calculate("SUM")`, только чтение, в `/CO`; `qty` — своя проверка `/AA /V`: не цифры → `app.alert` и
  `event.rc = false`; `secret` — пароль; `pin` — comb, `/MaxLen 6`), Btn (флажок `agree`, радиокнопки `color`
  ×3 red/green/blue, кнопки `reset` = ResetForm, `hello` = JavaScript `app.alert`, `submit_mail` = SubmitForm
  `mailto:forms@example.org` FDF (`/Flags 0`), `submit_http` = SubmitForm `https://example.org/ottisk/submit`
  HTML POST (`/Flags 4`)), Ch (`city` выпадающий, `country` выпадающий **редактируемый**, `langs` список с
  мультивыбором), Sig (`sig`, пустое). `/DV` = исходное `/V` у всех полей со значением, кроме `comment` (сброс его
  очищает); страница `/Tabs /R`. У всех свой `/AP`, `NeedAppearances false`, JS документа `ottisk-init`. `/V` чисел
  хранится без форматирования (`1200.5`), `/AP` — с форматом (`1,200.50`). Проверено `mutool run`: `qty` ← «abc»
  отклоняется, ← «42» принимается; `a` ← 1234.5 → `total` 2034; `country` принимает «Italia».
- `xfa-stub.pdf` — `/AcroForm /XFA` массивом пакетов (preamble, config, template, datasets, postamble),
  `NeedsRendering false`, запасное поле AcroForm `form1[0].page1[0].name[0]` — для полосы XFA (6.1).

### `tests/fixtures/imgcmp/` — `make_imgcmp.py` (метрики — `expected.json`; сессия 1.1)

- Три пары PNG 160 × 120 на синтетической «странице»: `noise-*` (±1…3 в 188 пикселях — в пороге по умолчанию),
  `shift-*` (сдвиг на 1 px), `block-*` (чёрный прямоугольник 24 × 16). Метрики (эталон 9.3, как у
  `pdfium_diff --metrics`) посчитаны независимым оракулом на numpy float64 — бинарника pdfium_diff на машине нет;
  с ними сверяется `src/core/render/image_compare.cpp` (`unit.imgcmp.*`).

### `tests/fixtures/enc/` — `make_enc.py` (пароли и права — `passwords.json`)

`plain.pdf` (основа, 2 страницы) и шесть зашифрованных: `enc-rc4-40.pdf` (V1/R2), `enc-rc4-128.pdf` (V2/R3,
права: печать low, без правки и извлечения, `P=-3388`), `enc-aes-128.pdf` (V4/R4), `enc-aes-256-r6.pdf`
(V5/R6), `enc-aes-256-emptyuser.pdf` (R6, пустой пароль пользователя — открывается без пароля),
`enc-aes-256-unicode.pdf` (R6, пароли «пароль»/«владелец», SASLprep UTF-8). Пароли: `user` / `owner`.

### `tests/fixtures/sig/` — `make_sig.py` (ожидания — `sig.json`)

`key.pem` (RSA 2048, PKCS#8 без пароля — тестовый, не секрет), `cert.pem` (самоподписанный, CN=Оттиск тест,
O=Оттиск, C=RU, 2026-01-01 + 3650 дней, KeyUsage digitalSignature+nonRepudiation), `unsigned.pdf`,
`signed-bb.pdf` — PAdES B-B (ETSI.CAdES.detached, SHA-256, видимое поле `Signature1`, время 2026-01-01T12:00Z).
`pyhanko sign validate` → `INTACT:UNTRUSTED,UNTOUCHED`; с `--trust cert.pem --trust-replace` →
`INTACT:TRUSTED,UNTOUCHED` (запуск pyhanko.exe — с `PYTHONUTF8=1`, иначе «Generic processing error» на кириллице).
Полная PKI (ЦС, TSA, OCSP/CRL, `.pfx`) — `tests/fixtures/pki/`, сессия 4.1.

### `tests/fixtures/scans/` — `make_scans.py` (параметры — `scans.json`; каждый ≤ 1,5 МБ)

`scan-01…10.pdf` — A4 300 dpi, растр на всю страницу, и эталоны `scan-NN.txt` (UTF-8, LF, строки как на
странице). План 8.1 называет `scan-ru.pdf`/`scan-ru.txt` — это `scan-01` (русский, без поворота); «перекос 3°» —
`scan-03` (+3,0°, без поворота); соответствие — в `scans.json` → `plan_names`. Копий нет: Р4 считает долю слов
ровно по 10 файлам `scans/*.pdf`. Языки: ru (01, 04, 08, 10), en (02, 05), de (03, 06), ru+en (07), de+en (09); повороты содержимого
по часовой 0/90/180/270; перекос от −3° до +3°; размытие 0,6, тон бумаги, шум σ 2,5, точки; режимы — JPEG
серый (01–06), 1 бит Flate (07–08), JPEG цветной (09–10). Замер `check_scans_ocr.py` (mutool 1.28.5,
`tessdata_fast`): прямые страницы 01/02/03/07 — 0,98–1,00 доли слов; повёрнутые 90/180/270 без определения
ориентации — 0,01–0,13 (это работа Р4/8.x). **Грабли MuPDF:** в `-F ocr.txt` кириллические буквы разделены
пробелами («П р и в е т»), слова — переводами строк; `check_scans_ocr.py` склеивает буквы перед подсчётом.

### `tests/fixtures/office/`

`make_office_fixtures.ps1` (Word COM): `проба.docx` — заголовок «Привет», абзацы кириллицы, `photo.jpg`;
`заметка.rtf` — «Заметка», кириллица и латиница, `logo.png`. `make_office_fixtures.py`: `таблица.xlsx`
(лист «Города», формулы `SUM` и доли, формат чисел, `chart.png`, второй лист «Заметки»), `слайды.pptx` (3 слайда,
список, `photo.jpg`), `письмо.odt` (ODF, заголовок, абзацы, `logo.png`). ZIP пересобраны с датой 1980-01-01.

### `tests/fixtures/cyr/` — `make_cyr_fixtures.ps1` (Word COM → `ExportAsFixedFormat(…, 17)`)

`word-truetype.pdf` — PDF из Word: TrueType-подмножества Arial и Times New Roman (Type0 Identity-H и
простой TrueType WinAnsi), заголовки → закладки, теги (`/StructTreeRoot`), таблица, картинка. CID Identity-H и
Type1 с `/Differences` — в `tests/data/mini`.

### `tests/fixtures/demo/` — `make_demo.py` (сводка — `demo.json`) — для сцен и `--smoke`

`report.pdf` (12 стр.: обложка с фото, оглавление со ссылками `/Dest`, 5 глав, векторная диаграмма, таблица,
PNG с альфой, колонтитулы, закладки 7 + 10, метки i, ii, 1…10, `PageMode UseOutlines`), `form.pdf` («Заявка на
пропуск», кириллица в полях, дата и число с JS), `comments.pdf` (3 стр., 11 комментариев двух авторов, ответ
`demo-reply-1` → `/IRT demo-note-1`), `mixed-sizes.pdf` (A4, A4 альбомная, A3, Letter, A5, `/Rotate 90`, `/CropBox`).

### `tests/fixtures/images/` — `make_images.py` (сводка — `images.json`)

`photo.jpg` (1200×800), `logo.png` (512×512 RGBA), `chart.png`, `page-gray.png` (A4 150 dpi), `cmyk.jpg`
(CMYK, маркер Adobe), `multipage-g4.tif` (2 страницы CCITT G4, 200 dpi).

### Вне Git: `C:\Tools\ottisk\corpus\gen\` (или `%OTTISK_CORPUS%\gen`)

- `make_big.py`: `big-1200.pdf` (1 200 страниц, закладки 12 × 10, объектные потоки, ~2,2 МБ),
  `images-200.pdf` (200 страниц: JPEG RGB 2000×1500, JPEG серый каждая 7-я, Flate + SMask каждая 10-я, ~17 МБ),
  `big.json` (страницы, байты, sha256).
- `make_mutants.py`: `mutants/m001…m100-<вид>-<источник>.pdf` + `index.json` (seed 1): trunc ×15, startxref ×10,
  length ×15, kids ×8 (MuPDF: «cycle in page tree»), prev ×8 (петля `/Prev`), bomb ×4 (16/64 МБ, двойной Flate,
  1 бит 20000²), bitflip ×20, size ×5, count ×5, nesting ×4 (до 100 000 уровней — MuPDF «exception stack
  overflow»), xref ×6. Ожидание для всех: процесс жив, ремонт или `FZ_ERROR_*`; прогон `mutool draw` по
  всем 100 — без падений и зависаний (≤ 0,3 с на файл).
