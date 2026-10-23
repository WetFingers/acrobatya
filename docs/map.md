# Карта кода «АкроБати»

Где что лежит и как добавляется типовая вещь. Сессия читает этот файл **первым**, до поиска по коду.
Дерево и состав — эталон `docs/design.md` 5.1–5.3; здесь — короткий путеводитель и рецепты.

> **Состояние: сессия 1.2 — поток документа, кеш, рендер-пул и тайлы.** Есть `MuContext::global()` (клоны на
> документ и поток, `forThisThread()`, `family()`), `MuError` с `ErrorKind`, `mu::call`, RAII `mu::*Handle`,
> `Document::open → OpenResult` (пароли, ремонт, PDF/A, подписи, форма, XFA, свойства, права), `Renderer` и
> `renderDisplayList`, `DisplayList`/`DisplayCache` (LRU 40), `tiles.h` (`TileKey`, одна матрица, тайлы 512, превью
> 1/8), `fit.h` (`fitZoom`, проценты, раскладка), `DocumentSession` на своём потоке (очередь с приоритетами,
> `post`/`edit`/`postSync` → `QFuture`, `cancelAll`, `TaskFailed`), `DocumentRegistry`, `RenderPool`, `TileCache`,
> `MemoryGuard`, `bench.exe` (Release) с базой `docs/bench/baseline-1.2.json`, `imgcmp --cross`, эталоны рендера
> бит в бит, `engine.corpus.*`/`engine.mutants.*`/`engine.stress.render`, фаззер через ядро. Из каркаса этапа 0:
> пустое окно `MainWindow` (Главная-заглушка, меню, строка состояния; страница — `DocumentSession::renderPage`), режимы
> `--version`, `--self-check`, `--shots`, `--smoke`, `--register-notify`, первые unit/ui/render-тесты, `imgcmp`,
> `render_refs`, стражи, фаззер, установщик 0.0.1. Всё остальное в таблицах ниже появляется в указанной сессии —
> рецепты описывают, как это будет устроено, и ссылаются на файлы, которых может ещё не быть. Сессия, которая
> создаёт новую типовую вещь или меняет её устройство, **дополняет этот файл**.

## Цели CMake (`CMakeLists.txt`, `cmake/*.cmake`)

| Цель | Каталоги (GLOB_RECURSE … CONFIGURE_DEPENDS) | PCH | Зависит от |
|---|---|---|---|
| `ottisk_core` (static) | `src/core/**` | `src/core/pch.h` (MuPDF + std) | MuPDF `.lib` (`cmake/mupdf.cmake`), OpenSSL, tiff, fmt, spdlog, json, sqlite3, Tesseract/Leptonica |
| `ottisk_services` (static) | `src/services/**` | `src/services/pch.h` (QtCore) | `ottisk_core`, Qt Core/Gui/Network/PrintSupport/Concurrent |
| `ottisk_ui_lib` (static) | `src/ui/**` кроме `app/main.cpp` | `src/ui/pch.h` (QtWidgets) | `ottisk_services`, Qt Widgets/Svg |
| `AcroBatya` (exe, WIN32) | `src/ui/app/main.cpp` + `installer/app.rc`, `installer/AcroBatya.manifest` | — | `ottisk_ui_lib` |
| `unit_tests` | `tests/unit/**` | — | gtest, `ottisk_services`; метка `unit` |
| `ui_tests` | `tests/ui/**` | — | QTest, `ottisk_ui_lib`; метка `ui` |
| `engine_tests`, `render_tests`, `e2e_tests` | `tests/engine/**`, `tests/render/**`, `tests/e2e/**` | — | gtest; метки `engine;slow`, `render;slow`, `e2e;slow` |
| `imgcmp`, `render_refs`, `bench` | `tools/imgcmp/**`, `tools/render_refs.cpp`, `tests/bench/**` | — | `ottisk_core` (`bench` — `ottisk_services`: тайлы через `RenderPool`) |
| `pdf_open_fuzzer` | `fuzz/pdf_open_fuzzer.cpp` | — | `ottisk_core` (с `/fsanitize-coverage`); только пресет `msvc-fuzz`, запуск — `tools\fuzz.ps1` |

Все exe — в `build/<пресет>/bin/`. Qt DLL в `build` не копируются: кто запускает exe из `build`, добавляет
`C:\Qt\6.12.0\msvc2022_64\bin` в `PATH` (ctest делает это сам через `ottisk_test_env.cmake`). Версия — одна строка
`set(OTTISK_VERSION "0.N.0")` в `cmake/product.cmake`. Пресеты: `msvc-debug` (`build/debug`, рабочий),
`msvc-release` (`build/release`, рубежи), `msvc-asan`, `msvc-fuzz`, `msvc-analyze`.

**Новый `.cpp`/`.h` кладётся в каталог своей цели — CMake не правится.** Новая цель, каталог вне этих,
правка PCH или `CMakeLists.txt` — только главный исполнитель сессии (после правки PCH `verify.py` может
потребовать один раз `--target clean`).

## Где что лежит (эталон 5.2)

### `src/core/` — без Qt (страж `unit.guard.boundaries`: ни одного `<Q…>`)

| Каталог | Что | Сессия |
|---|---|---|
| `mu/` | `context.h` (`MuContext::global()` — один корень на процесс, `clone()` на документ и поток, `forThisThread()` — thread_local клон для потоков пула, `family()` — корень и клоны с общим store: объекты MuPDF ходят только внутри семьи, `shrinkStore`/`emptyStore`, замки, журнал MuPDF → spdlog), `error.h` (`MuError`, `ErrorKind`: Damaged/Unsupported/Limit/System/Aborted…), `call.h` (`mu::call` — `fz_try` в одном месте, `#pragma warning(disable: 4611)`), `handles.h` (RAII `mu::PageHandle`, `PixmapHandle`, `BufferHandle`… — объявлять снаружи `mu::call`, внутри только `reset`), `stream.h` | 0, 1.1 |
| `doc/` | `document.h` (`Document::open(path, password) → OpenResult`, `openOrThrow` для инструментов, свойства, `allowed(Perm)`, `pageBox`/`pageRotation`; документ живёт в клоне базового контекста), `page.h`, `saver.h`, `journal.h`, `undo_stack.h`, `autosave.h`, `revisions.h`, `props.h`, `permissions.h`, `xmp.h` | 0, 1.1–1.3 |
| `render/` | `renderer.h` (`RenderRequest{page, matrix, scissor, aa, recolor}`, `render(buffer, stride, Bgra32Premultiplied\|Rgb24, cookie)`, `renderPage`, `renderDisplayList(ctx, list, matrix, area, …)` — готовый список своим контекстом), `display_cache.h` (`DisplayList` — `shared_ptr`, свой клон контекста для отдачи в любом потоке, `contentBounds()`; `DisplayCache` — LRU 40, `invalidate(page)`, построение с cookie), `tiles.h` (`TileKey`, `pageMatrix` = scale(zoom·dpr) × rotate × translate, `pageDeviceRect`, `tileRectsFor`/`tilesIntersecting` 512 px, `makeTileKey`, превью 1/8 — `previewZoom`/`makePreviewKey`), `fit.h` (`fitZoom` по режимам, `percentToZoom`/`zoomToPercent` — 100 % = dpi/72, `clampZoom` 1–6400 %, `layoutPageSize` — 0 × 0 → Letter, > 14 400 pt — уменьшить), `bitmap.h`, `image_compare.h` (метрики `imgcmp`, `crossCompare`); дальше `recolor_device.h` | 0, 1.1, 1.2, 2.5b |
| `text/` | `stext.h`, `search.h`, `selection.h`, `index.h` (FTS5), `reflow.h` | 1.4, 8.3 |
| `print/` | `imposition.h` (вписать, n-up, буклет, постер), `page_set.h`, `band_renderer.h` | 2.6 |
| `edit/` | `content_model.h`, `text_blocks.h`, `font_resolver.h`, `reflow.h`, `rewriter.h`, `images.h`, `replace.h` | 3.1–3.4 |
| `sign/` | `cms.h`, `pkcs12.h`, `cng_signer.h`, `tsa.h`, `signer.h`, `verifier.h`, `chain.h`, `revocation.h`, `ltv.h`, `docmdp.h`, `trust_store.h`, `eutl.h`, `aatl.h`, `appearance.h`, `warnings_scan.h` | 4.1–4.6 |
| `annot/` | `annots.h` (минимум в 2.2: `AnnotRef`, `listAnnots`, `setRect`, `remove`; 28 типов — 5.1), `appearance.h`, `popups.h`, `fdf.h`, `xfdf.h`, `stamps.h`, `measure.h`, `summary.h`, `fillsign.h` | 2.2, 5.1–5.5 |
| `forms/` | `fields.h`, `field_factory.h`, `js_bridge.h`, `data_io.h`, `submit.h`, `detect.h`, `tab_order.h` | 6.1–6.4 |
| `pages/` | `organizer.h`, `graft.h`, `split.h`, `labels.h`, `boxes.h`, `insert.h` | 7.1 |
| `create/` | `dispatcher.h`, `image_to_pdf.h`, `pdf_to_images.h`, `tiff_writer.h`, `text_export.h`, `rtf_writer.h`, `combine.h`, `html_to_pdf.h`, `presets.h` | 7.2 |
| `decor/` | `header_footer.h`, `watermark.h`, `background.h`, `artifact.h` | 7.4 |
| `links/` | `outlines.h`, `destinations.h`, `links.h`, `links_auto.h`, `attachments.h`, `layers.h`, `initial_view.h` | 2.4, 2.5, 7.5 |
| `crypt/` | `password.h`, `permissions_write.h`, `policies.h`, `strength.h` | 7.6 |
| `ocr/` | `engine.h` (`TessBaseAPI`), `text_layer.h`, `orientation.h`, `enhance.h`, `suspects.h`, `scan_to_pdf.h` | 8.1–8.2 |
| `a11y/` | `autotag.h`, `struct_tree.h`, `reading_order.h`, `lang.h` | 8.3–8.4 |
| `optimize/` | `optimizer.h`, `qpdf_tool.h` | 9.3 |
| `util/` | `paths.h` (единственная точка UTF-8/wchar и `toShortPath()`), `geometry.h` (`RectF`, `IRect`, `Matrix` как у MuPDF), `features.h` (`OT_FEATURE`); дальше `log.h`, `fs.h`, `hash.h`, `ids.h` | 0, 1.1 и далее |

### `src/services/` — Qt без Widgets (страж: ни одного `QtWidgets`)

`document_session.h` (поток документа: очередь задач с приоритетами, `post`/`edit`/`postSync` → `QFuture`, `cancelAll`,
`displayList(page)`, сигналы `pageChanged`/`documentChanged`/`taskFailed`, `OpenSummary` с размерами раскладки; журнал —
1.3), `task_context.h` (что видит задача: `document()`, `context()`, `cookie()`, `page()` → `PageHandle`, `displayList()`,
`touchPage()`; с MuPDF — ui его не включает), `document_registry.h` (сессии по id вкладки, один файл — одна вкладка),
`render_pool.h` (`RenderPool`: `QThreadPool` idealThreadCount − 2, `submit(TileRequest)` → `tileReady`/колбэк, `cancel`,
`cancelDocument`, `retarget`, `setPriority`), `tile_cache.h` (`TileCache`: `QCache` тайлов 7/8 и превью 1/8 лимита,
384 МБ, `limitFromMegabytes`, `shrink`), `memory_guard.h` (фон и простой 60 с → `fz_shrink_store` 50 % и `lowMemory`),
`app_info.h` (версия, пути для `--self-check`), дальше — `settings.h` + `settings_keys.h`,
`state_db.h` (sqlite), `recent.h`, `http_fetcher.h` (+Fake), `office_converter.h` (+`FakeOfficeBackend`),
`word_converter.h`, `print_job.h`, `scan/{iscansource,wia,fake}.h`, `tts.h` (+Fake), `mail.h` (+Fake),
`shell_register.h`, `help.h`, `i18n.h`, `clipboard_io.h`, `autosave_runner.h`, `trust_updater.h`, `crypto_worker.h`,
`ocr_service.h`, `commands.h` (реестр команд и клавиш), `script_runner.h` (`--script`, 2.4b), `shell_launcher.h` (2.4).

### `src/ui/` — Qt Widgets (страж: ни одного `mupdf/`)

| Каталог | Что | Сессия |
|---|---|---|
| `app/` | `main.cpp` (QApplication, тема, single-instance), `cli.{h,cpp}` (аргументы эталона 3.18) | 0 |
| `shell/` | `main_window.{h,cpp}` (сейчас — пустое окно), `global_bar.h`, `home/`, `document_tabs.h`, `document_view.h`, `tool_panel.h`, `nav_pane.h`, `quick_bar.h`, `status_bar.h`, `message_bar.h`, `command_palette.h`, `preferences/` | 0, 2.3–2.4, 9.1 |
| `theme/` | `tokens.h` (`constexpr`, две палитры), `palette.cpp`, `qss.cpp`, `theme.cpp`, `icons.{h,cpp}` (Lucide с перекраской), `fonts.cpp`, `icon_names.txt` | 2.1 |
| `widgets/` | `ToolButton`, `SegmentedBar`, `SearchField`, `PanelHeader`, `Card`, `ListRow`, `Chip`, `Toast`, `EmptyState`, `MessageBar`, `ColorSwatch`, `NumberField`, `PropertySheet`, `ZoomBox`, `showcase.cpp` | 2.1 |
| `view/` | `page_view.h` (`QAbstractScrollArea`), `layout.h`, `tile_painter.h`, `overlay.h`, `tool_controller.h`, `tools/` (hand, select, marquee_zoom, snapshot, annot_select), `view_history.h` | 2.2, 2.2b |
| `tools/` | `tool.h` (класс `Tool`), `ToolRegistry`, по папке на инструмент Acrobat: `print/ edit/ organize/ comment/ fillsign/ prepareform/ create/ combine/ export/ protect/ compress/ certificate/ scan/ accessibility/ javascript/ measure/` | 2.3 и далее |
| `panes/` | миниатюры, закладки, комментарии, вложения, слои, подписи, поля, назначения, поиск | 2.4 и далее |
| `dialogs/` | диалоги по списку эталона 6.3 | по сессиям |
| `shots/` | `scenes.{h,cpp}` — реестр сцен для `--shots` (таблица сцен в `scenes.cpp`, эталон 6.10 — `SCENES`; сейчас `scenes()`), `fixtures.cpp` | 0, 2.1+ |

### Остальное

| Где | Что |
|---|---|
| `tests/unit/` | gtest, метка `unit`, < 1 с, данные — `tests/data/mini/*.pdf` (свои, ≤ 50 КБ); `support/test_pdf.h` — PDF в памяти (`makePdf`, `makeHeavyPdf` — секунды рендера на 600 dpi, `makeMixedPdf` — текст/кривые/картинки, `makeImagePagesPdf` — 200 страниц с картинками), `support/qt_app.h` — `QCoreApplication` и `waitUntil` для сигналов из потоков |
| `tests/ui/` | QTest offscreen, метка `ui` (ctest `ui.main_window`), снимки виджетов → `build/ui-shots/` |
| `tests/engine/` | gtest на корпусе по `<корпус>/index.json` (`open_corpus_test.cpp`: тег → части по 200 файлов, `progress:`), `mutants_test.cpp`; зонд `support/probe.h` (открыть → 32 × 32 → закрыть, `QuietLog`, `isBrokenFileCode`, `isMemoryFailure`); предел памяти
процесса `support/memory_cap.cpp` (Job Object, `OTTISK_TEST_MEMORY_LIMIT_MB`, 2048 / ASan 4096, 0 — выкл.),
`PROCESSORS 2`; `OTTISK_CORPUS_TRACE=1` — печать каждого файла перед зондом (найти файл падения); срок рендера зонда
`OTTISK_PROBE_RENDER_TIMEOUT_S` (15 / ASan 45 с); пропуск `tests/corpus/slow.json` — файлы, которые сам MuPDF
рисует минутами (`tools/scan_slow_corpus.py --write`); метки `engine;slow`, `FIXTURES_REQUIRED corpus;mutants` (`engine.corpus.verify` — `fetch_corpus.py --verify --dry-run`, 77 — пропуск; `engine.mutants.generate` — `make_mutants.py` в `build/<пресет>/gen`), `TIMEOUT_AFTER_MATCH 120;^progress:` |
| `tests/render/` | `render_test.cpp` по `expected/expected.json`, эталоны `expected/<корпус>/<файл>.<стр>.<dpi>.png`, `files.json` — источники |
| `tests/e2e/`, `tests/compat/`, `tests/bench/`, `tests/build/` | сценарии эталона 2 без UI; `compat_check.py` (SKIP 77); `bench.cpp` → `bench.exe <файл\|hello\|pdf32000\|big-1200\|images-200\|enc-r6>` (только Release: ctest `bench.engine.*`, JSON в `build/bench/`, сравнение — `tools/bench_compare.py docs/bench/baseline-<прошлый>.json "build/bench/*.json"`: +25 % медианы, шум < 5 мс, цели 5.8); `install_test.py` (метка `build`) |
| `tests/corpus/manifest.json` | корпуса `{id, url\|git, sha256, license, tags[], expect}` → `C:\Tools\ottisk\corpus` (`OTTISK_CORPUS`) |
| `tests/fixtures/` | свои фикстуры ≤ 50 КБ и пермиссивные (`LICENSES.md`): `annots/ forms/ enc/ sig/ images/ scans/ office/ scenes/ demo/ imgcmp/ (метрики imgcmp) regress/ (падения фаззера → unit.regress.load) pki/ (4.1)` |
| `tools/` | `vcenv.cmd`, `verify.py`, `build.ps1`, `build_lock.ps1`, `fuzz.ps1` (фаззер 60 с / 10 мин), `shots.ps1`, `lint.ps1`, стражи `*_guard.py` и `scope_check.py`, `scope_report.py`, `imgcmp/`, `render_refs.cpp`, `review_refs.py`, `fetch_corpus.py`, `scan_slow_corpus.py`, `compat_check.py`, `bench_compare.py`, `analyze_summary.py`, `copy_icons.py`, `gen/` (генераторы фикстур), `get-tessdata.ps1`, `install-qt.ps1`, `mupdf-build.ps1` + `ottisk-md.props`, очередь: `queue-lib.ps1`, `test_reset_parse.ps1`, `test_guard.ps1` |
| `installer/` | `ottisk.iss` (UTF-8 BOM, per-user), `app.ico`, `app.rc`, `AcroBatya.manifest` (UTF-8 ACP, longPathAware), `licenses/` |
| `assets/` | `icons/` (только нужные Lucide + `LICENSE`, список `icons.txt`), `stamps/`, `help/ru`, `help/en` |
| `i18n/` | `ottisk_ru.ts`, `ottisk_en.ts`, части `en-parts/` для перевода агентами (9.2) |

### Стражи (ctest `unit.guard.*`, запуск `"$env:OTTISK_PYTHON" -X utf8 -I tools/<имя>.py --root .`; 0 — зелёный, 1 — красный, 77 — пропуск)

`i18n_guard.py` (кириллица только в `tr()`), `fztry_guard.py` (`fz_try` только в `mu::call`), `boundaries_guard.py`
(слои), `scope_check.py` (`docs/scope.json` ↔ `docs/features.json` ↔ ID пунктов плана), `encoding_guard.py`
(BOM у `.ps1`/`.iss`), `no_registry_guard.py` (настройки не в реестре), `format_guard.py` (clang-format).
PowerShell-стражи очереди `tools/test_reset_parse.ps1` и `tools/test_guard.ps1` запускает `tools/verify.py`.
Позже: `icons` (`ui.icons.exist`), `hotkeys` (`unit.guard.hotkeys`).

## Рецепты

### Новый инструмент в `ToolRegistry` (с 2.3)

1. Папка `src/ui/tools/<папка>/` (имя — из эталона 6.2: `comment/`, `organize/`, `protect/`…), класс
   `<Имя>Tool : Tool` в `<имя>_tool.{h,cpp}`. Интерфейс `Tool` (`src/ui/tools/tool.h`): `id()`, `title()`
   (`tr()`), `icon()` (имя Lucide), `stage()` (для серых, ещё не готовых), `activate(DocumentView&)`/`deactivate()`,
   `panel()` (виджет левой панели), `commands()`, `cursorFor()`.
2. Регистрация — одна строка `ToolRegistry::instance().add(std::make_unique<<Имя>Tool>())` в `src/ui/app/main.cpp`;
   оболочка («Все инструменты», QuickBar) не правится — порядок и состав из эталона 6.2.
3. Иконка — имя Lucide в `src/ui/theme/icon_names.txt` + `"$env:OTTISK_PYTHON" -X utf8 -I tools/copy_icons.py`
   (копирует только нужные SVG в `assets/icons/`); неизвестное имя — `qFatal` в Debug и красный `ui.icons.exist`.
4. Состояния мыши — через `ToolController` в `PageView` (не свой обработчик событий поверх виджета).
5. Логика — в `core`/`services` с тестами первыми; инструмент только вызывает `DocumentSession`.
6. Команды и клавиши — рецепт «Новая команда и клавиша»; сцена `tool-<имя>` и просмотр PNG; ui-тест в `tests/ui/`.

### Новая аннотация (ядро — 2.2/5.1, инструменты — 5.1–5.5)

1. `src/core/annot/annots.h`: тип в перечне подтипов, создание/чтение/правка через `mu::call`, значение-тип
   `AnnotInfo` для `ui` (никаких `pdf_annot*` наружу); внешний вид — `annot/appearance.h` (свой AP, не полагаться
   на чужие читалки).
2. Правка — только через `DocumentSession` и журнал (undo/redo); сохранение — правила `Saver` (инкрементально,
   подписи не ломать).
3. Тесты первыми: `tests/unit/annot_<тип>_test.cpp` — создать → сохранить → переоткрыть → свойства и рендер;
   XFDF round-trip (`annot/xfdf.h`) на матрице 28 подтипов (`tests/fixtures/annots/`, генератор `tools/gen/`);
   `compat`: `qpdf --check`, `pdfcpu validate -m strict`, pypdfium2 рендерит (`imgcmp --cross`).
4. Инструмент — в `src/ui/tools/comment/` (рецепт выше), свойства — диалог «Свойства комментария»,
   панель «Комментарии» — `src/ui/panes/`.
5. `OT_FEATURE("comment-NN")` в тестах; строка `CHANGELOG.md`.

### Новый диалог

1. `src/ui/dialogs/<имя>_dialog.{h,cpp}`, класс `<Имя>Dialog : QDialog`; собирается из компонентов
   `src/ui/widgets/` и токенов `src/ui/theme/tokens.h` — без своих цветов и размеров.
2. Строки — только `tr("…")` (русские исходные); мнемоники `&`, кнопка по умолчанию, `Esc` закрывает;
   `QAccessible`-имена у своих виджетов (`ui.a11y.names`); контекстная справка `?` → раздел `assets/help/ru/*.md`.
3. Файловые диалоги — нативные, в тестах `QFileDialog::DontUseNativeDialog`; модальных «подождите» нет —
   прогресс в строке состояния.
4. Данные — через `services` (настройки, `DocumentSession`), не прямые вызовы `core` из диалога.
5. Сцена `dialog-<имя>` (рецепт «Новая сцена»), ui-тест (открыть, заполнить, принять) в `tests/ui/`.

### Новая настройка (с 2.4b)

1. Ключ и умолчание — одна строка в `src/services/settings_keys.h` в группе своей категории
   (`general/`, `pageDisplay/`, `documents/`, `commenting/`, `forms/`, `editing/`, `signatures/`, `trust/`,
   `search/`, `reading/`, `ocr/`, `convert/`, `print/`, `hotkeys/`, `quickbar/`, `language/`); чтение — через
   типизированный `Settings` (`src/services/settings.h`).
2. Хранение — `QSettings(IniFormat)` → `%LOCALAPPDATA%\AcroBatya\settings.ini` (никогда не реестр —
   `unit.guard.no_registry`); в тестах и сценах — `OTTISK_DATA_DIR`. Смена смысла ключа — миграция по `meta/version`.
3. Интерфейс — в категории диалога «Установки» (`src/ui/shell/preferences/`, названия — как в русском Acrobat).
4. Тест: умолчание, запись-чтение, влияние на поведение; сцена категории Установок при новом виде.

### Новая сцена

1. Функция в `src/ui/shots/scenes.cpp` и строка в таблице сцен там же (имя — `kebab-case`); сцена доводит
   `MainWindow` до нужного вида на фикстурах `tests/fixtures/scenes/` и `tests/fixtures/demo/` — без сети и без
   данных человека.
2. Снять: `powershell -NoProfile -ExecutionPolicy Bypass -File tools\shots.ps1 -Only <имя> [-Theme dark]` →
   `.queue/screenshots/<имя>.png` (offscreen, 1440 × 900, Segoe UI из `C:/Windows/Fonts`); список имён —
   `sceneNames()` в `scenes.h` (ключ `--shots-list` — эталон 6.10, когда появится).
3. **Посмотреть PNG** (`Read`) и сверить с эталоном 6: размеры 48/320/44/280/32, токены, подписи, иконки,
   кириллица. Главные экраны — и в тёмной теме; 30 главных сцен рубежа — `tools/main_scenes.txt`.
4. Пиксельного эталона у сцен нет; pHash к снимкам прошлого рубежа (`tools/review_refs.py --shots`) — сигнал, не закон.

### Новый тест на корпусе

1. Файл `tests/engine/<тема>_test.cpp` (цель `engine_tests`, метки `engine;slow`, фикстура `corpus`);
   файлы — по тегу из `tests/corpus/manifest.json` (теги эталона 9.2: `annots`, `forms`, `xfa`, `enc-r2..r6`,
   `sig-b/t/lt/lta`, `pdfa-pass/fail`, `big`, `cyr`, `fuzz`, `office`, `scan`, `text-edit`, `tagged`, `trust`)
   из `OTTISK_CORPUS` (`C:\Tools\ottisk\corpus`).
2. Сессии и рубежи корпуса **не качают**: нет тега на диске — `GTEST_SKIP()` и строка в «Нужна ручная проверка»;
   новый тег — только этап 0 / человек (`fetch_corpus.py --verify` сверяет теги плана).
   **Память:** файлы-фаззы заставляют MuPDF выделять гигабайты — тест корпуса живёт в `engine_tests` (предел памяти
   процесса) или ставит свой; `ctest -L engine` — не больше `-j 6` (под ASan `-j 4`): 23.10.2026 десять шардов
   `fuzz` без предела исчерпали память и уронили Windows.
3. Ожидания — JSON рядом с тестом или в `tests/fixtures/` (для XL — до кода); каждый записанный тестом файл
   проходит минимум `qpdf --check` (`tools/compat_check.py`).
4. Маленький пермиссивный файл, нужный быстрому тесту, — копией в `tests/fixtures/` с записью в `LICENSES.md`.

### Задача документа и рендер тайлов (с 1.2)

1. Всё, что трогает `fz_document`/`fz_page`/`pdf_*`, — задачей `DocumentSession`: `post([](TaskContext& tc) { … },
   TaskPriority::…)` → `QFuture<T>`; правка — `edit(QStringLiteral("<операция по-английски>"), fn)` (после неё `revision++`,
   `documentChanged`, `pageChanged` для `tc.touchPage(n)`); в тестах — `postSync`. Задача использует `tc.cookie()` в
   `fz_run_*`/`Renderer::render` — тогда `cancelAll()` её прерывает. Ошибка — исключение в `QFuture` (`MuError`) и
   `taskFailed(TaskFailed{code, kind, message, hint})`; отмена — `MuError` с `ErrorKind::Aborted` и без сигнала.
2. ui не держит указатели MuPDF: только номер страницы, `TileKey`, `shared_ptr<const DisplayList>` (непрозрачен для ui) и
   `QImage`. `PageHandle` (`tc.page(n)`) живёт только внутри задачи.
3. Тайлы: `session.displayList(page)` → `tileRectsFor(list->bounds(), zoom, dpr, rotation)` → `RenderPool::submit(TileRequest{
   makeTileKey(session.id(), page, zoom, dpr, rotation, tile, session.revision()), list, pageMatrix(…), tile.rect, dpr})` →
   результат в `TileCache::insert`; смена зума — `retarget(doc, видимые, приоритет)`; закрытие — `cancelDocument(doc)` и
   `TileCache::removeDocument(doc)`. Превью — `makePreviewKey` + `previewZoom(zoom)` + `insertPreview`.
4. Документы для пула — только на клонах `MuContext::global()` (так открывает `DocumentSession`): список из чужой семьи
   пул отвергает ошибкой. Мозаика тайлов совпадает с целым рендером бит в бит для текста и прямоугольников; у кривых и
   растянутых картинок MuPDF даёт отличия в доли пикселя на рёбрах (GEL режет рёбра по scissor) — швов нет, тесты
   `unit.render.tiles_mosaic_*` держат границы.

### Новая команда и клавиша (с 2.3–2.4)

1. Регистрация в реестре `src/services/commands.h`: id `группа.действие` (`doc.save`, `view.zoomIn`), `tr`-название,
   клавиши по умолчанию — **из таблицы эталона 6.8** (она источник; `unit.guard.hotkeys` сверяет реестр с таблицей
   и справкой).
2. Команды инструмента отдаёт `Tool::commands()`; однобуквенные клавиши инструментов выключены по умолчанию.
3. Конфликт клавиш — подсветка в диалоге «Горячие клавиши» (переназначение — `hotkeys/` в настройках).
4. Действие для рубежей — `ScriptRunner::registerAction(id, jsonSchema, fn)` (`--script`, 2.4b) с тем же id.
5. Палитра команд (`Ctrl+Shift+P`) берёт команды из реестра сама.

### Новый `OT_FEATURE`

1. ID — только из `docs/scope.json` (`full`/`partial`/`nice`) и пунктов плана; новый ID не выдумывается
   (`unit.guard.scope` красный).
2. В теле gtest-теста: `#include "core/util/features.h"` и `OT_FEATURE("view-10");` — пишет
   `RecordProperty("feature", id)` (в `build/<пресет>/gtest-xml/*.xml`) и строку `OT_FEATURE:view-10` в stdout;
   в QTest — только строку в stdout (`<system-out>` в `ctest.xml`).
3. Покрытие — `"$env:OTTISK_PYTHON" -X utf8 -I tools/scope_report.py` («закрыто N из M», к Р5 ≥ 95 % `full`).
4. В рабочем коде макрос не используется.

### Новый страж-скрипт

`tools/<имя>_guard.py` (stdlib + пакеты venv, `--root`), коды 0/1/77, нарушения — `файл:строка: что`;
регистрация `ottisk_guard(<имя> <имя>_guard.py)` в `CMakeLists.txt` (главный исполнитель) → ctest `unit.guard.<имя>`.

### Новая фикстура

Генератор — `tools/gen/<что>.py` (`"$env:OTTISK_PYTHON" -X utf8 -I`, детерминированный: фиксированные `/ID`,
даты, seed — два прогона дают одинаковые sha256) или `.ps1` (Word COM, UTF-8 BOM); результат ≤ 50 КБ — в
`tests/fixtures/<тема>/`, больше — не в Git (генерируется заново).
