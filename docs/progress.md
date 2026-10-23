# Журнал разработки «АкроБати»

Сюда Claude Code дописывает итоги этапа 0, каждой сессии и рубежа очереди. Правила записи — в `CLAUDE.md`.
Очередь — `docs/plan.md`, эталон — `docs/design.md`, как работает очередь — `QUEUE.md`.

## Нужна ручная проверка

Новые пункты — сверху, с номером сессии или рубежа. Ничего из этого не блокирует очередь: она работает на
`Fake*`-реализациях и фикстурах, живая часть ждёт здесь (эталон 8.1 «Человек отдельно», 9.7).

- **Сессия 2.1 · Тема и витрина на экране (к Р1)**: `AcroBatya.exe --showcase -platform windows` (с
  `C:\Qt\6.12.0\msvc2022_64\bin` в `PATH`) — наведение, фокус клавишей Tab и нажатия живой мышью совпадают с колонками
  витрины; переключатель «Системная» следует смене темы Windows (Параметры → Персонализация → Цвета) без перезапуска,
  включённая контрастная тема Windows даёт «Высокую контрастность»; при масштабе экрана 125–150 % иконки чёткие,
  «Крупные элементы» не обрезают подписи. Очередь проверила всё это под offscreen (снимки `showcase*`).
- **Сессия 1.4 · Экспорт текста и буфер обмена вживую (к Р1)**: после `ctest -R compat.rtf` открыть
  `build/debug/compat/rtf/*.rtf` в Word и WordPad — кириллица, абзацы, жирный заголовок, курсив; Word очередь уже
  проверила через COM («Привет» в тексте), глазами — вид. Буфер обмена и «Снимок» на экране — когда окно 2.2b
  подключит выделение: Ctrl+C из PDF → вставка в Word сохраняет жирный и курсив, снимок вставляется картинкой.
- **Сессия 1.3 · Сохранённое — в Acrobat Reader (к Р1)**: открыть `build/debug/compat/save/**` (после `ctest -L
  compat`) в Acrobat Reader и Edge — без предупреждений и ремонта; `signed/signed-incremental.pdf` — в панели подписей
  подпись действительна, «документ изменён после подписи: добавлен комментарий» (pyHanko и MuPDF строже — считают
  записку на странице подписи изменением); `properties-xmp.pdf` — «Свойства → Описание» и «Дополнительные метаданные»
  показывают новый заголовок и ключевые слова. Автосохранение после настоящего падения окна (снять задачу в
  диспетчере) — когда окно 2.3 подключит `AutosaveRunner`.
- **Между 1.2 и 1.3 · Решить: предел памяти MuPDF в самой программе** (23.10.2026). Битые и «злые» PDF заставляют
  MuPDF выделять гигабайты (FZ_STORE ограничивает только кеш): прогон корпуса фаззов без предела уронил Windows.
  Тесты теперь ограничены Job Object (2 ГБ на процесс), а программа — нет: такой файл может съесть всю память.
  Варианты: (а) оставить как есть; (б) свой аллокатор MuPDF с пределом (например, 4 ГБ или половина ОЗУ) —
  открытие или рендер такого файла заканчивается ошибкой «файл слишком сложный». Решение — строкой сюда; без него
  очередь эталон не меняет.
- **Сессия 1.2 · «Мало памяти» вживую** — `MemoryGuard` сжимает store MuPDF и кеш тайлов при уходе программы в фон и
  после 60 с простоя; проверено событиями без экрана. Когда окно (2.2–2.3) подключит его, проверить на экране: Alt+Tab
  из большого документа → в журнале `low memory: MuPDF store shrunk to 50%`, страница перерисовывается без ошибок.
- **Между 1.2 и 1.3 · vcpkg и корпус — решено 23.10.2026** (пункты сессий 1.1 «Нет клона vcpkg и корпуса» и 1.2
  «Корпус живёт в `C:\Tools\ottisk\corpus`» закрыты): оба были на месте, но в папке пакета приложения Claude, а не
  в настоящей `%LOCALAPPDATA%` (журнал ниже). Теперь `C:\Tools\vcpkg` и `C:\Tools\ottisk\corpus`, `index.json`
  переписан, `build/*` переконфигурированы своим vcpkg. Человеку ничего не нужно.
- **Сессия 1.1 · Корпус после восстановления — сделано 23.10.2026**: `ctest -L render` 130/130 в Debug и Release
  (эталоны из корпуса — commons-ru ×2, adobe-spec — больше не SKIP); `engine` в Debug — 136/136 за 4 мин; под
  `msvc-asan` — 126/136, остальные 10 шардов `fuzz` зелёные после раздела `asan` в `tests/corpus/slow.json` (9 файлов,
  на которых кончается внутренняя память ASan; `fuzz_35` — на грани, прошёл без добавлений). Полный ASan-прогон —
  на рубеже Р1. Человеку ничего не нужно.
- **Этап 0 · Загрузки и LibreOffice — сделано 23.10.2026** (разрешение — поручение человека «начинай этап 0 по
  docs/plan.md»): tessdata rus/eng/deu, корпуса всех тегов плана (1,8 ГБ, теперь в `C:\Tools\ottisk\corpus`),
  LibreOffice 26.8.1 распакован `msiexec /a` без прав в `C:\Tools\libreoffice` (`OTTISK_SOFFICE` записан) —
  `winget`/UAC к 7.3 не нужен. Человеку только посмотреть: при желании поставить LibreOffice штатно (тогда
  обновить `OTTISK_SOFFICE`).
- **К рубежу Р1 · Acrobat Reader (по желанию)** — для живой сверки рендера, буклета и наших файлов глазами.
- **К рубежу Р2 · Сертификат для живой подписи** — `New-SelfSignedCertificate -Subject "CN=АкроБатя тест"
  -CertStoreLocation Cert:\CurrentUser\My -KeyExportPolicy NonExportable` или настоящий от УЦ/на токене;
  подпись с меткой времени DigiCert и проверка в Acrobat Reader и Edge.
- **К рубежу Р4 · Сканер WIA и принтер** — скан со сканера, буклет на бумаге (очередь проверяет их через
  `FakeScanSource` и «Microsoft Print to PDF»).

## Журнал

### 2026-10-23 · Этап 0 · Окружение и очередь
- Сделано (Claude с человеком, интерактивно; участки — workflow с независимой проверкой каждого):
  1. Qt 6.12.0 (`C:\Qt`), MuPDF 1.28.5 (`C:\src\mupdf`) и venv aqtinstall (`C:\Tools\aqt`, v3.3.1.dev166,
     commit `076e1659`) перенесены из scratchpad переименованием (0,8 с); Debug MuPDF пересобран
     `tools/mupdf-build.ps1 -Config Debug` с `tools/ottisk-md.props` (`MultiThreadedDLL`, `/U_DEBUG`), Release —
     `-NoLtcg`; `tools/install-qt.ps1` — запасной путь.
  2. vcpkg — свой клон `%LOCALAPPDATA%\vcpkg` (vcpkg-tool 2026-09-26), `vcpkg.json` (baseline `8570362`,
     openssl `overrides` 3.5.4, tiff без фич, fmt, spdlog, nlohmann-json, sqlite3[fts5], gtest в фиче `tests`),
     `CMakePresets.json` (msvc-debug/release/asan/fuzz/analyze, testPresets offscreen), `CMakeUserPresets.json`
     (не в Git).
  3. `C:\Tools`: venv-test (Python 3.14.6, `tools/requirements-test.txt`; сверх списка — `pyhanko-cli` 0.6.0 для
     команды `pyhanko`), qpdf 12.4.2, pdfcpu 0.16.3, veraPDF 1.30.3 (JBR 25 из Android Studio, `auto-install.xml`),
     SumatraPDF 3.6.1 portable; sha256 qpdf и pdfcpu сверены.
  4. tessdata_fast rus/eng/deu (`tools/get-tessdata.ps1`, размеры и sha256 закреплены); первый OCR
     `mutool draw -t rus+eng -r 300` — 0,2 с на строчную страницу, кириллица распознана.
  5. Каркас CMake по эталону 5.3/5.5 (GLOB по каталогам, PCH на цель + unity, `/MD`, `/external:I`, `/WX` у
     core/services), `src/core` (`MuContext`, `mu::call`, `Document`, `Bitmap`/PNG, `image_compare`, `paths`,
     `OT_FEATURE`), `src/services` (`DocumentSession`, `app_info` с `--self-check`), `src/ui` (`MainWindow`,
     Главная-заглушка, `--shots`, `--smoke`, `--self-check`, `--register-notify`), тесты unit/ui/render, `imgcmp`,
     `render_refs`, `bench` (первая версия), `fuzz/pdf_open_fuzzer.cpp`, стражи i18n/fztry/boundaries/scope/
     encoding/no_registry/format, `docs/scope.json` 186 + 8 + 28 + 30 (252 = 252 ID плана).
  6. Корпуса: `tests/corpus/manifest.json` (81 запись), `tools/fetch_corpus.py` — все 21 тег плана подтверждены
     `--verify` (1,8 ГБ в `%LOCALAPPDATA%\ottisk\corpus`); выборка `tests/fixtures/pick` — 185 пермиссивных PDF
     (15 МБ) с `LICENSES.md`; генераторы `tools/gen/` (`make_all.ps1`, детерминизм — два прогона): mini, матрица
     28 аннотаций, `forms-all.pdf`, шифрование, `sig/{key,cert}.pem` + B-B, кириллица и Office через Word COM,
     10 сканов с эталонами `*.txt`, big-1200 / images-200 / 100 мутантов в `corpus\gen`.
  7. Эталоны рендера: `render_refs --update` на Release — 60 файлов (`tests/render/files.json`), 128 PNG (7,1 МБ);
     `ctest -L render` зелёный на Release и Debug (129/129), `release-only` не понадобился; контакт-листы
     `tools/review_refs.py` просмотрены.
  8. LibreOffice 26.8.1: `msiexec /a … /qn TARGETDIR=C:\Tools\libreoffice` **без прав сработал** (код 0, 150 с,
     1,5 ГБ на диске, MSI 375 МБ, sha256 `c6298b10…b04b5312` совпал), найден
     `C:\Tools\libreoffice\program\soffice.com` → `OTTISK_SOFFICE`; `проба.docx` → `C:\Tools\logs\проба.pdf`
     с «Привет» (код 0, 35 с — первый запуск с созданием профиля `C:\Tools\lo-prof-0`).
  9. `tools/vcenv.cmd`, `verify.py`, `build.ps1`, `shots.ps1`, `lint.ps1`, `build_lock.ps1`, `analyze_summary.py`,
     `compat_check.py`, `bench_compare.py`, `copy_icons.py`, `scope_report.py`, `review_refs.py`; `installer/`
     (`ottisk.iss` per-user, ProgID `AcroBatya.pdf`, Capabilities в HKCU, `app.ico`, `app.rc`, `AcroBatya.manifest`,
     `licenses/`), `THIRD-PARTY-NOTICES.md`; `dist/AcroBatya-0.0.1-setup.exe` (38 МБ): тихая установка, `--self-check`
     с экраном и под offscreen, `--shots`, `--smoke 3`, удаление — `tests/build/install_test.py` зелёный.
  10. Очередь: `run-queue.ps1` + `tools/queue-lib.ps1` (без Docker/Postgres; `Test-Limited`, 24-часовой и
      относительный разбор «resets …», запас 120 с, основная → запасная Fable 5.1 xhigh → ждать → снова основная;
      страж этапа 0 раньше всего, `-Setup` пункты не отмечает), `tools/test_reset_parse.ps1`, `tools/test_guard.ps1`,
      `claude-setup/` (SKILL.md под C++ с «Ultracode» и циклом сборки, `done.mjs` без изменений, `guard.ps1`,
      `settings.json`), `.claude/settings.local.json`, `CLAUDE.md`, `docs/map.md`, `QUEUE.md`, `README.md`, `CHANGELOG.md`.
- Решения:
  - **Ответы человека на вопросы объёма (23.10.2026)** — все три совпали с умолчаниями: security-11 после 1.0
    (заглушка в 7.6, без сессии 7.7), security-34 — замена песочницы подтверждена (защищённый просмотр 2.5b +
    устойчивость парсера 1.1), forms-47 остаётся в объёме (консоль JS в 6.3). Записано в «Правила работы» плана.
  - **OpenSSL 3.5.4** — последний 3.5.* в истории порта (`versions/o-/openssl.json`; дальше порт ушёл на 3.6.x).
    Патчей 3.5.5–3.5.9 в vcpkg нет; для домашней программы приемлемо, FireDaemon 3.5.9 не нужен.
  - **qpdf — portable zip, не vcpkg** (нужен `qpdf.exe` для `{app}` и compat; библиотека в коде не используется) —
    отступление от решения (2), как записано в плане.
  - **tiff без фич** (`default-features: false`): jpeg/zip/lzma тянули libjpeg-turbo и zlib, которые конфликтуют
    символами с libjpeg 9 и zlib внутри `libthirdparty.lib` MuPDF (разный ABI `jpeg_decompress_struct`). CCITT G4,
    LZW и PackBits встроены; TIFF на вход читает MuPDF. Вернуть фичи — только вместе с решением конфликта.
  - **Release MuPDF без LTCG**: линковка `render_refs` с `/GL`-библиотеками заняла 238 с (> 3 мин) → один раз
    `mupdf-build.ps1 -Config Release -NoLtcg -Rebuild` (217 с), `/LTCG` убран из флагов Release; эталоны сняты с неё.
  - Qt-рантайм копируется в `build/<пресет>/bin` (цель `qt_runtime`): `gtest_discover_tests` PRE_TEST запускает exe
    без ENVIRONMENT. Окружение ctest — `build/<пресет>/ottisk_test_env.cmake` через `TEST_INCLUDE_FILES`.
  - В Release регистрируются только `render_tests`: прочие exe там не собираются, а PRE_TEST-обнаружение
    роняло весь ctest.
  - Хук очереди строже 10.6: запрет установки зависимостей, записи в `.git`, справочники и эталоны рендера;
    `msiexec /a` только в `C:\Tools\libreoffice`. Команда хука в `settings.json` находит `guard.ps1` через
    `CLAUDE_PROJECT_DIR` и возвращает его код (иначе запрет 2 превращался в 1 и пропускался).
  - **База `cl /analyze`** (`docs/analyze-baseline.json`) снята на каркасе: 358 записей, почти все — CppCoreCheck
    от макросов gtest в `tests/` (C26426, C26818, C6326). В пресете `msvc-analyze` `/WX` выключен
    (`cmake/warnings.cmake`), иначе предупреждения анализа обрывают сборку core. `analyze_summary.py` читал путь
    только из `artifactLocation.uri`, а MSVC пишет `{"index": N}` на `run.artifacts` — без правки он насчитывал 0
    и страж был бы всегда зелёным; исправлено.
  - **Детерминизм фикстур вместо буквы плана** (`tests/fixtures/GENERATED.md`): AES-256 R6 делает pyHanko с
    `DetRandom(seed)` (qpdf берёт соли R6 из системного ГСЧ), RC4-40/128 и AES-128 — qpdf `--static-id
    --static-aes-iv`; `sig/{key,cert}.pem` — RSA 2048 из `random.Random(1)` через `cryptography`, а не
    `openssl req -x509`, — иначе два прогона генераторов давали разные байты.
  - `expected.json` эталонов лежит в `tests/render/expected/expected.json` (как в «Опоре» 1.1) — под deny и хуком
    вместе с PNG; `render_refs --update`/`--mark-release-only` в Debug-сборке отказывают (код 2).
  - Release-пресет сборки собирает только `AcroBatya render_refs render_tests bench` (`targets` в `CMakePresets.json`).
  - Метки ctest якорятся: `-L "unit|ui"` совпадало и с меткой `build` теста установки (`ui` ⊂ `build`) — ручной
    `ctest -L ui` ставил программу. `verify.py` оборачивает `--labels` в `^(…)$`, в командах — `-L "^(unit|ui)$"`.
  - Фикстуры названы как в плане: `forms/forms-all.pdf` (+ копия `fields-all.pdf`), `annots/annots-all.pdf`
    (+ `matrix-28.pdf`); сканы `scan-01…10`, соответствие именам плана — `scans.json`, раздел `plan_names`.
- Подводные камни:
  - **`vcvars64.bat` переставляет `VCPKG_ROOT` на vcpkg из Build Tools** — первый configure тихо поставил порты
    оттуда. `tools/vcenv.cmd` после vcvars возвращает свой клон (`OTTISK_VCPKG_ROOT` или `%LOCALAPPDATA%\vcpkg`).
  - **`vcpkg x-history` в vcpkg-tool 2026-09 нет** — версии портов смотреть в `versions/<буква>-/<порт>.json`.
  - **`fz_locks_context` в MuPDF 1.28.5 — три поля** (`user, lock, unlock`), а не пять, как в `docs/tech-facts.md`
    (§1.2 и факт 7 раздела 0): справочник ошибается, код сделан по заголовку `fitz/context.h:270`.
  - ctest не передаёт окружение в PRE_TEST-обнаружение gtest; в строках CMake пути только с `/`.
  - Программа WIN32: из PowerShell `& AcroBatya.exe` не ждёт и не отдаёт вывод — запускать через `cmd /c` с
    перенаправлением или из Python; `--self-check` сам подключается к консоли родителя.
  - Qt offscreen без `QT_QPA_FONTDIR` рисует квадраты — `main()` подставляет `%WINDIR%\Fonts`.
  - clang-format (Google, `ReflowComments`) склеивает строки комментария — абзацы разделять пустым `//`.
  - `soffice.com` печатает «Could not find platform independent libraries» (встроенный Python) — безвредно.
  - `qtdiag` Qt 6.12 печатает только «by MSVC 2022»; версия компилятора 19.44.35227.0 — в
    `C:\Qt\6.12.0\msvc2022_64\config_qtbase.summary` (тест пункта 1 «qtdiag печатает msvc 19.44» проверять там).
  - Каталог корпусов `%LOCALAPPDATA%\ottisk\corpus` (эталон 9.2) до переименования совпадал в NTFS с каталогом данных
    `%LOCALAPPDATA%\Ottisk` (регистр не различается). С 23.10.2026 данные программы — `%LOCALAPPDATA%\AcroBatya`,
    каталоги разные; деинсталлятор корпус не трогает.
  - Хук блокирует `curl -o C:\Tools\dl\…`: этап 0 вёлся из сессии вне папки `ottisk`; из очереди `C:\Tools` — только чтение.
- Замеры:

  | Что | Время / размер |
  |---|---|
  | configure Debug первый (vcpkg 14 пакетов: OpenSSL 4,4 мин, libjpeg-turbo 28 с, tiff 25 с, gtest 18 с, остальные < 18 с; четыре из них — jpeg/zlib/lzma/spng от фич tiff — затем сняты) | 467 с |
  | configure Release (порты из бинарного кэша vcpkg) | 15 с |
  | configure повторный | 19–34 с |
  | полная сборка Debug (39 целей, PCH + unity) | 33 с |
  | инкремент одного `.cpp` в ui → `AcroBatya.exe` | 4 с |
  | линковка Debug: `AcroBatya.exe` / `unit_tests` / `render_refs` | 0,6 / 1,7 / 6,8 с |
  | линковка Release с `/GL` MuPDF: `render_refs` / `AcroBatya` | 238 / 74 с |
  | линковка Release без LTCG: `render_refs` / `AcroBatya` | 1,6 / 1,1 с |
  | MuPDF: Debug с props / Release `-NoLtcg` | 130 / 217 с |
  | `verify.py` (configure + сборка + стражи + 28 тестов) | 28–30 с |
  | `render_refs --update` (60 файлов, 128 PNG) / `ctest -L render` | 3 / 9 с |
  | `tools/build.ps1` целиком / ISCC | 41 / 18 с |
  | deploy / установщик | 87 МБ / 38 МБ |
  | `install_test.py` | 9 с |
  | `tools/gen/make_all.ps1` / `check_determinism.ps1` | 79 / 169 с |
  | корпуса на диске / `fetch_corpus.py --verify` | 1,8 ГБ / 4–9 с |
  | LibreOffice: загрузка MSI / `msiexec /a` / первая конвертация | 90 / 150 / 35 с |
  | `test_reset_parse.ps1` / `test_guard.ps1` / один вызов хука | 6–18 / 4–8 / 0,7 с |
  | `lint.ps1 -All` (29 файлов) | 512 с |
  | сборка `msvc-analyze` (configure + сборка) | 52 с |

- Проверки: `py -3.14 tools/verify.py` зелёный (стражи, оба PowerShell-теста очереди, 28 тестов `unit|ui`);
  `ctest -L render` 129/129 на Debug и Release; `dumpbin /directives` Debug-библиотек MuPDF — `MSVCRTD|ucrtd|
  msvcprtd|MDd_DynamicDebug` 0 вхождений, `RuntimeLibrary=MD_DynamicRelease`; `fetch_corpus.py --verify` 0;
  `install_test.py` 0; снимок `home` 1440 × 900 (Segoe UI, кириллица) посмотрен; `scope_check` 252 = 252.

### 2026-10-23 · Этап 0 · Решения человека: Opus 5.5 xhigh и имя «АкроБатя»
- Сделано: очередь по умолчанию — Opus 5.5 с усилием xhigh (было ultracode), запасная Fable 5.1 xhigh: `run-queue.ps1`,
  `tools/test_reset_parse.ps1`, план, эталон, `QUEUE.md`, навык (раздел «Ultracode» — только при `-Effort ultracode`).
  Продукт получил имя «АкроБатя» (AcroBatya): заголовок, установщик, exe/цель `AcroBatya`, ProgID, Capabilities, каталог
  данных, значок (печать «АКРОБАТЯ», буква «А»), документы; внутренние имена `ottisk*`/`OTTISK_*`/`ot` и тексты фикстур — без
  изменений (правило в «Правилах работы» плана). Исправлена ошибка плана и эталона: `fz_locks_context` — три поля, не пять.
- Проверки: те же команды, что в записи «Окружение и очередь», — `verify.py`, рендер Debug и Release, `build.ps1` → `AcroBatya-0.0.1-setup.exe`,
  `install_test.py`, `test_reset_parse.ps1`, `test_guard.ps1`, `-DryRun` → 1.1.

### 2026-10-23 · Сессия 1.1 · Контекст, ошибки, открытие, рендер и эталоны
- Сделано:
  1. **Контекст и ошибки:** `MuContext::global()` (один корень на процесс, клоны с общим store и замками из трёх
     полей), предупреждения и ошибки MuPDF → spdlog (`mupdf: …`, клоны наследуют, колбэки не бросают и снимаются до
     `fz_drop_context`), `MuError::kind()` → `ErrorKind` (Damaged/Unsupported/Limit/System/Argument/Aborted/Other) и
     `codeName()`, `mu::call` с `fz_ignore_error` для ABORT/TRYLATER, RAII `mu::*Handle` (`handles.h`); страж
     `fztry_guard.py` дополнен (лексер не тронут): `fz_var` для локалов, меняемых в `fz_try`, тело `mu::call` без
     `return`/`throw`/вложенного `mu::call` и объектов с деструкторами, проверка `src tests tools fuzz`, самопроверка на
     14 примерах; `features_test.cpp` → `features_macro_test.cpp` (+ проверка `RecordProperty`).
  2. **Открытие:** `Document::open(path, password) → OpenResult{document, ok, needsPassword, repaired, pdfa, isSigned,
     hasForm, hasXfa, error}` (не бросает), `openOrThrow` для инструментов, `authenticate` (владелец — бит 2),
     `isPdf`, `repaired`, `properties()` (`/Info`, формат, версия, линеаризация, шифрование, `/R`, `/P`, PDF/A из XMP),
     `allowed(Perm)`, `pageBox`/`pageRotation`; документ живёт в клоне базового контекста; `DocumentSession` — на
     клоне `global()`. Тесты на JSON фикстур (`mini.json`, `passwords.json` — R2…R6, пустой и кириллический пароль,
     `demo.json` — рамки и `/Rotate`).
  3. **Рендер и эталоны:** `Renderer` (`RenderRequest{page, matrix, scissor, aa, recolor}`, `render(buffer, stride)`
     BGRA premultiplied или RGB, `fz_new_pixmap_with_data` + display list + `fz_close_device` до drop, отмена через
     `fz_cookie`, AA на запрос), `geometry.h` (`IRect`, `Matrix` как у MuPDF); `crossCompare` в ядре и `imgcmp --cross
     --region …`; фикстуры `tests/fixtures/imgcmp` (3 пары, оракул numpy); `render_refs --check` сверяет с эталонами
     (метрики, `--out` для `review_refs.py --actual`, `--json`), `--update --only id`, записи без источника — KEEP;
     `render_test`: Release — `pixels == 0`, Debug — пороги, отдельный тест `hello_world` 72 dpi.
  4. **Корпус и мутанты:** `engine.corpus.open` + части по тегам из `<корпус>/index.json`, `engine.mutants` на 100
     мутантах, зонд `tests/engine/support/probe.h`, ctest-фикстуры `engine.corpus.verify` и `engine.mutants.generate`,
     `TIMEOUT_AFTER_MATCH 120;^progress:` (в `ottisk_gtest_exe` — `PROPERTIES`).
  5. **Фаззер и регресс:** харнес через ядро (до 4 страниц: рамка, рендер 32 × 32, stext, свойства), `ottisk_core` в
     пресете `msvc-fuzz` с `/fsanitize-coverage`, `tools\fuzz.ps1` (семена safedocs, pdf-differences, мутанты, regress),
     `tests/fixtures/regress/README.md`, `unit.regress.load`.
- Решения:
  - **Обход пропавшего vcpkg** — toolchain из Build Tools с `VCPKG_MANIFEST_INSTALL=OFF` поверх уже собранных пакетов
    (см. «Нужна ручная проверка»); не `BLOCKED`: пакеты те же, сеть и установка не нужны.
  - Поле `signed` из плана — `isSigned` (`signed` — ключевое слово C++). `hasForm` — есть поля, кроме подписей
    (документ только с подписью — не «форма для заполнения»); `isSigned` — поле `/FT /Sig` с `/V`.
  - Пароль при открытии пробуется и для документа с пустым паролем пользователя (MuPDF открывает сам) — иначе пароль
    владельца не повышает права.
  - **Эталоны:** рендер теперь через display list (как будут тайлы 1.2); `pdf-differences/OverlappingGlyphClipping`
    отличался от `fz_new_pixmap_from_page` на 4–5 пикселей (Δ ≤ 4, отсечение перекрывающихся глифов) — переснят
    `render_refs --update --only … --reason …` на Release, контакт-лист просмотрен; `release_only` не понадобился.
    Полный `--update` без корпуса выкинул бы 3 записи — поэтому `--only` и KEEP.
  - `pdfium_diff` на машине нет: метрики фикстур imgcmp считает независимый оракул на numpy с теми же определениями
    (эталон 9.3); C++ совпал до 1e-9.
  - `tools/gen/mutate.py` из плана не заведён: `make_mutants.py` этапа 0 уже делает 100 мутантов (seed 1) из 12 своих
    фикстур; в тестах он пишет в `build/<пресет>/gen` (фикстура ctest), корпус для этого не нужен.
  - Фикстура `corpus` — `fetch_corpus.py --verify --dry-run --missing-code 77`: без `--dry-run` сверка пишет
    `<корпус>/index.json`. Пропущенная (77) фикстура не мешает зависимым тестам — они дают SKIP сами.
  - ASan: `_DISABLE_VECTOR_ANNOTATION` и `_DISABLE_STRING_ANNOTATION` (`cmake/asan.cmake`) — MuPDF, gtest, spdlog, fmt
    собраны без ASan, иначе LNK2038 `annotate_string/vector`.
  - Фаззер: MuPDF не инструментирован (патчей и пересборки нет) — покрытие libFuzzer видит только наш код; ASan в
    пресете ловит ошибки кучи через перехват malloc/free, но не выход за границы внутри кода MuPDF.
  - `tools/test_reset_parse.ps1`: копия плана для сценария очереди теперь сбрасывает пункты этапов 1+ в `[ ]` — тест
    ждал «Сессия 1.1» и краснел после закрытия любой сессии.
- Подводные камни:
  - `fetch_corpus.py --verify` без `--dry-run` создал отсутствующий `%LOCALAPPDATA%\ottisk\corpus\index.json` (только
    файлы из репозитория) — удалён тем же ходом.
  - `TIMEOUT_AFTER_MATCH` срабатывает один раз (на первой строке `progress:`), отсюда части по 200 файлов.
  - `fz_pixmap` — открытая структура: начало координат буфера-тайла — `pix->x/y`, матрица не сдвигается.
  - `MuContext` до `fz_new_context` трогает реестр spdlog: так он разрушается после `global()` при выходе.
  - В PowerShell 5.1 `Set-Content -Encoding utf8` пишет BOM; heredoc Git Bash ломает кавычки, а хук очереди читает
    текст heredoc как команду — правки длинных текстов через файл-скрипт.
- Замеры: configure `--fresh` Debug 9 с, Release 8 с, ASan 6 с, fuzz 6 с; сборка `unit_tests` инкрементально 7–15 с,
  Release (4 цели, без LTCG) 20 с, ASan `unit_tests engine_tests` 12 с; `render_refs --check` 118 растров: Release 4,5 с,
  Debug 20,6 с; **Debug ↔ Release по 57 из 60 эталонов (118 растров; 3 — корпус): 118 одинаковых, max delta 0**;
  `engine.mutants`: генерация 6,5 с, тест 7 с (FORMAT 11, SYNTAX 4, открыто 44, с ремонтом 31, пароль 10; дольше всех
  m060-bomb 0,48 с); ASan `unit|engine` — 67 тестов за 16 с, чисто; фаззер 60 с — 2857 прогонов (46/с), 2372 признака
  покрытия, падений нет, RSS 595 МБ.
- Проверки: `verify.py` зелёный; `ctest -L render` 130/130 в Debug и Release; `ctest -L "^(unit|engine)$"` под
  `msvc-asan` зелёный; `tools\fuzz.ps1 -Seconds 60` — 0 падений; экранов сессия не строила — сцен нет.

### 2026-10-23 · Сессия 1.2 · Поток документа, кеш, рендер-пул и тайлы
- Сделано:
  1. **Поток документа:** `DocumentSession` — свой `QThread` с очередью `std::deque` и условной переменной, приоритеты
     (Background…Urgent, FIFO среди равных; правки — Normal по порядку), `post`/`edit`/`postSync` → `QFuture` через
     `QPromise` (исключение задачи — в `QFuture`), `cancelAll` (очередь — `MuError` ABORT, выполняемой — `abort = 1` в её
     `fz_cookie`), `QFuture::cancel()` до начала — задача не выполняется, сигналы `opened`/`closed`/`pageChanged`/
     `documentChanged`/`taskFailed(TaskFailed{task, code, kind, message, hint})`, `OpenSummary` (флаги и размеры
     раскладки); `TaskContext` и `PageHandle` — в `services/task_context.h` (с MuPDF), `document_session.h` MuPDF не
     включает; `DocumentRegistry` (id вкладки = id сессии, один файл — одна вкладка, закрытие после очереди). Окно
     этапа 0 работает через синхронные `open`/`renderPage`.
  2. **Кеш списков и рендер-пул:** `DisplayList` (`shared_ptr`, свой клон контекста документа для отдачи в любом потоке,
     построение с cookie), `DisplayCache` (LRU 40, `invalidate`), `renderDisplayList`; `tiles.h` (`TileKey`, `hashValue`,
     одна матрица `scale(zoom·dpr) × rotate × translate`, `pageDeviceRect` = `fz_round_rect` как у `deviceBounds`, тайлы
     512 по целым device px, `tilesIntersecting`); `RenderPool` (`QThreadPool` idealThreadCount − 2, `thread_local` клон
     `MuContext::forThisThread()`, `QImage` ARGB32_Premultiplied без копий, `tileReady` + колбэк через `invokeMethod`
     (QueuedConnection), `cancel`/`cancelDocument`/`retarget`/`setPriority` — не начатые снимаются `tryTake`);
     `TileCache` (`QCache` по байтам, 384 МБ).
  3. **Превью, HiDPI, память:** `fit.h` — `fitZoom` (реальный размер по DPI, страница, ширина, высота, видимое по
     `DisplayList::contentBounds`), проценты 1–6400 %, `layoutPageSize` (0 × 0 → Letter, > 14 400 pt — уменьшить с
     сохранением пропорций; `OpenSummary` отдаёт размеры раскладки); превью 1/8 — `previewZoom`/`makePreviewKey`, одно на
     1/8 октавы, отдельный `QCache` (1/8 лимита); `TileCache::limitFromMegabytes` (64–4096), `shrink`; `MemoryGuard`
     (`ApplicationDeactivate`/неактивное состояние и простой 60 с → `fz_shrink_store` 50 % + `lowMemory`).
  4. **Бенчмарк:** `tests/bench/bench.cpp` — открытие → первый растр, раскладка, смена страницы без кеша и из кеша
     списков, первый тайл зума 200 %, тайлы стр. 1–50 при 100 и 200 % через `RenderPool`, `PeakWorkingSetSize`; ctest
     `bench.engine.{hello,pdf32000,big-1200,images-200,enc-r6}` (только Release, `RUN_SERIAL`, SKIP 77 без файла) и
     фикстура `bench.engine.generate` (`make_big.py --skip-existing`); `bench_compare.py` — цели 5.8 (`--strict-targets`)
     и порог шума; база `docs/bench/baseline-1.2.json`.
  5. **Стресс под ASan:** `engine.stress.render` — 20 потоков-заказчиков × 200 страниц корпуса (98 файлов: big, cyr,
     annots, forms) через `DocumentSession` и общий `RenderPool`, случайные приоритеты и отмены (тайл, документ в пуле,
     вся очередь сессии, `retarget`), закрытие одного документа и разрушение сессии другого посреди работы; проверки:
     у каждого тайла ровно один результат, одинаковый ключ — одинаковые пиксели, ошибки только ожидаемые.
- Решения:
  - **Мозаика == целое — бит в бит только для текста и прямоугольников.** MuPDF режет рёбра контуров по scissor
    (GEL: `clip_lerp` с целочисленной интерполяцией, DDA стартует с нового конца ребра) и считает шаг аффинной отрисовки
    картинки от начала отсечения — у кривых отличия ≤ 17/255 на рёбрах (≤ 0,1 % пикселей), у растянутых картинок граница
    тексела сдвигается на 1 px. Это не швы и не ошибка раскладки; без патча MuPDF не устранить. Тесты: текст и
    прямоугольники — `pixels == 0` в 4 вариантах (поворот 0/90/270, dpr 1/1,25/1,5); кривые — maxΔ ≤ 24, доля < 0,2 %,
    MSE ≤ 0,05; картинки — доля < 0,5 %, SSIM ≥ 0,999; параллельно == последовательно — бит в бит.
  - `TileKey` дополнен полем `zoomMilli` (точный масштаб, тысячные): `zoomBucket` (1/8 октавы) из плана не задаёт растр
    однозначно — тайл 100 % отдавался бы на запрос 104 % с другой сеткой. Корзина осталась: по ней — превью (одно на 1/8
    октавы). Масштаб и dpr везде округляются до тысячных — матрица, рамка и ключ согласованы.
  - Масштаб `zoom` — логические пиксели на пункт (матрица `scale(zoom·dpr)` эталона 4.2); проценты интерфейса —
    относительно `dpi/72` (100 % = реальный размер при выбранном DPI экрана; DPI — системный или физический, решает ui).
  - `DisplayList` держит свой клон контекста документа и отдаёт им список (последняя ссылка умирает в любом потоке);
    «семья» контекстов (`MuContext::family()`) проверяется пулом — список документа с отдельного корня (`MuContext mu;
    Document::open(mu, …)`) пулу нельзя: чужие store и замки. `MuContext::clone()` сериализован мьютексом семьи
    (`fz_clone_context` копирует исходный контекст `memcpy`).
  - Отмена посреди списка: draw-устройство не закрывается — `fz_close_device` бросал `FZ_ERROR_ARGUMENT` «items left
    on stack» (clip/group на стеке), и отмена выглядела ошибкой; стек освобождает `drop`, `close_device = nullptr`
    убирает предупреждение «dropping unclosed device». Найдено стрессом, тест `unit.render.cancel_inside_clip_*`.
  - `TileCache` — общий лимит на тайлы (7/8) и превью (1/8): «кеш 64 МБ не превышается» считает их вместе.
  - `bench_compare.py`: ухудшение меньше 5 мс / 5 МБ — шум (`noise`), не падение: повторный прогон дал +25,7 % у
    `enc-r6/tiles_200_ms` (10,9 → 13,8 мс — планировщик); от 20 мс действует относительный порог +25 % эталона.
  - Тесты `DocumentSession` — gtest в `unit_tests` с `QCoreApplication` из `support/qt_app.h` (план называл QTest; цель
    `unit_tests` — gtest, `Qt6::Test` к ней не подключён, `QSignalSpy` не понадобился).
  - `bench` связан с `ottisk_services` (тайлы — через настоящий `RenderPool`), `OTTISK_SOURCE_DIR`/`OTTISK_BINARY_DIR`
    для поиска файлов; `bench_main.cpp` переименован в `bench.cpp` (имя плана).
- Подводные камни:
  - В unity-сборке одноимённые функции анонимных пространств двух `.cpp` конфликтуют (`toFz` в `renderer.cpp` и
    `tiles.cpp`).
  - `fz_cookie` — анонимная структура (`typedef struct {…} fz_cookie`): заранее не объявить — отсюда
    `task_context.h` и `struct TaskCancel`.
  - `signals` — макрос Qt: так нельзя называть переменную в тесте.
  - `QFuture::waitForFinished()` в Qt 6 бросает исключение задачи — `postSync` и `close` на это опираются.
  - Heredoc Git Bash превращает `\n` внутри строк Python в перевод строки — правки C++-литералов через Edit или файл.
  - `make_big.py` уже сгенерировал `big-1200`/`images-200` в корпусе: фикстура `bench.engine.generate` их не трогает.
- Замеры: сборка `unit_tests` инкрементально 6–14 с; Release `bench` 18 с, `render_tests`+`bench` 16 с; ASan
  `engine_tests unit_tests` 17 с. Отмена: рендер 600 dpi в потоке документа — 10–24 мс, в пуле (140 тайлов) — 3–4 мс.
  Стресс: Debug 6–8 с (200 страниц, ~5 900 тайлов, отменено ~60 %), ASan 21,7 с, ошибок ASan нет. Кеш 64 МБ на 200
  страницах с картинками: вставлено 215 МБ, пик 59 МБ. **bench (Release, медиана из 5):** открытие → первый растр
  hello 3,2 мс, enc-r6 22,8 мс (AES-256 R6), big-1200 7,1 мс, images-200 9,5 мс, PDF32000 19,4 мс; раскладка PDF32000
  28 мс; смена страницы из кеша / без — big-1200 0,8 / 1,3 мс, PDF32000 0,8 / 3,2 мс; первый тайл 200 % ≤ 5 мс; тайлы
  50 стр. 100 % / 200 % — big-1200 342 / 537 мс, PDF32000 435 / 657 мс, images-200 86 / 437 мс; пиковая память big-1200
  54 МБ, images-200 335 МБ — все цели 5.8 выполнены.
- Проверки: `verify.py` зелёный (105 тестов); `ctest -L render` 130/130 в Debug и Release; `ctest -L bench` (Release) 6/6;
  `engine.stress.render` и `unit_tests` (сессия, пул, кеши, рендер — трижды) под `msvc-asan` — чисто; шарды
  `engine.corpus` `big_0`, `cyr_0` — зелёные; экранов сессия не строила — сцен нет.

### 2026-10-23 · Между сессиями 1.2 и 1.3 · vcpkg и корпус в `C:\Tools`, корпус без падения Windows
- Сделано (оператор очереди, вне пунктов плана):
  1. **Перенос из `%LOCALAPPDATA%`.** Причина «пропажи» клона vcpkg и корпуса в сессиях 1.1–1.2: десктопное
     приложение Claude — MSIX-пакет, записи процессов оператора в `%LOCALAPPDATA%` ложатся в папку пакета
     (`%LOCALAPPDATA%\Packages\Claude_pzs8sxrjxfjjc\LocalCache\Local`), а очередь из вкладки «Терминал» видит
     настоящую AppData. Клон vcpkg (с `archives` и `registries`) и корпус перенесены в `C:\Tools\vcpkg` и
     `C:\Tools\ottisk\corpus`; значения по умолчанию — туда же: `corpusRoot()` тестов, `render_refs`, `bench`,
     `vcenv.cmd` (+ `VCPKG_DEFAULT_BINARY_CACHE` и `X_VCPKG_REGISTRIES_CACHE` рядом с клоном), `CMakeLists.txt`,
     `verify.py`, `fetch_corpus.py`, генераторы, манифест, шаблон `settings.local.json` в `run-queue.ps1` (`-Setup`
     ругается на пути в `%LOCALAPPDATA%`), документы. `fetch_corpus.py --verify` переписал `index.json` (все теги
     сошлись). `build/{debug,release,asan,fuzz,analyze}` — `--fresh` со своим vcpkg без ключей сессии 1.1 (asan и
     fuzz — свои `vcpkg_installed` из двоичного кеша за 1 с).
  2. **Корпус уронил Windows.** Первый прогон `ctest -L engine -j 10` (21 621 файл `fuzz`) исчерпал выделяемую
     память — у тестов `0xc000012d`, `0xc0000142`, «file not found» на существующих файлах; Windows упала (bugcheck
     0x51) и перезагрузилась, у соседней очереди bny-studio обнулилась ветка Git. Теперь у `engine_tests` предел
     памяти процесса (`tests/engine/support/memory_cap.cpp`, Job Object, `OTTISK_TEST_MEMORY_LIMIT_MB`: 2048, под
     ASan 4096, 0 — выкл.) и `PROCESSORS 2` — `verify.py --full` (`-j 6`) держит не больше трёх шардов.
  3. **broken-ok по ответам MuPDF.** Ожидание этапа 0 «FORMAT/SYNTAX/LIMIT/UNSUPPORTED» уже, чем отвечает сам MuPDF:
     LIBRARY (декодеры JPEG/JPX/JBIG2), GENERIC («CTM CHANGE IN CLIP TEXT!!!»), ARGUMENT («not a dict» при ремонте) —
     все воспроизводит `mutool draw`. `isBrokenFileCode` += LIBRARY, GENERIC; в корпусе для broken-ok допустимы ещё
     ARGUMENT, нехватка памяти под пределом и рендер дольше срока (счётчики в строке шарда); ввод-вывод, исключения
     нашего кода и падение процесса — провал.
  4. **Медленные файлы.** Срок рендера в зонде (`OTTISK_PROBE_RENDER_TIMEOUT_S`: 15 с, под ASan 45 с; сторож ставит
     `fz_cookie.abort`), `OTTISK_CORPUS_TRACE=1` — печать файла перед зондом, `tests/corpus/slow.json` — пропуск
     файлов, которые MuPDF рисует минутами внутри одной операции (cookie не прерывает): `mupdf-fuzz/…/02af972f…`
     (888 байт) — `mutool draw` больше 90 с в FreeType на гигантских глифах.
- Решения:
  - Предел памяти — только у тестов; для программы — вопрос человеку («Нужна ручная проверка»).
  - Список медленных — по Debug-MuPDF (тесты корпуса идут в Debug и ASan): Release-`mutool` под пределом 2 ГБ
    сдаётся быстро, Debug на тех же файлах считает минуты.
- Подводные камни:
  - Каталоги, которые оператор ставит из приложения Claude, — только вне AppData (`C:\Tools`).
  - Коды `0xc000012d`/`0xc0000142` и «file not found» у существующего файла — исчерпание памяти машины, не дефект.
  - Git Bash превращает `/c` в `C:/` в аргументах родных программ (`cmd /c` из Python под Bash открывает пустой
    `cmd`) — `cmd //c` или PowerShell.
- Проверки: `verify.py` зелёный (105 тестов); `ctest -L render` 130/130 в Debug и Release; `-L engine` в Debug —
  113/138 (25 шардов `fuzz` — таймаут 600 с на медленных в MuPDF файлах, до `slow.json`); проверка правки — 3
  независимых агента + по 2 скептика на находку (16 находок, учтены все, кроме давнего расхождения эталона 10.x
  про фикстуру `corpus.fetch`: реальная фикстура — `fetch_corpus.py --verify --dry-run`).

### 2026-10-23 · Сессия 1.3 · Сохранение, журнал undo/redo, автосохранение, ревизии и XMP
- **Решение по покрытию журнала (эталон 4.3, риск 5) — до этапа 3:** журнал MuPDF покрывает правку потоков и новые
  объекты. `pdf_update_stream` (поток без фильтра и `/FlateDecode`, две замены в одной операции) → `pdf_undo` → сырые
  и декодированные байты и словарь (`/Length`, `/Filter`) исходные; `pdf_add_image`/`pdf_add_object`/`pdf_add_stream` →
  `pdf_undo` → запись xref свободна, объекты после полной записи равны исходнику; redo возвращает
  (`unit.journal_coverage.*`). **`Rewriter` (этап 3) и OCR-слой (этап 8) прежний буфер сами не хранят — откат через
  журнал.** Оговорки: (1) трейлер в журнал не входит (`parent_num` 0): `/Info`, созданный правкой (`pdf_set_metadata`),
  после undo остаётся ссылкой на свободный объект (читается как null) — `Saver` убирает висячие ссылки трейлера перед
  записью; (2) полная запись (`pdf_write_document`/`pdf_save_document`) меняет xref документа в памяти (сплошная
  таблица, инкрементальные секции «сохранены», сборка мусора перенумеровывает) — прежний журнал после неё не отменяет
  объекты, перенесённые вперёд из старой секции: `savePdfToMemory` сбрасывает историю, `Saver` переоткрывает файл.
- Сделано:
  1. **Журнал и undo/redo:** `core/doc/journal.{h,cpp}` — `Journal::Operation` (RAII: `pdf_begin_operation`, `commit` →
     `pdf_end_operation`, без `commit` → `pdf_abandon_operation`; вложенная присоединяется к внешней, её неудача
     откатывает внешнюю при `commit`; незакрытую операцию самого MuPDF откатывает), `UndoStack` (шаги с именами поверх
     шагов MuPDF, группировка одноимённых за 1 с, страницы шага `TouchedPages`, «тяжёлые» шаги, перестройка по MuPDF
     при чужой операции или сбросе журнала, слушатель). Журнал включает `Document` при открытии PDF и в `createBlank`.
     `DocumentSession::edit` — в операции; `undo()`/`redo()` очередью правок, `undoState()` + `undoStateChanged`,
     `TaskContext::touchAllPages` (пересчёт раскладки) и `markHeavyEdit`. Тесты `unit.journal.*`,
     `unit.journal_coverage.*`, `unit.document_session.*` (правка, откат, структурная правка, undo/redo).
  2. **Сохранение:** `core/doc/saver.{h,cpp}` — `Saver::decide/save` по правилам 4.4, временный файл `.~<имя>.<hex>.tmp`
     в папке цели (`_wfopen` «w+b» → `fz_output`, `_commit`), `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)`,
     переоткрытие в том же `Document` (`release`/`reopen`, пароль помнится), висячий `/Info` трейлера убирается;
     `core/optimize/qpdf_tool.{h,cpp}` (поиск, `check`, `linearize`, `checkLinearization`, `json`; процесс без окна, вывод
     каналом с опросом, срок); `DocumentSession::save/saveAs` + сигнал `saved`. ctest `compat.save.generate` →
     `compat.save`, `compat.save.signed` (pyHanko INTACT), `compat.save.encrypted` — 10 файлов: инкремент, полная с
     оптимизацией, ремонт, новый, линеаризованный, подписанный ×2, AES-256, свойства ×2; `compat_check.py` принимает
     каталоги.
  3. **Автосохранение:** `core/util/hash.{h,cpp}` (SHA-256), `core/doc/autosave.{h,cpp}` (журнал с позиции 0, копия
     исходника ≤ 500 МБ, `index.json` {path, key, sha256, mtime, size, copy, position, steps, savedAt}, `recover`:
     исходник по sha256, иначе копия, иначе отказ), `Document::recoverFrom` (свой загрузчик журнала), `services/
     autosave_runner.{h,cpp}` (таймер 1–30 мин, `watch`/`tick`/`saveNow`, «Сохранить» и «Не сохранять» удаляют запись,
     `recoverable`/`recover` → `DocumentSession::recoverAsync`). Тест `unit.autosave.taskkill_*`: дочерний процесс
     (`unit_tests` с отключённым тестом) правит, автосохраняет и ждёт `taskkill /F`.
  4. **Ревизии:** `core/doc/revisions.{h,cpp}` — `versionCount`, `lastValidVersion`/`changeHistoryValid`, `signatures`
     (поле, конец `/ByteRange`, версия, `pdf_validate_signature`), `signedRevision`/`signedRevisionBytes` (байты из потока
     документа во временный файл); тест на B-B pyHanko, после инкремента и на DSS `modified_after_signature.pdf`.
  5. **Свойства и метаданные:** `core/doc/xmp.{h,cpp}` (свой разбор и сериализатор XMP: дерево узлов, свойства по URI
     пространства, простые/Alt/Seq/Bag, правка Alt x-default на месте, даты PDF ↔ ISO), `core/doc/props.{h,cpp}`
     (`readInfo`/`writeInfo` с пользовательскими ключами и синхронизацией XMP, `xmpPacket`, `lang`/`setLang`, `tagged`,
     `fonts` + `FontCache`, `advanced` с предупреждениями `pdf_check_document`), `Document::setProperties`.
- Решения:
  - **MuPDF 1.28.5 не загружает журнал с правками:** `pdf_deserialise_journal` добавляет фрагменты в `pending_tail`,
    которого при загрузке нет (Debug — assert `entry != NULL`, Release — «Can't add a journal fragment absent an
    operation»). Патч MuPDF не нужен: свой загрузчик в `Document::recoverFrom` разбирает формат `%!MuPDF-Journal-100`
    публичными `pdf_lex`/`pdf_parse_dict`/`pdf_parse_journal_obj`, сверяет отпечаток xref как MuPDF и переигрывает
    шаги, как `pdf_redo` (`pdf_get_incremental_xref_entry` + `pdf_add_journal_fragment` в своей операции).
  - **Автосохранение — журнал с позиции 0, а не снимок:** фрагменты сделанного шага MuPDF хранят прежние значения, новые
    живут в несохранённой секции xref; `pdf_write_snapshot` (как и любая запись) обнуляет `num_incremental_sections` —
    undo и «есть несохранённое» ломаются. Запись: отменить все шаги → `pdf_write_journal` → вернуть все шаги; документ в
    памяти не меняется. Новый документ и документ из памяти (нет пути) не автосохраняются — до 7.2 («Без имени»).
  - **Любая запись сбрасывает историю:** `pdf_write_document` меняет xref документа (сплошная таблица, секции
    «сохранены», перенумерация) — после неё журнал не отменяет перенесённые вперёд объекты. `Saver` всегда переоткрывает
    файл (история пуста, как и в эталоне 4.4), `savePdfToMemory` — `UndoStack::clear`.
  - Подписанный — это подписанное поле (`/V`), а не `pdf_count_signatures` (тот считает и пустые поля подписи: им
    полная запись не вредит).
  - Вложенная `Journal::Operation` begin в MuPDF не делает (план: «begin внутрь begin не вкладывать»); откатить её
    отдельно нельзя — внешняя при `commit` откатывается и бросает `MuError`.
  - Занятый файл цели («Сохранить» в свой путь): документ открывается из временного файла с прежним путём
    (`backingFile`), правки не теряются, следующее сохранение заменяет файл и удаляет временный.
  - QFuture задачи завершается после `revision++`, пересчёта раскладки и истории (результат держится до `finish`):
    иначе `undo().result()` возвращался раньше, чем сводка видела новую раскладку.
  - XMP — свой разборщик (библиотек XML в стеке нет): правятся только свои свойства; `dc:subject` (Bag), `dc:language`,
    `pdfaid`, чужие схемы и обёртка xpacket — как были; не разобрать — /Info пишется, XMP не трогается (с предупреждением).
    Автор → `dc:creator` одним элементом Seq; пользовательские ключи → `pdfx:<ключ>`.
  - Ревизии: `changeHistoryValid` (`pdf_validate_change_history`) смотрит только права DocMDP и `/Lock` — у утверждающей
    подписи без них допустимо всё (и у DSS `modified_after_signature.pdf`); строгий ответ на подпись —
    `SignatureInfo::changedSince` (`pdf_validate_signature`): записка на странице с виджетом подписи — уже изменение
    (как у pyHanko: `ILLEGAL_MODIFICATIONS`, подпись `INTACT`; правка метаданных — `ACCEPTABLE_MODIFICATIONS`). «Разрешено
    ли как в Acrobat» решает валидатор 4.4.
  - План называет `qpdf --json --json-key=trailer` — в qpdf 12 (JSON v2) такого ключа нет: трейлер — запись `trailer`
    среди объектов `--json-key=qpdf`.
- Подводные камни:
  - Правка через `Get-Content`/`Set-Content` PowerShell 5.1 портит UTF-8 без BOM (читается как ANSI, двойное
    кодирование кириллицы) — только Edit/Write или Python.
  - Узкий `getenv` отдаёт пути с кириллицей в ANSI — в дочернем процессе только `_wgetenv`.
  - `OpenResult` с `needsPassword` держит документ (и файл) открытым — замена файла в тесте падала с «Access is denied».
  - При инкременте MuPDF читает вывод обратно (`fz_stream_from_output`) — файл записи открывать «w+b», иначе «read
    error: Bad file descriptor» (для подписи этапа 4 — обязательно).
  - Трейлер не в журнале: `/Info`, созданный правкой, после undo висит ссылкой на свободный объект (MuPDF пишет «cannot
    find object in xref» при чтении).
  - `pdf_update_object` в журнал не попадает; `PDF_NAME(MarkInfo)` нет в таблице имён MuPDF — `pdf_dict_puts`.
  - У записки (`pdf_create_annot` Text) есть Popup — в `/Annots` две записи.
- Замеры: инкремент `unit_tests` 6–23 с; `verify.py` 33 с (162 теста); `compat.save*` 4 теста — 4,6 с; автосохранение
  трёх правок и восстановление — миллисекунды, тест с `taskkill` — 0,2 с; ASan `unit_tests` — 23 с сборки.
- Проверки: `verify.py` зелёный (162 теста); `ctest -L compat` 4/4; `engine.stress.render` зелёный; новые тесты
  (журнал, сохранение, автосохранение, ревизии, свойства, сессия — 70) под `msvc-asan` — чисто; экранов сессия не
  строила — сцен нет.

### 2026-10-23 · Между сессиями 1.3 и 1.4 · Зависимости от заголовков, отчёт о падении, корпус под ASan
- Сделано (оператор очереди, вне пунктов плана):
  1. **Зависимости от заголовков в Ninja.** У Build Tools здесь только русские ресурсы (`…\bin\Hostx64\x64\1049`;
     `VSLANG=1033` не помогает), системные кодировки 1252/850: строка `/showIncludes` «Примечание: включение файла:»
     приходит как «??????????» без консоли и как UTF-8 в консоли 65001, а CMake записывал в `msvc_deps_prefix` то, что
     увидел при configure. Ninja не узнавал строки и не записывал зависимости (у 34 из 58 объектов `build/debug` —
     `#deps 0`, строки «Примечание…» в логе сборки): правка `.h` не пересобирала зависящие `.cpp`. Так
     `mutants_test.obj` остался со старым `ProbeResult` и упал на `/RTC` (порча стека). Теперь `tools/cl_launcher.py`
     (`CMAKE_C/CXX_COMPILER_LAUNCHER`) приводит префикс к «Note: including file:», `CMakeLists.txt` задаёт его же; все
     `.cpp` пересобраны, зависимости записаны у всех объектов (`build/debug` — 67/67). Вероятно, так было с этапа 0:
     «необъяснимые» падения сессий 1.1–1.3 после правки заголовка — отсюда.
  2. **Отчёт о падении** `tests/engine/support/crash_report.cpp`: необработанное исключение → код и стек с функциями и
     строками в stderr; стек упавшего потока разбирает отдельный поток (`StackWalk64`), у главного — запас стека, так
     что работает и при переполнении стека (проверено на тестовой программе).
  3. **Корпус под ASan.** `engine_tests` под ASan: `allocator_may_return_null=1` (`__asan_default_options`) — битые
     JBIG2 просят 10 ГБ одним выделением, обычный `malloc` возвращает NULL, а ASan по умолчанию завершал процесс;
     предел памяти 6144 МБ (ASan сам держит ~2,7 ГБ) и `PROCESSORS 4` — одновременно вдвое меньше ASan-процессов.
     Раздел `asan` в `tests/corpus/slow.json` — файлы, на которых кончается внутренняя память ASan (миллионы мелких
     объектов: «allocator exhausted the free list space», «Failed to commit shadow memory»), пропускаются только под
     ASan; виновник — последний файл трассировки (`OTTISK_CORPUS_TRACE=1`) упавшего шарда.
  4. **Проверка правок оператора** — 3 агента по областям + по 2 скептика на находку: учтены `isMemoryFailure`
     (MuPDF пишет и «calloc/realloc … failed»), отчёт о падении на переполнении стека, `VCPKG_DEFAULT_BINARY_CACHE`
     убран из шаблона `settings.local.json` (`vcenv.cmd` ставит его, только если папка есть — иначе vcpkg падает).
- Решения: тяжёлые для ASan файлы проверяет Debug-прогон; предел ASan выше 6 ГБ не поднимаем (два процесса на машине
  16 ГБ).
- Подводные камни:
  - range-for по `doc.value("asan", …).items()` — временный json умирает до цикла (до C++23): ASan — heap-use-after-free.
  - `ninja -n` при `GLOB … CONFIGURE_DEPENDS` останавливается на «Re-running CMake» — пробный прогон ничего не покажет.
  - Какой шард упрётся во внутреннюю память ASan, зависит от раскладки адресов: с пределом 4 ГБ от прогона к прогону
    падали разные шарды.
  - Debug-`mutool` падает переполнением стека на `mupdf-fuzz/…/0f009fa6…` (наш зонд его проходит).
- Проверки: `verify.py` зелёный (162 теста); `ctest -L render` (Release) 130/130; `engine` Debug 136/136 (4 мин, было 94
  мин и 25 таймаутов); `engine` под ASan — 126/136 до раздела `asan`; память — сторож, минимум свободной выделяемой
  11 ГБ.

### 2026-10-23 · Сессия 1.4 · Текст: извлечение, поиск, выделение и индекс FTS5
- Сделано:
  1. **Текстовый слой:** `core/text/text_types.h` (значения без MuPDF) и `core/text/stext.{h,cpp}` — `TextPage`
     (`fz_new_stext_device_for_page` по содержимому страницы с cookie; блоки, строки, символы с квадом, точкой пера,
     кеглем, цветом `argb`, флагами и шрифтом — в порядке чтения, struct-блоки обходятся в глубину; слова, `text()`,
     `charIndexAt`/`lineAt`/`wordAt`/`wordBounds`, векторы `collectVectors`, дерево `collectStructure`, шрифты с
     жирным/курсивом по имени и флагам, страница блока из `fz_stext_page_details`), `TextCache` (LRU 16 по {страница,
     флаги}) в `DocumentSession` — `textPage()`, `TaskContext::textPage`, сброс правкой страницы, undo/redo, открытием.
     Фикстуры `tools/gen/make_text.py` → `tests/fixtures/text/` (поиск, векторы, невстроенный TrueType, `text.json`),
     эталон слов `tools/gen/stext_expected.py` (pypdfium2) — слова и точки пера четырёх файлов совпадают с PDFium.
  2. **Поиск:** `core/text/search.{h,cpp}` — `fz_new_search` (регистр, диакритика, regexp), свой фильтр «целое
     слово», `searchPage` → квады, `SearchSession` (страницы по `FZ_SEARCH_MORE_INPUT`, совпадение через границу
     страниц, прогресс раз в 100 мс, отмена, пропуск страниц без слоя, «N из M», `next`/`previous`/`selectFrom`);
     `bench search` (+ ctest `bench.engine.search` в Release, порог 3 с — код 1 при превышении).
  3. **Выделение и копирование:** `core/text/selection.{h,cpp}` — свой перенос алгоритмов MuPDF (ближайшая граница
     символа, привязка к словам и строкам, прямоугольник), квады высотой в строку, `toPlainText`, `toHtml` (`<b>`/`<i>`);
     `services/clipboard_io.{h,cpp}` — text/plain + text/html, снимок области при dpi (PNG + изображение), `put`.
  4. **Индекс и поиск по папкам:** `core/text/index.{h,cpp}` (`TextIndex` на FTS5), `services/state_db.{h,cpp}`
     (`StateDb`: одно подключение, WAL, миграции по `user_version`, испорченный файл → `.bad`),
     `services/folder_search.{h,cpp}` (обход, извлечение в своём пуле, прогресс, отмена, результаты по файлам,
     зашифрованные и битые — отдельно, удалённые уходят из индекса); `engine.folder_search.*` — 30 файлов.
  5. **Экспорт и 0.1.0:** `core/create/text_export.{h,cpp}` (TXT UTF-8/UTF-16/CP1251, HTML/XHTML с картинками base64,
     XML), `core/create/rtf_writer.{h,cpp}`, `DocumentSession::exportText`; ctest `compat.rtf.generate` → `compat.rtf`
     (Word COM открыл оба RTF, «Привет» найден); `cmake/product.cmake` 0.1.0, тег `v0.1.0`.
- Решения:
  - **SEGMENT стоит почти всё время извлечения:** весь PDF32000 (756 стр.) в Release `mutool draw -F txt` — 23,7 с с
    SEGMENT и 3,3 с без (2,5 с без всех флагов). Поиск и индекс строят слои без сегментации и абзацев
    (`TextOptions::readingOrder = false`, `SearchSession::textOptions()`) — совпадения те же (1241 «annotation» в обоих
    случаях), Debug-поиск по PDF32000: 91 → 10,7 с. Эталон 3.2 требует SEGMENT для извлечения — он остаётся по
    умолчанию (выделение, экспорт, порядок чтения); `readingOrder` — добавленное поле опций (план называет четыре).
  - **Выделение MuPDF 1.28.5 не заходит в struct-блоки:** `fz_highlight_selection`, `fz_copy_selection`,
    `fz_snap_selection` и `fz_copy_rectangle` перебирают только верхний уровень блоков, а SEGMENT (и разбивка списков
    PARAGRAPH_BREAK) кладут весь текст в struct-узлы «Split»/LI — на таком слое выделять нечего. Выделение — свой перенос
    тех же алгоритмов на значения `TextPage`; канарейка `unit.selection.mupdf_selection_ignores_struct_blocks`
    покраснеет, когда MuPDF это починит. Вывод stext (`fz_print_stext_page_as_*`) и `fz_new_search` struct-блоки обходят.
  - Поиск — новый `fz_new_search` и для страницы, и для документа (`fz_search_stext_page_cb` — та же функция с
    `ignore-case`, без опций); квады — из совпадения. «Целое слово» — по `TextPage::isWordChar` (буквы, цифры, метки, `_`).
  - Квады выделения — высотой в строку: у ACCURATE_BBOXES рамка символа — чернила глифа (у «р» ниже, у пробела высота
    нулевая), по ним подсветка рваная, а прямоугольник не задевал пробелы.
  - Карта `fz_stext_char* → индекс` строится лениво (`std::call_once`) — поиск спрашивает только у страниц с совпадениями.
  - Индекс: отдельная `fts_files` (путь UNIQUE, mtime, пометка «зашифрован») + `fts_pages` (FTS5), rowid = id · 2^20 +
    страница — удаление файла диапазоном, без полного прохода; запрос — фразой, папка — буквальный префикс
    (`substr`, не LIKE); контекст — `snippet` с метками U+0002/U+0003. Таблица `recent` эталона 4.7 — следующим шагом
    миграции в сессии недавних файлов, не сейчас.
  - Снимок рисуется своей матрицей ровно dpi/72: `pageMatrix` тайлов округляет зум до тысячных (216 pt при 300 dpi
    давали 901 px вместо 900).
  - TXT: страницы через «\f» (как у pdftotext и MuPDF), CRLF; CP1251 — `WideCharToMultiByte` с
    `WC_NO_BEST_FIT_CHARS` («ﬁ» и ударение — «?», не «похожие» буквы). HTML MuPDF пишет кириллицу ссылками `&#x…;`, но
    заголовок без кодировки — добавлен `<meta charset="utf-8">`.
  - RTF — чистый ASCII: всё вне ASCII — `\uN?` (знаковое 16-битное, вне BMP — суррогатная пара), `\fcharset204`,
    семейство шрифта из имени PDF («TimesNewRomanPS-BoldMT» → «Times New Roman»).
- Подводные камни:
  - Unity-сборка `ottisk_core`: одноимённые функции в безымянных пространствах разных `.cpp` (`toQuad`) — ошибка C2084;
    имена помощников делать уникальными.
  - Пустой `std::string_view` с `data() == nullptr` → `sqlite3_bind_text` привязывает NULL, а не '' (`?2 = ''` ложно).
  - `fz_stext_page_details_for_block` бросает, если id вне списка: длина проверяется заранее.
  - Без COLLECT_STRUCTURE у тегированного файла Word всё равно есть узлы: «Split» от SEGMENT и LI от поиска списков.
  - `mutool grep` в нашей сборке 1.28.5 падает сразу (segfault) — замер поиска только своим `bench`.
  - Оператор параллельно правил и закоммитил `tests/corpus/slow.json` (раздел `asan`, 1935e03) — в коммит пункта 1 его
    правка не попала (`git update-index --assume-unchanged` на время `done.mjs`).
  - `unit.document_session.cookie_cancel_stops_long_render_within_200ms` один раз упал в `verify.py` под нагрузкой
    (параллельно шёл ASan-прогон корпуса оператора); отдельно — 3 из 3. Нестабилен по времени, не правился.
- Замеры: Debug `bench search` (PDF32000, 756 стр., «annotation» — 1241 совпадение): 91 с со слоями SEGMENT, 10,7 с без;
  Release — проверит Р1 (по `mutool draw` извлечение всего документа без SEGMENT ~3 с: порог 3 с на грани, запас —
  параллельное извлечение вторым открытием документа); поиск по папке из 30 файлов — 0,2 с; инкремент `unit_tests`
  10–50 с; `verify.py` 49 с (214 тестов).
- Проверки: `verify.py` зелёный (214 тестов); `engine.folder_search.*` 3/3; `compat.rtf` (Word COM) зелёный; экранов
  сессия не строила — сцен нет.

### 2026-10-23 · Сессия 2.1 · Тема, токены, иконки, компоненты, витрина и снимки
- Сделано:
  1. **Тема:** `ui/theme/tokens.h` — светлая и тёмная палитры по таблице эталона 6.4, третья «Высокая
     контрастность» из тех же имён, размеры `Metrics` и `Tokens::scaled(k)` (все размеры × k, время движения — нет);
     `palette.cpp` (QPalette: Window/Base — surface, Highlight — accent, Mid — border, Dark — canvas…), `qss.cpp`,
     `fonts.cpp` (Segoe UI 9 pt, заголовки 13/20 px, `tnum`), `Theme::apply(app, choice, scale)` без перезапуска с
     `themeChanged`, «Системная» — `colorScheme()` и `contrastPreference()` Windows; `--theme`, `--scale`.
     `ui_tests` — наборы по файлам: `tests/ui/<имя>_test.cpp` + `OT_UI_TEST` → ctest `ui.<имя>` (свой `main` в
     `tests/ui/support/ui_main.cpp`), `ui.theme`.
  2. **Иконки:** `ui/theme/icon_names.txt` (16 инструментов и 8 панелей по эталону 6.5, плавающая панель,
     компоненты) → `tools/copy_icons.py` → 60 SVG в `assets/icons` + LICENSE, ресурс `:/icons` в `ottisk_ui_lib`;
     `Icons::get` (QSvgRenderer, `stroke`/`currentColor` → цвет токена, `stroke-width` 1,75, кэш по имени, цвету,
     размеру и dpr, `qFatal` на неизвестное имя в Debug), `Icons::icon` — QIcon с движком, который при каждой
     отрисовке берёт цвета текущей темы (нажатая — accent, выключенная — приглушённая); `kToolIcons`/`kPaneIcons`;
     `ui.icons`, `unit.guard.icons` (`copy_icons.py --check`).
  3. **Компоненты:** `ui/widgets/` — `ToolButton`, `SegmentedBar`, `SearchField`, `PanelHeader`, `Card`, `ListRow`,
     `Chip`, `Toast`, `EmptyState`, `MessageBar`, `ColorSwatch`, `NumberField`, `PropertySheet`, `ZoomBox`,
     `FloatingBar` (основа cloud-38: состав и порядок списком id, один выбранный инструмент, тень из токенов, в
     «Высокой контрастности» — рамка без тени), общий `tone.h`; витрина `showcase.cpp` и `AcroBatya.exe --showcase`
     (переключатель темы и «Крупных элементов»); `ui.widgets` — клики, сигналы, состояния, контраст, перекраска.
  4. **Снимки:** таблица `SCENES` (`sceneTable()`): `home`, `showcase`, `showcase-dark`, `showcase-contrast`,
     `showcase-large`; сцена может закрепить тему или масштаб; `--shots-list`, `--lang`; имя файла
     `<сцена>[-dark|-contrast][-large][-en].png`; `fixtures.cpp` (`sourceRoot`, `sceneFixture`); `shots.ps1` —
     `-Dark`, `-Large`, `-Lang`, `-All` (5 вариантов); `review_refs.py --main` и `tools/main_scenes.txt`; `ui.shots`.
  5. **i18n:** `i18n/ottisk_ru.ts` (пустой), `ottisk_en.ts` (только заголовок окна — для `ui.i18n`),
     `qt_add_translations(TARGETS AcroBatya ui_tests SOURCE_TARGETS ottisk_ui_lib AcroBatya … MERGE_QT_TRANSLATIONS
     LUPDATE_OPTIONS -no-obsolete)` — `.qm` ресурсом `:/i18n` с qtbase; `services/i18n` (`--lang` → `language/ui` в
     `settings.ini` → язык системы; `QLocale::setDefault`); `MainWindow` меняет заголовок по `LanguageChange`;
     `i18n_guard.py` — кириллица в `src/services` только в комментариях, контекст `translate`/`QT_TRANSLATE_NOOP`
     — ASCII, `--self-test` (ctest `unit.guard.i18n_selftest`); правило в `CLAUDE.md` (п. 10).
- Решения:
  - **Токены сверх таблицы 6.4** (эталон их требует, но не задаёт): `onAccent` — текст на акценте (в тёмной теме
    #121212: белый на #D9634E — 3,6:1, ниже 4,5), `form` — фиолетовая полоса «Документ содержит поля» (6.1),
    `shadow` — тень плавающей панели (6.1: rgba(0,0,0,.18); в тёмной .40, в контрастной — нет тени), цвета пометок
    `kAnnotationColors` (данные документа, не тема). Размеры сверх 6.4: `iconEmpty` 40, `focusRing` 2, тень 16/4.
  - **Подпись `textMuted` на `hover` светлой темы — 4,34:1**, поэтому у карточки при наведении меняется рамка, а не
    фон, а у строки списка подпись в наведении и выборе — цветом `text` (`rowColors()`).
  - QSS компонентов — одна таблица правил в `qss.cpp`: из неё собирается стиль и пары «текст/фон»
    `componentColors()`, которые тест проверяет на 4,5:1 во всех трёх палитрах — проверяется то, что рисуется.
    Поля ввода (поиск, число, масштаб), их рамка и фокус — QSS; у `NumberField` нет кнопок-стрелок (шаг — ↑/↓,
    колесо в фокусе).
  - Состояния в витрине без мыши: наведение — `WA_UnderMouse`, фокус — свойство `otFocus` (те же правила QSS, что
    у `:focus`), выключено — `setEnabled(false)`.
  - `ThemeChoice::System` разрешается в «Высокую контрастность», если Windows сообщает контрастную тему
    (`QStyleHints::accessibility()->contrastPreference()`, Qt 6.10+).
  - Ключ языка `language/ui` пока объявлен в `services/i18n.h` — 2.4b переносит его в `settings_keys.h`. Английский
    `.ts` не пустой: в нём заголовок окна, иначе тест «смена языка меняет заголовок» нечем проверить.
  - `qt_add_translations`: эталон пишет `TARGETS AcroBatya`; добавлены `ui_tests` (тест языка) и
    `SOURCE_TARGETS ottisk_ui_lib AcroBatya` (иначе вызов откладывается и lupdate читает все цели, включая core).
- Подводные камни:
  - С QSS приложения `QApplication::style()` — обёртка `QStyleSheetStyle` с пустым `name()`: Fusion держится своим
    указателем (`Theme::baseStyleName()`).
  - `QAbstractSpinBox` с `NoButtons` под QSS сжимает поле ввода до 1 px — кнопки скрыты нулевой шириной в QSS;
    отступ QSS до поля ввода не доходит — `lineEdit()->setTextMargins`.
  - `ToolButton` на `themeChanged` заново ставит минимум 32 px — вложенный крестик чипа вылезал за рамку:
    `setCompact(side)` для кнопок внутри компонентов.
  - Встроенный в раскладку `Toast` при смене темы «уезжал» к низу хоста — `place()` только у всплывшего.
  - `QTest::keyClicks` печатает только Latin-1 (кириллица — `qFatal` в `qasciikey.cpp`): в тестах — `setText`.
  - Первый фокусируемый виджет окна получает настоящий фокус при показе — у витрины фокус на самой странице.
  - Heredoc Git Bash с вложенными кавычками ломается — длинные правки Python-скриптом из файла.
- Замеры: инкремент `ui_tests` 15–35 с; `ui.*` 2,5–4 с; `shots.ps1 -All` (13 снимков, 5 вариантов) 4,3 с;
  `verify.py` 25 с (221 тест).
- Проверки: `verify.py` зелёный (221 тест); сцены: home, home-dark, showcase, showcase-dark, showcase-contrast,
  showcase-large (и `-All` — 13 снимков) — просмотрены: размеры токенов, Segoe UI, кириллица, иконки, состояния.
