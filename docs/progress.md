# Журнал разработки «Оттиска»

Сюда Claude Code дописывает итоги этапа 0, каждой сессии и рубежа очереди. Правила записи — в `CLAUDE.md`.
Очередь — `docs/plan.md`, эталон — `docs/design.md`, как работает очередь — `QUEUE.md`.

## Нужна ручная проверка

Новые пункты — сверху, с номером сессии или рубежа. Ничего из этого не блокирует очередь: она работает на
`Fake*`-реализациях и фикстурах, живая часть ждёт здесь (эталон 8.1 «Человек отдельно», 9.7).

- **Этап 0 · Три вопроса объёма — ответить до запуска очереди** (в «Правилах работы» `docs/plan.md`, пункт
  «Ответы на вопросы раздела 12»): (1) эталон 12.1, **security-11** — шифрование сертификатом (PubSec): без
  ответа в 7.6 остаётся честная заглушка «после 1.0»; нужен ли патч чтения PubSec (сессия 7.7)? (2) **security-34** —
  песочницы AppContainer нет; замена — защищённый просмотр 2.5b и устойчивость парсера 1.1 — подтвердить;
  (3) **forms-47** — консоль JavaScript (Pro, nice, ~1 пункт в 6.3) — подтвердить, иначе убрать из `docs/scope.json`
  и из 6.3.
- **Этап 0 · Загрузки и LibreOffice — сделано 23.10.2026** (разрешение — поручение человека «начинай этап 0 по
  docs/plan.md»): tessdata rus/eng/deu, корпуса всех тегов плана (1,8 ГБ в `%LOCALAPPDATA%\ottisk\corpus`),
  LibreOffice 26.8.1 распакован `msiexec /a` без прав в `C:\Tools\libreoffice` (`OTTISK_SOFFICE` записан) —
  `winget`/UAC к 7.3 не нужен. Человеку только посмотреть: при желании поставить LibreOffice штатно (тогда
  обновить `OTTISK_SOFFICE`).
- **К рубежу Р1 · Acrobat Reader (по желанию)** — для живой сверки рендера, буклета и наших файлов глазами.
- **К рубежу Р2 · Сертификат для живой подписи** — `New-SelfSignedCertificate -Subject "CN=Оттиск тест"
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
     (`ottisk.iss` per-user, ProgID `Ottisk.pdf`, Capabilities в HKCU, `app.ico`, `app.rc`, `Ottisk.manifest`,
     `licenses/`), `THIRD-PARTY-NOTICES.md`; `dist/Ottisk-0.0.1-setup.exe` (38 МБ): тихая установка, `--self-check`
     с экраном и под offscreen, `--shots`, `--smoke 3`, удаление — `tests/build/install_test.py` зелёный.
  10. Очередь: `run-queue.ps1` + `tools/queue-lib.ps1` (без Docker/Postgres; `Test-Limited`, 24-часовой и
      относительный разбор «resets …», запас 120 с, основная → запасная Fable 5.1 xhigh → ждать → снова основная;
      страж этапа 0 раньше всего, `-Setup` пункты не отмечает), `tools/test_reset_parse.ps1`, `tools/test_guard.ps1`,
      `claude-setup/` (SKILL.md под C++ с «Ultracode» и циклом сборки, `done.mjs` без изменений, `guard.ps1`,
      `settings.json`), `.claude/settings.local.json`, `CLAUDE.md`, `docs/map.md`, `QUEUE.md`, `README.md`, `CHANGELOG.md`.
- Решения:
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
  - Release-пресет сборки собирает только `Ottisk render_refs render_tests bench` (`targets` в `CMakePresets.json`).
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
  - Программа WIN32: из PowerShell `& Ottisk.exe` не ждёт и не отдаёт вывод — запускать через `cmd /c` с
    перенаправлением или из Python; `--self-check` сам подключается к консоли родителя.
  - Qt offscreen без `QT_QPA_FONTDIR` рисует квадраты — `main()` подставляет `%WINDIR%\Fonts`.
  - clang-format (Google, `ReflowComments`) склеивает строки комментария — абзацы разделять пустым `//`.
  - `soffice.com` печатает «Could not find platform independent libraries» (встроенный Python) — безвредно.
  - `qtdiag` Qt 6.12 печатает только «by MSVC 2022»; версия компилятора 19.44.35227.0 — в
    `C:\Qt\6.12.0\msvc2022_64\config_qtbase.summary` (тест пункта 1 «qtdiag печатает msvc 19.44» проверять там).
  - Каталог корпусов `%LOCALAPPDATA%\ottisk\corpus` (эталон 9.2) в NTFS — тот же каталог, что данные программы
    `%LOCALAPPDATA%\Ottisk` (эталон 4.7): регистр не различается. Деинсталлятор корпус не трогает
    (`[UninstallDelete]` — только `thumbs`, `autosave`, `lo-prof-*`); расхождение эталонов, не дефект.
  - Хук блокирует `curl -o C:\Tools\dl\…`: этап 0 вёлся из сессии вне папки `ottisk`; из очереди `C:\Tools` — только чтение.
- Замеры:

  | Что | Время / размер |
  |---|---|
  | configure Debug первый (vcpkg 14 пакетов: OpenSSL 4,4 мин, libjpeg-turbo 28 с, tiff 25 с, gtest 18 с, остальные < 18 с; четыре из них — jpeg/zlib/lzma/spng от фич tiff — затем сняты) | 467 с |
  | configure Release (порты из бинарного кэша vcpkg) | 15 с |
  | configure повторный | 19–34 с |
  | полная сборка Debug (39 целей, PCH + unity) | 33 с |
  | инкремент одного `.cpp` в ui → `Ottisk.exe` | 4 с |
  | линковка Debug: `Ottisk.exe` / `unit_tests` / `render_refs` | 0,6 / 1,7 / 6,8 с |
  | линковка Release с `/GL` MuPDF: `render_refs` / `Ottisk` | 238 / 74 с |
  | линковка Release без LTCG: `render_refs` / `Ottisk` | 1,6 / 1,1 с |
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
