# «АкроБатя» — настольный PDF-редактор для Windows

Личная программа семьи уровня Adobe Acrobat Standard: просмотр и печать, правка текста и картинок в PDF,
комментарии и Fill & Sign, формы и конструктор форм, подписи PAdES, страницы, создание и конвертация,
оформление, защита, OCR и сканирование, доступность. Интерфейс — как у нового Acrobat, по-русски
(английский — в 9.2). Ставится на пользователя, без прав администратора. Не продаётся: MuPDF под AGPL
допустим, текст лицензии и ссылка на исходники — в «О программе».

## Документы (читать перед работой)

| Файл | Что внутри | Можно менять? |
|---|---|---|
| `docs/map.md` | карта кода: где что лежит и рецепты типовых вещей | дополнять |
| `docs/design.md` | **эталон** (версия 0.1 от 22.10.2026, решения человека (1)–(16)): объём и ID функций (1), сценарии (2), функции по модулям (3), документная модель (4), архитектура и дерево (5), интерфейс (6), безопасность (7), этапы (8), проверки (9), сборка и очередь (10), риски (11), вопросы человеку (12) | **нет** (deny) |
| `docs/plan.md` | очередь: этапы 0–10, сессии, рубежи Р1–Р5; «Правила работы» — решения по умолчанию | только отметки `[ ]` → `[x]` / `[~]` |
| `docs/sessions.md` | почему сессии собраны так, стоимость, что делает человек | **нет** (человек; deny) |
| `docs/tech-facts.md` | справочник проверенных фактов: версии, флаги, грабли (~320 КБ — только поиском) | **нет** (deny) |
| `docs/features.json`, `docs/features.md` | каталог 469 функций Acrobat (поле `id`: `view-10`, `sign-28`…) | **нет** (deny) |
| `docs/scope.json` | объём: `full` 186 + `partial` 8 + `nice` 28 + `out` 30; сверяет `unit.guard.scope` | только решением человека |
| `docs/progress.md` | журнал очереди и «Нужна ручная проверка» | дописывать |
| `docs/analyze-baseline.json` | база `cl /analyze` | только `analyze_summary.py --update-baseline` с причиной в журнале |
| `tests/render/expected/` | эталоны рендера и `expected.json` | только `render_refs --update --reason` / `--mark-release-only` (deny) |
| `CHANGELOG.md` | «Не выпущено» — строка «что видно человеку» от каждой сессии | дописывать |
| `QUEUE.md` | как работает очередь, команды, сбои | человек |

Позже появятся `docs/manual.md` (9.4), `docs/limitations.md`, `docs/glossary.md`, `docs/bench/baseline-*.json`,
`docs/pilot.md` (пишет человек). Если эталон и план расходятся — прав `docs/design.md`; где эталон молчит —
«Правила работы» `docs/plan.md`; спорные решения записывай в `docs/progress.md`.

## Стек (не менять)

C++20 без модулей; MSVC 14.44 (`cl` 19.44, Build Tools 2022 17.14, SDK 10.0.26100); CMake 3.31 + Ninja 1.12
по пресетам `CMakePresets.json`; **Qt 6.12.0 LTS** (Widgets, PrintSupport, Svg, Concurrent, Network, Test,
LinguistTools) в `C:\Qt\6.12.0\msvc2022_64`; **MuPDF 1.28.5** (с Tesseract 5.5.2 и Leptonica из её состава) —
статические `.lib` из `C:\src\mupdf\platform\win32\x64\{Debug,Release}` (`libresources.lib` — всегда Release);
vcpkg (`C:\Tools\vcpkg`, манифест `vcpkg.json`, триплет `x64-windows-static-md-release`): OpenSSL 3.5.4,
libtiff, fmt, spdlog, nlohmann-json, sqlite3[fts5], gtest; qpdf 12.4.2 — portable `qpdf.exe`, не библиотека.
Все цели `/MD`; Debug = Debug-библиотеки MuPDF + release-DLL Qt. Тесты — gtest и QTest (offscreen);
скрипты над PDF — Python 3.14 в venv `C:\Tools\venv-test` (pypdfium2, pikepdf, pyHanko, scikit-image, …);
формат и анализ — clang-format/clang-tidy 19.1.5, `cl /analyze`; установщик — Inno Setup 6.7.3 (per-user).

**Новые зависимости не добавляются** — ни порты vcpkg, ни библиотеки, ни пакеты Python, ни программы:
только `QUEUE: BLOCKED` с доводом. Патчи MuPDF не планируются (если понадобятся — `tools/mupdf-patches/`
и решение человека).

## Слои и структура (эталон 5.1–5.2)

`core` (чистый C++20, без Qt) → `services` (Qt Core/Gui/Network/PrintSupport, без Widgets) → `ui`
(Qt Widgets, `AcroBatya.exe`). Обратных зависимостей нет: `core` не включает `<Q…>`, `services` — `QtWidgets`,
`ui` — `mupdf/` (страж `unit.guard.boundaries`). `ui` меняет документ только запросом к `DocumentSession`.

```text
ottisk/                    корень Git (локальный, без push) и очереди
├─ CMakeLists.txt  CMakePresets.json  vcpkg.json  .clang-format  .clang-tidy
├─ AGENTS.md  QUEUE.md  run-queue.ps1  CHANGELOG.md  README.md  THIRD-PARTY-NOTICES.md
├─ Codex-setup/ → .Codex/   навык /next-task (SKILL.md, done.mjs), hooks/guard.ps1, settings.json
├─ .queue/                 current.md, state.json, log/, screenshots/, r<N>/, build.lock — не в Git
├─ docs/                   эталон, план, журнал, карта, справочники, scope.json
├─ cmake/                  product (версия), mupdf, qt, warnings, pch, tests, asan, deploy, features
├─ src/core/               ottisk_core: mu/ doc/ render/ text/ print/ edit/ sign/ annot/ forms/ pages/
│                          create/ decor/ links/ crypt/ ocr/ a11y/ optimize/ util/
├─ src/services/           ottisk_services: DocumentSession, настройки, пулы, процессы, Fake*-бэкенды
├─ src/ui/                 ottisk_ui_lib + AcroBatya.exe: app/ theme/ widgets/ shell/ view/ tools/ panes/ dialogs/ shots/
├─ tests/                  unit/ ui/ engine/ render/ e2e/ compat/ bench/ build/ corpus/ fixtures/ data/mini/
├─ fuzz/                   pdf_open_fuzzer.cpp (пресет msvc-fuzz)
├─ tools/                  vcenv.cmd, verify.py, build.ps1, build_lock.ps1, shots.ps1, lint.ps1, стражи *.py,
│                          imgcmp/, render_refs.cpp, queue-lib.ps1, test_reset_parse.ps1, test_guard.ps1, gen/ …
├─ installer/              ottisk.iss (UTF-8 BOM), app.ico, app.rc, AcroBatya.manifest, licenses/
├─ i18n/                   ottisk_ru.ts, ottisk_en.ts (9.2)
└─ assets/                 icons/ (только нужные Lucide), stamps/, help/ru, help/en
```

Подробно — `docs/map.md`.

## Команды (из корня репозитория)

`cl`, `cmake`, `ctest`, `ninja`, `clang-format` доступны только после `tools\vcenv.cmd` (vcvars64 + LLVM из Build
Tools; в `PATH` машины их нет). Команды ниже — для PowerShell; в Bash (Git Bash) вместо `cmd /c` — `cmd //c`.
Окружение очереди (`QT_QPA_PLATFORM=offscreen`, `OTTISK_PYTHON`, `VCPKG_ROOT`, `QT_ROOT`, `MUPDF_ROOT`,
`TESSDATA_PREFIX`, `OTTISK_INSTANCE=test`) задаёт `.Codex/settings.local.json`.

- `powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_lock.ps1 -Target <цель>` — сборка цели в `build/debug`
  под замком `.queue/build.lock`, лог `build/debug/build.log`
- `cmd /c "tools\vcenv.cmd && ctest --test-dir build/debug -R <регекс> --output-on-failure"` — тесты рядом с правкой;
  `cmd /c 'tools\vcenv.cmd && ctest --test-dir build/debug -L "^(unit|ui)$" -LE slow -j 12 --timeout 60 --output-on-failure --no-tests=error'` — быстрые
- `py -3.14 tools\verify.py [--full] [--preset msvc-release --labels "render|bench"]` — configure при нужде, сборка,
  формат и стражи, ctest; печатает только упавшее; код 0 — зелёный
- `cmd /c "tools\vcenv.cmd && clang-format -i <файлы>"` — формат
- `powershell -NoProfile -ExecutionPolicy Bypass -File tools\shots.ps1 -Only home[,…] [-Theme dark]` — снимки
  сцен в `.queue/screenshots/` (offscreen, 1440 × 900); смотреть PNG
- `build\debug\bin\AcroBatya.exe --self-check | --version | --shots <сцены> --out <каталог> | --smoke <с>` (с
  `C:\Qt\6.12.0\msvc2022_64\bin` в `PATH`)
- `"$env:OTTISK_PYTHON" -X utf8 -I tools/<скрипт>.py` — всё на Python над PDF, корпусами и скачанным
- `tools\build.ps1` — выпуск → `dist/AcroBatya-<версия>-setup.exe` (только рубеж и 9.4)
- `node .Codex/skills/next-task/done.mjs "этап N: …" "<кусок пункта>"` — коммит и отметка пункта
- `.\run-queue.ps1 -Status` — прогресс очереди (подробно — `QUEUE.md`)

## Правила для автономной работы (эталон 8.3)

1. **Ничего интерактивного.** Никаких команд, которые ждут ввода, диалогов Windows, UAC.
2. **Qt без экрана.** Тесты и сцены — offscreen (глобальный `env`); окно на экране человека открывает
   только рубеж (`--smoke 8` с `$env:QT_QPA_PLATFORM=''`), на секунды.
3. **Ничего живого.** Нет экрана, принтера, сканера, Acrobat Reader, настоящих сертификатов, сети в тестах,
   LibreOffice до установки человеком — всё через `Fake*` (`FakeScanSource`, `FakeTts`, `FakeMailer`,
   `FakeHttpFetcher`, `FakeOfficeBackend`, `FakeOcrBackend`, `FakeShellLauncher`…), локальные TSA/OCSP,
   `PFXImportCertStore(NO_PERSIST_KEY)` и строку «Нужна ручная проверка». LibreOffice нет к 7.3 — не `BLOCKED`,
   а `FakeOfficeBackend` + `SKIP 77`.
4. **Только чтение:** `C:\Qt`, `C:\src\mupdf`, `C:\Tools` (с `vcpkg` и корпусом `ottisk\corpus`; писать можно в `C:\Tools\logs`),
   эталон и справочники; данные человека (`%LOCALAPPDATA%\AcroBatya`, профиль Acrobat, хранилище сертификатов)
   не трогать — у тестов `OTTISK_DATA_DIR` в `build`, у очереди `OTTISK_INSTANCE=test`. Хук `guard.ps1` это держит.
5. **Задание — сессия, шаг — пункт.** Пункты по очереди: одна законченная правка, один коммит, отметка `[x]`
   **сразу после коммита** (`done.mjs`). Пункты чужих сессий «заодно» не делаются.
6. **Сначала тесты** для `core`: gtest-файл с `TEST(...)` до кода, для XL-сессий — с ожиданиями JSON на
   фикстурах; тест, закрывающий ID каталога, пишет `OT_FEATURE("<id>")`.
7. **Проверки:** по ходу — сборка своей цели и тесты рядом с правкой; в конце сессии — `py -3.14 tools\verify.py`
   зелёный; экран построен или изменён — сцена и **просмотр PNG**.
8. **Сборка:** рабочая — `build/debug`, один Ninja за раз (`tools\build_lock.ps1`); `build/release` — только рубежи,
   9.4, сессии 1.1 (`render_refs`) и 1.2 (`bench`); Release-цели — `AcroBatya render_refs render_tests bench`.
   Новый файл — в каталог своей цели (GLOB), CMake не правится; цели, каталоги, PCH и `CMakeLists.txt` меняет
   только главный исполнитель сессии.
9. **Эталоны рендера** — только `render_refs --update --reason "…"` из сессии, которой это поручено (1.1, смена
   MuPDF); расхождение Debug ↔ Release — `--mark-release-only`, а не правка порогов.
10. **Интерфейс — эталон 6 закон.** Цвета и размеры — только токены `src/ui/theme/tokens.h`; строки интерфейса —
    только в `ui` внутри `tr()` (страж `unit.guard.i18n`), `core`/`services` возвращают коды и английский журнал;
    **кириллица в `src` — только в `tr()`/`QT_TR_NOOP()`/`QCoreApplication::translate()` и комментариях** (контекст
    `translate` — ASCII, имя класса; в `core`/`services` — только комментарии); тексты — по-русски, коротко,
    термины Acrobat; претензия к виду решается в токенах и компонентах, не в сцене.
11. **Кодировки и пути:** `/utf-8`; пути `QString::toStdWString` → `fz_open_file_w`/`_wfopen`/`TIFFOpenW`
    (`core/util/paths.h`); каталоги данных — только ASCII; настройки — `settings.ini`, не реестр
    (`unit.guard.no_registry`); `.ps1`/`.iss` — UTF-8 с BOM (`unit.guard.encoding`); Python над PDF —
    `"$env:OTTISK_PYTHON" -X utf8 -I`, `py -3.14` — только `verify.py` и создание venv.
12. **Операции с перезаписью** (объединение, уменьшение размера, оптимизация, OCR «Exact», экспорт, Office) создают
    новый документ «Без имени» без undo; остальное — через журнал с undo.
13. **Ничего сверх плана.** Эталон 1.6 и 1.7 (вымарывание, сравнение, PDF/A-конвертация, облако, обновления…)
    не добавлять, даже если быстро; идеи — строкой в журнал.
14. **Отметки:** пункт `[x]`, если его собственный результат сделан; живая проверка — строка в «Нужна ручная
    проверка», а не `[~]`/`NEEDS_HUMAN` (те — только когда результат без человека невозможен, как 10.1).
15. **Git:** коммиты на русском `этап N: что сделано`; версия `0.N.0` и тег `v0.N.0` ставит последний пункт
    этапа N; `git push`, `reset --hard`, `clean -f` запрещены, удалённого репозитория нет.
16. **Субагенты и ultracode** (очередь по умолчанию — Opus 5.5 xhigh): агенты не собирают (кроме единственного исполнителя — своя цель через
    `build_lock.ps1`), не запускают `verify.py`/`shots.ps1`, не правят CMake/PCH/план/журнал и не коммитят —
    подробно в разделе «Ultracode» навыка `/next-task`.

## Журнал `docs/progress.md`

Одна запись на сессию (не на пункт), в конец раздела «Журнал»:

```markdown
### 2026-11-02 · Сессия 1.1 · Контекст, ошибки, открытие, рендер и эталоны
- Сделано: … (по пунктам)
- Решения: …
- Подводные камни: …
- Замеры: configure …, сборка …, линковка …
- Проверки: verify.py зелёный; сцены: home
```

Пункты, которые требуют человека (экран, принтер, сканер, Acrobat Reader, сертификат, LibreOffice, пилот),
собирай в разделе «Нужна ручная проверка» в начале файла — новые сверху, с номером сессии или рубежа.
