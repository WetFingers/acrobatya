# Справочник технических фактов: настольный PDF-редактор уровня Acrobat Standard

Дата: 2026-10-21. Стек: C++20 + Qt 6 (Widgets), движок MuPDF 1.28.5 (AGPL допустим — программа для себя), только Windows 10/11, сборка MSVC 2022 (Build Tools 17.14, toolset 14.44), Office ↔ PDF через LibreOffice headless. Разработку ведёт автоматическая очередь Claude Code (сессии `claude -p` без человека), поэтому всё ниже ориентировано на сборку и тесты без экрана (`QT_QPA_PLATFORM=offscreen`), установку инструментов без кликов и аккаунтов и точные команды.

**Как собран справочник.** Сведены пять отчётов (MuPDF; Qt 6; сборка/зависимости/упаковка; подписи PAdES; конвертация/OCR/печать/сканирование; тестовые корпуса), каждый проверен скептиком по первоисточникам (документация, исходники тега MuPDF 1.28.5, GitHub/PyPI/Jira API, Microsoft Learn, ETSI/OpenSSL/RFC) и локальными проверками на целевой машине (две реальные сборки MuPDF, установленный kit Qt 6.12.0, живые запросы к четырём TSA, `winget show`, реестр, `Get-Printer`). WebSearch во всех проверках был исчерпан — всё, что требовало поиска (форумы, отчёты об ошибках, малоизвестные корпуса), не просматривалось и вынесено в §9 «Открытые вопросы». Повторы между отчётами убраны; исправления скептиков внесены в текст. Номера строк относятся к тегу MuPDF 1.28.5 (коммит `8ad45e92f0935d3d87f1db3f873086472a5e1b24`, автор 2026-09-24, коммит 2026-09-25).

**Целевая машина (проверено 2026-10-21).** Windows 11 Home 10.0.26200; i7-13650HX (14 ядер / 20 потоков, гибридный); **15,71 ГБ ОЗУ** (свободно 0,45–1,2 ГБ в моменты проверок — нагрузка меняется; параллельно может работать Ollama); 373 ГБ свободно на диске; системная локаль de-DE (ACP 1252), консоль сессии 65001; профиль `C:\Users\WetFingers` (ASCII-путь).

**Артефакты проверок на машине** (сессионный scratchpad `C:\Users\WETFIN~1\AppData\Local\Temp\claude\C--Users-WetFingers-Claude\fd7ab2a3-5b86-49d6-ae98-978ec6a6e7bc\scratchpad\`): клон MuPDF 1.28.5 с двумя сборками (`mupdf-src\mupdf\platform\win32\x64\{Release,Debug}\`), логи `build-log\{steps.txt,msbuild.txt,steps-debug.txt,msbuild-debug.txt}`, дымовые тесты `smoke-тест\`, `verify-out\`, `verify-scripts\`; установленный Qt 6.12.0 (`Qt\6.12.0\msvc2022_64`), venv с рабочим aqt (`aqtgit`), тестовый проект `smoke`; PKI-скрипты и TSA-ответы (`pki`, `p12`); выборки корпусов (`dss`, `pdfs`). Всё это можно перенести в постоянные каталоги (`C:\Qt`, `C:\Tools`) вместо повторной установки.

---

## 0. Коротко — факты, определяющие архитектуру и план

| № | Факт | Следствие для архитектуры/плана | Источник |
|---|---|---|---|
| 1 | **MuPDF 1.28.5** — текущий релиз (тег 2026-09-25, `FZ_VERSION "1.28.5"`; тегов 1.29.x нет, master = 1.29.0), AGPL v3. Официальной CMake-сборки нет; Windows-сборка — `platform/win32/mupdf.sln` (v142 → нужен `/p:PlatformToolset=v143`). **Измерено на этой машине:** submodules 136,8 с; Release x64 `mutool` 562,6 с (0 ошибок, 2469 МиБ); Debug 169,9 с (1415 МиБ). | Собирать из исходников через .sln (не vcpkg); Debug — для быстрого цикла очереди (в 3,3 раза быстрее, нет `/GL`); линковать статические `.lib` в Qt-приложение с `/MD`. | `include/mupdf/fitz/version.h:26-29`; `gh api repos/ArtifexSoftware/mupdf/tags`; `build-log\steps.txt`, `steps-debug.txt` |
| 2 | vcpkg-порт **`libmupdf` = 1.26.10** (отстаёт на две ветки), на Windows только статический, `FZ_ENABLE_JS=0`, `FZ_ENABLE_ICC=0`, `FZ_ENABLE_DOCX_OUTPUT=0`, без extract/zxing/mujs, `source/helpers/pkcs7/*` не компилируется. Порта `mupdf`, `pdfium`, `qtpdf` в vcpkg нет. | vcpkg — только для вспомогательных библиотек (openssl, qpdf, tiff, fmt, gtest…); MuPDF — из .sln. | `ports/libmupdf/{vcpkg.json,portfile.cmake,CMakeLists.txt}` (HEAD `857036234d15…`, 2026-10-20) |
| 3 | **Qt 6.12.0 LTS** вышла 2026-09-30 (prebuilt `msvc 19.44.35227.0` совпадает с MSVC 14.44 на машине); «Qt 6.12 will be the last version to support Windows 10». Open-source патчи ожидаются до 6.13 (~весна 2027), далее с задержкой ~год (практика 6.5/6.8). Запасной вариант — 6.10.3. | Брать 6.12.0; `find_package(Qt6 6.12 …)`; `cmake_minimum_required(3.25)`. | qt.io/blog/qt-6.12-released; doc.qt.io/qt-6/windows.html; download.qt.io/archive/qt/6.8/ |
| 4 | Установка Qt без аккаунта — **aqtinstall**, но релиз 3.3.0 (PyPI, 2025-06-02) не знает раскладку репозитория Qt ≥ 6.11; нужен master GitHub (локально `v3.3.1.dev166` ставит 6.12.0 за 194–196 с: 266 МБ скачать, 2 478 МБ на диске). **Python 3.15** (системный 3.15.0b3 и финальный 3.15.0) не годится — у `pyppmd`/`pybcj`/`inflate64`/`brotli` нет wheel `cp315`; на машине есть `py -3.14` (3.14.6). | `py -3.14 -m venv` + `pip install git+https://github.com/miurahr/aqtinstall.git`; запасной путь — ручная загрузка `.7z` по URL из `Updates.xml`. | aqtinstall CHANGELOG (Unreleased); PyPI JSON; `aqt list-qt` локально |
| 5 | **QtPdf (PDFium)** — только чтение/рендер/текст, API сохранения нет; с 6.12 выпускается отдельным продуктом 6.140.0 (QTBUG-149519), aqt берёт старый пакет → «procedure entry point not found» (issue #1047, PR #1048 open). | Просмотр — **свой виджет на MuPDF** (один движок для экрана, миниатюр, печати, снимков); QtPdf не брать. | doc-snapshots.qt.io/qtpdf; qt-project.atlassian.net/rest/api/2/issue/QTBUG-149519 |
| 6 | **Offscreen на Windows работает** (`plugins/platforms/qoffscreen.dll` в prebuilt), но берёт `QFreeTypeFontDatabase` и сканирует только `QT_QPA_FONTDIR` (`lib/fonts` в kit нет; `.ttc` не читается) → без переменной `families: 0`; с `QT_QPA_FONTDIR=C:\Windows\Fonts` — 176 семейств, кириллица в PNG корректна, шрифт по умолчанию «Agency FB». `qInfo` без консоли уходит в `OutputDebugString`. | В `ENVIRONMENT` тестов: `QT_QPA_PLATFORM=offscreen;QT_QPA_FONTDIR=C:/Windows/Fonts;QT_FORCE_STDERR_LOGGING=1`; `QApplication::setFont(QFont("Segoe UI", 9))`; стиль `fusion`. | qoffscreenintegration.cpp, qfreetypefontdatabase.cpp, qlogging.cpp (qtbase); тест `scratchpad\smoke` (152 мс) |
| 7 | **Правила потоков MuPDF:** один контекст / документ / устройство — один поток одновременно; `fz_locks_context` обязателен при многопоточности и имеет **пять полей** (`user, lock, unlock, create_external_mutex, destroy_external_mutex`), `FZ_LOCK_MAX = 3`; display list, полученный из документа, можно рендерить из любого потока через `fz_clone_context`. | Архитектура: один «серверный» поток документа (все `pdf_*`-правки, загрузка страниц, display lists) + `QThreadPool` рендер-потоков с клонами контекста; отмена через `fz_cookie.abort`. | docs reference/c/overview; `include/mupdf/fitz/context.h` |
| 8 | Zero-copy рендер в Qt: `fz_new_pixmap_with_data(ctx, fz_device_bgr(ctx), w, h, NULL, 1, img.bytesPerLine(), img.bits())` поверх `QImage(Format_ARGB32_Premultiplied)` + `fz_clear_pixmap_with_value(…, 0xff)`; `fz_close_device` обязателен до `fz_drop_device`. | Тайлы 512×512 device px, `QCache<Key,QImage>` 256–512 МБ, `setDevicePixelRatio(dpr)`. | `include/mupdf/fitz/{pixmap.h,color.h,util.h,device.h}` |
| 9 | **Линеаризация удалена в MuPDF 1.26** (`pdf_write_document` при `do_linear` бросает «Linearisation is no longer supported»); `incremental` несовместим с `garbage` и сменой шифрования. | Линеаризация — внешним **qpdf 12.4.2** (`--linearize`); после подписи — только инкрементальное сохранение. | `source/pdf/pdf-write.c:2918-2942`; artifex.com/blog/mupdf-removes-linearisation |
| 10 | **Подписи в MuPDF:** есть плейсхолдер/ByteRange/инкрементальная запись, внешний вид, интерфейсы `pdf_pkcs7_signer/verifier`; но `/SubFilter` жёстко `adbe.pkcs7.detached`, помощник `pkcs7-openssl.c` — PKCS7-API с **SHA-1**, доверие только к встроенному `AdobeCA.p7c`, нет PAdES/TSA/LTV/DocMDP; OpenSSL не поставляется (`HAVE_LIBCRYPTO` только в `*Extra`, ожидается `thirdparty\openssl` и `libcrypto-1_1-x64.dll`). | Свой signer/verifier на **OpenSSL ≥ 3.5** (`CMS_CADES` с 3.0, `CMS_NO_SIGNING_TIME` только с 3.5.0), SHA-256, `ETSI.CAdES.detached` по умолчанию, патч словаря `/V` после `pdf_sign_signature`, TSA по HTTP, DSS/DocTimeStamp; резерв `/Contents` **16 КиБ** (токен GlobalSign 7 660 байт). MuPDF собирать без `HAVE_LIBCRYPTO`. | `source/pdf/pdf-form.c:1924-2000`; `source/helpers/pkcs7/pkcs7-openssl.c:28-47,721`; openssl-3.5 CHANGES.md; живые TSA-запросы |
| 11 | **Acrobat:** формат по умолчанию `adbe.pkcs7.detached`, допускает `ETSI.CAdES.detached` (с 10.x); хэш SHA-256 с 9.1; LTV включён по умолчанию (`bIsEnabled=1`, `iAutoAddLTV=1` = «спросить, если велико»), данные LTV — по ETSI TS 102 778-4 (DSS); корням Windows **не доверяет** (`iMSStoreTrusted=0`), доверие через AATL/EUTL; `bValidateOnOpen=1`. | Паритет = B-B/B-T сразу, B-LT (DSS) вторым, B-LTA третьим; собственный список доверия + AATL/EUTL (`tl12`/`eutl12.acrobatsecuritysettings`); Windows ROOT — опция, по умолчанию выкл. | adobe.com/devnet-docs/acrobatetk/tools/PrefRef/Windows/Security.html; DigSigDC/planning.html, standards.html |
| 12 | **Формы:** чтение/заполнение/события/JS есть, но создать можно **только** подписное поле (`pdf_create_signature_widget`); XFA не поддерживается; `NeedAppearances` соблюдается с 1.27. **Редактирование текста** в контент-потоке — API нет вовсе (есть `pdf_processor`/фильтры/`pdf_update_stream`). | Создание Tx/Btn/Ch-полей через `pdf_create_annot_raw` + `pdf_dict_*` по образцу `pdf-form.c:1052-1095` (средний блок); редактор текста — самый большой блок проекта. | `include/mupdf/pdf/form.h:67`; `include/mupdf/pdf/interpret.h` |
| 13 | **Office → PDF через MuPDF теряет картинки** (проверено живым docx: текст и таблицы есть, `/Image` XObject 0), качественный Office — платный SmartOffice. **LibreOffice** 26.8.1 (MSI 358 МБ) в winget только `Scope: machine` → UAC; `msiexec /a … TARGETDIR` без прав не подтверждён. **Word 16 x64 (M365)** есть на машине → PDF → DOCX через COM (`Documents.Open` + `SaveAs2(…,16)`), DOCX → PDF/A-1 через `ExportAsFixedFormat(UseISO19005_1)`. PDF → XLSX штатно невозможен ни в LO, ни в Word. | Office→PDF — LibreOffice headless с JSON-опциями `writer_pdf_Export` (PDF/A-2b, теги); PDF→DOCX — Word COM с fallback на `writer_pdf_import`; честный UI «PDF→XLSX недоступно». | `verify-out\проба-из-docx.pdf`; winget-манифест LibreOffice 26.8.1.1; `HKCR\Word.Application\CurVer`; help.libreoffice.org pdf_params |
| 14 | **OCR:** Tesseract 5.5.2 + Leptonica 1.87.0 — сабмодули MuPDF, `HAVE_TESSERACT/HAVE_LEPTONICA` во всех 27 конфигурациях `libmupdf.vcxproj`, `libtesseract.vcxproj` собирается с `DISABLED_LEGACY_ENGINE` и AVX2 → **нет OSD/автоориентации и `--oem 0`**; данные `tessdata_fast` rus 3,9 МБ + eng 4,1 МБ + deu 1,5 МБ; traineddata открываются узким `fopen` (ANSI-путь). | OCR — через `fz_new_ocr_device`/`fz_new_pdfocr_writer` на тех же `libtesseract.lib`/`libleptonica.lib` (одна копия Tesseract); tessdata в ASCII-путь; ориентацию определять самим; запасной — Windows.Media.Ocr. | `platform/win32/libtesseract.vcxproj`; `source/fitz/tessocr.cpp`; `src/ccutil/serialis.cpp` (Tesseract); GitHub contents API tessdata_fast |
| 15 | **Build Tools 17.14.37710** (MSVC 14.44.35207, cl 19.44.35229, SDK 10.0.26100): `cmake 3.31.6-msvc6` и `ninja 1.12.1` попадают в PATH после `vcvars64.bat`; `clang-format`/`clang-tidy` **19.1.5** уже есть (`VC\Tools\Llvm\x64\bin`), **`clang-cl` нет**; ASan (`/fsanitize=address`) и libFuzzer (`/fsanitize=fuzzer`) есть; PREfast + CppCoreCheck есть; языковой пакет MSVC только русский (1049). | `/utf-8` обязателен; `msvc_deps_prefix = Примечание: включение файла:` (CMake выставляет сам; для ccache `CCACHE_MSVC_DEP_PREFIX`); UBSan/clang-cl — только с отдельным LLVM 23.1.4 (portable tar.zst 264 МиБ). | vswhere, state.json, `where cmake` после vcvars, листинги Build Tools |
| 16 | **C++20 безопасен на 14.44:** `std::format`, `views::filter/transform`, `format("{:%Y}", year)` компилируются под `-std:c++20` (проверено дважды); сноска 20abi в таблице соответствия устарела. | `CMAKE_CXX_STANDARD 20`, без modules (sccache их не кэширует, AUTOMOC сыроват) и без `/std:c++latest`. | learn.microsoft.com /std; пробная сборка `cmtest` |
| 17 | Qt из aqt собран с `/MD` → все vcpkg-зависимости — триплет **`x64-windows-static-md`** (release-only: community `x64-windows-static-md-release` уже есть). `VCPKG_MAX_CONCURRENCY` по умолчанию 21 при 15,7 ГБ ОЗУ; Ninja по умолчанию `processors + 2` = 22 `cl.exe`. | `VCPKG_MAX_CONCURRENCY=10`, `cmake --build -j 10`, `JOB_POOLS compile=10 link=2`; не запускать две сборки одновременно. | `triplets/community/x64-windows-static-md-release.cmake`; ninja.cc `GuessParallelism` |
| 18 | **Лимиты `claude -p`:** foreground-команда 120 с (`BASH_DEFAULT_TIMEOUT_MS`), потом уходит в фон; фоновый лимит в `-p` с текстовым промптом — **10 мин**, иначе 30 мин, максимум 2 ч; вывод > ~30 000 символов приходит файлом + превью 2 000 символов. | В `settings.local.json` → `env`: `BASH_DEFAULT_TIMEOUT_MS=1800000`, `BASH_MAX_TIMEOUT_MS=7200000`, `CLAUDE_CODE_PRINT_BG_WAIT_CEILING_MS=0`; сборки — в лог-файл + `Select-String`; первый `vcpkg install` — фоновая команда с `timeout` 7 200 000 мс. | code.claude.com/docs/en/{tools-reference,env-vars,headless} |
| 19 | **Inno Setup 6.7.3** уже стоит per-user (`%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe`); `PrivilegesRequired=lowest` → `{autopf}` = `{localappdata}\Programs`, `HKA` = HKCU; с Windows 10 программа не может сама назначить себя обработчиком `.pdf` — только `Capabilities` + `RegisteredApplications` + `ms-settings:defaultapps?registeredAppUser=…`. `vc_redist.x64.exe` есть в Build Tools (`VC\Redist\MSVC\14.44.35112`), тихая установка требует UAC → app-local CRT (вся папка `Microsoft.VC143.CRT`, 10 DLL). | Инсталлятор per-user без админа; `windeployqt --no-compiler-runtime …`; регистрация ProgID в HKCU. | jrsoftware.org/ishelp; learn.microsoft.com default-programs, launch-default-apps-settings; реестр `Inno Setup 6_is1` |
| 20 | **Инструменты проверки совместимости без Acrobat** — все portable без админа: qpdf 12.4.2 (`qpdf-12.4.2-msvc64.zip`, коды 0/2/3), pdfcpu 0.16.3 (zip; exit 0/1), veraPDF 1.30.3 (Java: на машине есть JBR OpenJDK 25.0.3 из Android Studio; коды возврата 0…12 в исходниках), pypdfium2 5.14.0 (движок Chrome/Edge), pyHanko 0.38.0, pikepdf 10.17.0; Python-зависимости на `py -3.14` (scikit-image 0.26.0 без cp315). | `tools/compat_check.py` + ctest `LABELS compat`, `SKIP_RETURN_CODE 77`; `requirements-test.txt` зафиксирован. | GitHub releases API; `CliConstants.java` (veraPDF); `cmd/pdfcpu/main.go`; PyPI JSON |
| 21 | **Корпуса PDF не копировать в репозиторий** — хранить манифест (URL + sha256 + лицензия + теги + ожидание) и загрузчик. Лицензии: veraPDF-corpus (2 906 PDF, в т.ч. Isartor 205) CC BY 4.0; pdfium_tests BSD-3; pdf-differences CC BY 4.0; safedocs Apache-2.0; BFO CC BY 3.0; **pdf20examples CC BY-SA 4.0**; DSS — 108 PAdES-PDF в LGPL-репозитории; pdf.js — лицензия PDF не задокументирована (460 файлов как `.link`); формы IRS и GovDocs1 — public domain; корпус OSS-Fuzz MuPDF (232 МБ) доступен. | `tests/corpus/manifest.json` + `fetch_corpus.py` (`py -3.14 -I`) в `%LOCALAPPDATA%\bny-acro\corpus\`. | GitHub trees/contents API; LICENSE-файлы; `curl -I` |
| 22 | **Рендер-тесты** по схеме pdfium: эталоны PNG + нечёткое сравнение `max delta ≤ 3 ∧ MSE ≤ 0.05 ∧ wMSE ≤ 15` (или SSIM > порога); эталоны рендерить напрямую MuPDF (не через виджет), фиксировать версию MuPDF; `gtest_discover_tests(DISCOVERY_MODE PRE_TEST)`. | `tools/imgcmp` (C++), `tests/render/expected/` + `expected.json`, метки ctest `unit/render/slow/compat/fuzz`. | pdfium `testing/tools/pngdiffer.py`; cmake GoogleTest module |
| 23 | **Кодировки:** PowerShell 5.1 скрипт с кириллицей **обязан иметь BOM** (иначе «ANSI»); `Out-File`/`>` — UTF-16LE, `Set-Content` — ANSI без `-Encoding`; Python — `PYTHONUTF8=1`; `std::filesystem::path(std::string)` трактует байты в ACP 1252 → только `path(u8"…")`/`QString::toUtf8()`; MuPDF — `fz_open_file_w(ctx, wchar_t*)` публична. | Пролог всех скриптов очереди (UTF-8 консоль, `$PSDefaultParameterValues['*:Encoding']='utf8'`); `QT_NO_CAST_FROM_ASCII`; `fz_open_document_with_stream(fz_open_file_w(...))`. | about_Character_Encoding; `include/mupdf/fitz/stream.h:81`; memory `powershell-cyrillic-needs-bom` |
| 24 | **OpenSSL:** ветка 3.0 — EOL (`qt.tools.opensslv3.win_x64` = 3.0.16, не брать); поддерживаются 3.5 LTS (до 2030-04-08) и 4.0 (до 2027-05-14); vcpkg `openssl` = 3.6.5 (поддержка до 2026-11-01); FireDaemon поставляет 3.5.x с `include`/`lib` (zip без админа). Для TLS в Qt хватает Schannel (`tls/qschannelbackend.dll`). | Крипто-ядро на OpenSSL 3.5 LTS (vcpkg с `overrides` на 3.5.x или FireDaemon zip); не класть OpenSSL DLL рядом с exe (Qt пробует OpenSSL первым). | openssl-library.org/source, /policies/releasestrat; kb.firedaemon.com 4000121705; ports/openssl/vcpkg.json |
| 25 | **Печать и сканирование:** `QPrinter` (NativeFormat) + растр MuPDF при DPI принтера; плагина printsupport на Windows нет (всё вкомпилировано в `Qt6PrintSupport.dll`); принтер «Microsoft Print to PDF» есть на машине (порт `PORTPROMPT:`), тихая печать в файл — GDI `StartDoc` с `lpszOutput`; сканирование — WIA 2.0 (`IWiaDevMgr2::GetImageDlg`, `IWiaTransfer::Download`), TWAIN на старте не нужен. | Буклет и «вписать» — самим; `IScanSource` + `FakeScanSource` для offscreen-тестов. | doc.qt.io qprinter; `src/printsupport/CMakeLists.txt`; `Get-Printer`; MS Learn WIA |

---

## 1. MuPDF 1.28.5 — движок: возможности C API и сборка под Windows

Метод исходной проверки: локальный клон тега 1.28.5 (`scratchpad\mupdf-src\mupdf`, HEAD `8ad45e92…`, `git describe` = `1.28.5`) с grep/sed по заголовкам и исходникам; две реальные сборки MSBuild; живые тесты `mutool.exe`; WebFetch mupdf.readthedocs.io, блога Artifex, raw-файлов vcpkg/OpenSSL; `gh api` для дат тегов.

### 1.0 Сводка «есть / писать самим»

| Потребность | Есть в MuPDF 1.28.5 «из коробки» | Писать самим |
|---|---|---|
| Просмотр (рендер, кэш, многопоточность) | Полностью: `fz_new_draw_device`, display lists, store 256 МиБ (`FZ_STORE_DEFAULT`), `fz_locks_context` | Qt-обвязку: пул потоков, клоны контекста, тайлинг |
| Текст / поиск | `fz_stext_*`, `fz_search_stext_page`, новый `fz_new_search` (regex, через страницы; 1.27, «experimental») | UI выделения; «найти/заменить» — замены в MuPDF нет |
| Аннотации | Создание/правка/удаление 28 типов, внешний вид синтезируется для 18 типов (`pdf_update_page`); `pdf_create_link` | Инструменты рисования, Popup-логика, custom stamps |
| Формы AcroForm | Чтение, заполнение, события, JS; создание **только** подписных полей (`pdf_create_signature_widget`) | Создание текстовых/чекбокс/радио/списочных полей через `pdf_create_annot_raw` + `pdf_dict_*`; FDF-инструменты |
| JavaScript | MuJS, `pdf_enable_js`, AF*-функции форматирования | Покрытие Acrobat-JS узкое (§1.7) |
| Подписи X.509 | PKCS#7 `adbe.pkcs7.detached`, PFX через OpenSSL, видимый внешний вид, проверка | **PAdES/`ETSI.CAdES.detached`, TSA, LTV/DSS, SHA-256 в CMS, доверие к системным корням, сборка OpenSSL под MSVC** (глава 4) |
| Вымарывание | `pdf_redact_page` с режимами для картинок/линий/текста | UI |
| Страницы | Удаление/вставка/перестановка/перенос (`pdf_graft_mapped_page`, `pdf_rearrange_pages`) | Поворот — запись `/Rotate` вручную; UI |
| Сохранение | Инкрементально, garbage, objstms, AES-256 (R6 проверено), права | **Линеаризация удалена в 1.26** |
| Редактирование текста/картинок | Нет API правки текста; есть `pdf_processor`/фильтры, `pdf_add_image`, `pdf_update_stream` | **Редактор текста целиком**; замена XObject — просто |
| OCR | `fz_new_ocr_device`, `fz_new_pdfocr_writer`; Tesseract 5.5.2 + Leptonica 1.87.0 в thirdparty, в .sln во всех конфигурациях | Скачивание tessdata, UI, определение ориентации |
| Office → PDF | Открывает docx/xlsx/pptx/hwpx через конвертацию в HTML; **текст и таблицы есть, картинок нет** | LibreOffice headless |
| Вывод | PDF, SVG, PNG/JPEG/PNM…, PCL/PCLM/PS/PWG, TXT/HTML/XHTML/STEXT/CSV, DOCX/ODT (extract), OCR-PDF | TIFF на запись — libtiff |
| Сборка MSVC 2022 | `platform/win32/mupdf.sln` (нужно `/p:PlatformToolset=v143`); проверено дважды | Свой CMake (по образцу vcpkg) или .sln как есть |

### 1.1 Версия, даты, лицензия

| Факт | Значение | Источник |
|---|---|---|
| Текущий релиз | **1.28.5** (`FZ_VERSION "1.28.5"`, major 1 / minor 28 / patch 5); тегов 1.29.x нет; master — `FZ_VERSION "1.29.0"` | `include/mupdf/fitz/version.h:26-29`; `gh api repos/ArtifexSoftware/mupdf/tags` |
| Дата 1.28.5 | коммит тега **2026-09-25T02:11:56Z** («Bug 709752: Avoid dropping uninitialised document on error in bake»); GitHub Releases у проекта пустые | `gh api repos/ArtifexSoftware/mupdf/commits/1.28.5` |
| Ряд 1.28 | 1.28.0 — 2026-06-25; 1.28.1 — 07-30; 1.28.2 — 08-03; 1.28.3 — 08-22 («mutool poster restored (was broken in 1.28.0–1.28.2)» — единственная детализация в CHANGES); 1.28.4 — 09-15; 1.28.5 — 09-25; 1.27.2 — 2026-02-18 | GitHub API; `CHANGES:1-3` |
| Даты веток | 1.27.0 — 2025-12-12; 1.26.0 — 2025-05-12; 1.25.0 — 2024-11-20; 1.24.0 — 2024-03-19 | `gh api …/commits/<tag>` |
| Документация | mupdf.readthedocs.io/en/latest показывает «MuPDF 1.28.5» | — |
| Лицензия | **GNU AGPL v3** (`COPYING`), альтернативно коммерческая от Artifex | `COPYING`; шапка `pkcs7-openssl.c:1-21` |
| Репозиторий | git.ghostscript.com/mupdf.git, зеркало github.com/ArtifexSoftware/mupdf | docs guide/install |

Ключевое из `CHANGES` (строки файла в теге 1.28.5):
- **1.24** (`CHANGES:336-420`): обязательный `pdf_report_error` в финальном `fz_catch`; чтение Office (docx/pptx/xlsx) — «we internally open and convert … to HTML … The exact layout will NOT be preserved»; txt, gzip; FDF «using the low-level PDF functions. There are no tools for this yet»; CFB-архивы; `mutool bake`; objstms; compression effort; опции вымарывания line art/`remove-unless-invisible`; `pdf_rearrange_pages`; `fz_text_decoder`; субсеттинг шрифтов в `mutool clean -S` (экспериментально); OCR появился ещё в 1.18, прогресс/отмена — 1.19.
- **1.25** (`CHANGES:278-334`): опции вымарывания «don't redact text» и «redact line art»; rich text, callout у FreeText; leader/caption у Line; «Low level FDF format support» + `docs/examples/import-fdf.js`; ZUGFeRD; **`mutool audit`** (появился здесь, в 1.28 переписан с HTML-отчётом); `pdf_annot_rect` работает с «design»-прямоугольником; `color`→`argb` в stext; «Support for "SmartOffice" commercially licensed plugin».
- **1.26** (`CHANGES:138-276`): **удалена линеаризация** (блог 2025-03-19); object labels; Brotli; barcode (zxing-cpp, `make barcode=yes`); Paragraph Breaking и Table Hunt; CSV; JSON-парсер; «Removed bit-rotted sections of the Makefile … (primarily iOS and MinGW)»; «Support non-latin/greek/cyrillic/CJK languages in annotations and widgets (requires HTML layout engine)»; «Fixed several bugs with undo/redo journal affecting the cache of open pages»; `mutool clean --structure=keep` «will prevent the file from shrinking»; `pdf_clip_page`; `fz_outline_item.flags`/цвет.
- **1.27** (`CHANGES:57-136`): «New (experimental) search API … across pages and regular expressions»; conditional compilation html/xps/svg/extract; VS-проект получил ARM64; «Respect AcroForm/NeedAppearances flag»; NoZoom/NoRotate; `mutool merge: preserve external (only) links`; `mutool grep`; `PDF_TEXT_REMOVE_INVISIBLE`; `pdf_check_document`; `pdf_set_page_tree_cache`; `fz_stext_page_details`.
- **1.28** (`CHANGES:5-55`): Markdown; новый `fz_options` API (убраны `fz_has_option`, `fz_copy_option`, `fz_option_eq`); «decrypt» устарел → `encrypt=none`; новый `mutool audit`; улучшения incremental search API и `mutool grep` (regex по умолчанию); `fz_check_document`; шейдинги при XPS→PDF; `fz_ckd_*`; `pdf_validate_signature` переименован в `pdf_validate_signature_widget`, новая `pdf_validate_signature` работает по полю; `mutool draw`: overprint `-O` → `-M`, `-O` теперь device-опции.

### 1.2 Контексты, потоки, замки

- `fz_context *fz_new_context(alloc, locks, max_store)` (макрос над `fz_new_context_imp`, `context.h:931`); `FZ_STORE_UNLIMITED = 0`, `FZ_STORE_DEFAULT = 256 << 20` (`context.h:314-315`). Далее `fz_register_document_handlers(ctx)`.
- Замки: клиент даёт `FZ_LOCK_MAX` (= 3) нерекурсивных мьютексов (`FZ_LOCK_ALLOC = 0`, `FZ_LOCK_FREETYPE`, `FZ_LOCK_GLYPHCACHE`; `context.h:256-281`). Структура **из пяти полей**: `typedef struct fz_locks_context { void *user; void (*lock)(void *user, int lock); void (*unlock)(void *user, int lock); void *(*create_external_mutex)(void *user); void (*destroy_external_mutex)(void *user, void *external_mutex); }` — при собственной реализации заполнять все поля (`new std::mutex`/`delete` для external); мьютексы «may be recursive mutexes, but do not have to be». Правило внутри MuPDF: «We can never take lock n when we already hold any lock i, where 0 <= i <= n» (проверка — `FITZ_DEBUG_LOCKING`).
- Правила (`docs/reference/c/overview.md:190-230`): (1) один контекст — один поток одновременно; (2) один документ — один поток одновременно, «once display lists are created from that document, multiple threads at a time can operate on them»; (3) одно устройство — один поток; (4) `fz_locks_context` обязателен, если библиотека используется не из одного потока, «All contexts in use must share the same fz_locks_context»; (5) клонировать контекст на поток — `fz_clone_context(ctx)` (`context.h:363`; общие allocator, store, locks; «their own exception stacks»).
- `fz_cookie{abort, progress, progress_max, errors, incomplete}` (`device.h`): «communication is unsynchronized without locking»; «once the application has set this field to 1 … it may not change the value again».
- Пример `docs/examples/multi-threaded.c`; онлайн cookbook/c/multi-threaded (рендерит страницы в потоках через display lists).

**Писать самим:** класс `MuContext` поверх `std::mutex[FZ_LOCK_MAX]`; «серверный» поток документа (все `pdf_*`-правки, загрузка страниц, `fz_new_display_list_from_page`) + пул рендер-потоков с клонами контекста; очередь задач с `fz_cookie` для отмены.
**Грабли:** доступ к `pdf_document` из второго потока без сериализации — UB; с 1.24 ошибки обязаны завершаться `pdf_report_error` в последнем `fz_catch` (`CHANGES:340-342`); `fz_drop_context` нельзя внутри `fz_try`.

### 1.3 Рендеринг и связка с QImage

Символы проверены grep по `include/mupdf`: `fz_new_pixmap_from_page`, `fz_new_display_list_from_page[_number|_contents]`, `fz_new_pixmap_from_display_list(ctx, list, ctm, cs, alpha)`, `fz_new_pixmap_from_page[_number|_contents]` («Render the page contents without annotations»), `fz_new_stext_page_from_page/_display_list` — все в **`include/mupdf/fitz/util.h`**; `fz_run_display_list` (`display-list.h`); `fz_new_draw_device`, `fz_new_draw_device_with_bbox` (`device.h`); `fz_run_page_contents` («Just the main page content, without the annotations») / `fz_run_page_annots` / `fz_run_page_widgets`, `fz_bound_page` («Determine the size of a page at 72 dpi») (`document.h`); `fz_empty_store`, `fz_shrink_store` (`store.h`); `fz_set_aa_level` (`context.h`); `fz_device_rgb/bgr/gray/cmyk/lab` (`color.h`); `fz_clear_pixmap_with_value` (`pixmap.h`). `FZ_ENABLE_SPOT_RENDERING` = 1 по умолчанию (`config.h:157-159`).

```c
fz_pixmap *fz_new_pixmap_from_page(ctx, page, ctm, cs, alpha);
fz_display_list *fz_new_display_list_from_page(ctx, page);
void fz_run_display_list(ctx, list, dev, ctm, scissor, cookie);
fz_device *fz_new_draw_device(ctx, transform, pixmap);
fz_device *fz_new_draw_device_with_bbox(ctx, transform, pixmap, &clip);
fz_pixmap *fz_new_pixmap_with_data(ctx, cs, w, h, seps, alpha, stride, samples); /* «using the supplied data block» */
```

Факты из `pixmap.h`: «The components are in the order process-components, spot-colors, alpha»; `n = num composite colors + num spots + num alphas`; `stride` — байтовое смещение до той же точки строкой ниже; «The data is in premultiplied alpha when rendering, but non-premultiplied for colorspace conversions and rescaling»; `xres/yres` по умолчанию 96; `fz_clear_pixmap_with_value(ctx, pix, 0xff)` — «alpha will always be set to 255».

| MuPDF `cs`, `alpha` | байт/пиксель | QImage::Format (little-endian) |
|---|---|---|
| `fz_device_rgb`, 0 | 3 (R,G,B) | `Format_RGB888` (медленнее — конверсия) |
| `fz_device_rgb`, 1 | 4 (R,G,B,A premul) | `Format_RGBA8888_Premultiplied` |
| `fz_device_bgr`, 0 | 3 (B,G,R) | `Format_BGR888` |
| **`fz_device_bgr`, 1** | 4 (B,G,R,A premul) | **`Format_ARGB32_Premultiplied`** («родной» формат QPainter) или `Format_RGB32` при непрозрачном фоне |
| `fz_device_gray`, 0 | 1 | `Format_Grayscale8` |

Zero-copy путь (QImage владеет памятью, MuPDF рисует прямо в неё):
```cpp
QImage img(w, h, QImage::Format_ARGB32_Premultiplied);
fz_pixmap *pix = fz_new_pixmap_with_data(ctx, fz_device_bgr(ctx), w, h, nullptr, /*alpha*/1,
                                         int(img.bytesPerLine()), img.bits());
fz_clear_pixmap_with_value(ctx, pix, 0xff);                 // белый фон, alpha=255
fz_device *dev = fz_new_draw_device(ctx, fz_identity, pix); // transform уже в ctm
fz_run_display_list(ctx, list, dev, ctm, scissorRect, &cookie);
fz_close_device(ctx, dev); fz_drop_device(ctx, dev); fz_drop_pixmap(ctx, pix);
img.setDevicePixelRatio(dpr);
```
Если использовать `fz_new_pixmap_from_page*` — оборачивать `QImage(const uchar*, w, h, stride, fmt)` и сразу `.copy()` (буфер «must remain valid throughout the life of the QImage and all copies»; cleanup с `fz_drop_pixmap` требует контекст потока, где умрёт QImage — проще копировать).

**Грабли:** `fz_close_device` обязателен до `fz_drop_device` («Don't forget to call fz_close_device before dropping the device, or you may get incomplete output!»; «This is NOT called implicitly on fz_drop_device»); не держать `fz_pixmap` дольше QImage; pixmap без alpha + `fz_clear_pixmap_with_value(…, 0xff)` для белого фона; при дробном dpr вычислять размеры в device px и только потом делить на dpr.

### 1.4 Извлечение текста и поиск

- `fz_new_stext_page_from_page(ctx, page, &opts)` (`util.h`); 22 флага `fz_stext_options.flags` (`structured-text.h:196-217`): `PRESERVE_LIGATURES`, `PRESERVE_WHITESPACE`, `PRESERVE_IMAGES`, `INHIBIT_SPACES` (8), `DEHYPHENATE`, `PRESERVE_SPANS`, `CLIP`, `USE_CID_FOR_UNKNOWN_UNICODE` (128), `COLLECT_STRUCTURE`, `ACCURATE_BBOXES`, `COLLECT_VECTORS`, `IGNORE_ACTUALTEXT` (2048), `SEGMENT`, `PARAGRAPH_BREAK`, `TABLE_HUNT`, `COLLECT_STYLES`, `USE_GID_FOR_UNKNOWN_UNICODE` (65536), `CLIP_RECT`, `ACCURATE_ASCENDERS`, `ACCURATE_SIDE_BEARINGS`, `LAZY_VECTORS`, `FUZZY_VECTORS`.
- Поиск: `int fz_search_stext_page(ctx, text, needle, hit_mark, hit_bbox, hit_max)` (`:657`), `fz_search_stext_page_cb` (`:692`); новый `fz_search *fz_new_search(ctx, needle, fz_search_options)` (`:1046`), `fz_search_forwards/backwards` (`:1107,1121`), состояние `FZ_SEARCH_MORE_INPUT` (`:1051`), `fz_match_stext_page(_cb)` (`:1165,1180`). Опции (reference/common/search-options): `exact`, `regexp` (JS-регекс), `ignore-case`, `ignore-diacritics`, `keep-lines`, `keep-paragraphs`, `keep-hyphens`; опции «целое слово» нет.
- Выделение: `fz_highlight_selection`, `fz_copy_selection`, `fz_copy_rectangle`, `fz_snap_selection`; экспорт `fz_print_stext_page_as_text/html/xhtml/xml/json` — все в `structured-text.h`.

**Писать самим:** индекс по документу, «найти и заменить» (замены нет — это редактор текста, §1.11). **Грабли:** цвет в stext — поле `argb` (1.25); `fz_stext_page` может содержать несколько страниц (`fz_stext_page_details`, 1.27).

### 1.5 Аннотации

28 типов `enum pdf_annot_type` (`annot.h`): Text, Link, FreeText, Line, Square, Circle, Polygon, PolyLine, Highlight, Underline, Squiggly, StrikeOut, Redact, Stamp, Caret, Ink, Popup, FileAttachment, Sound, Movie, RichMedia, Widget, Screen, PrinterMark, TrapNet, Watermark, 3D, Projection.

Жизненный цикл (`annot.h`): `pdf_create_annot` (`:395`, «Populate it with sensible defaults per the type»), `pdf_create_annot_raw` (`:358`), `pdf_create_link(ctx, page, bbox, uri)` (`:364`), `pdf_delete_annot` (`:405`), `pdf_first_annot`/`pdf_next_annot`, `pdf_bound_annot`, `pdf_annot_rect` (`:516`), `int pdf_update_annot` (`:897`), `int pdf_update_page` (`:910`), `pdf_annot_request_synthesis/_resynthesis` (`:845,854`), `pdf_set_annot_appearance(ctx, annot, "N", state, ctm, bbox, res, contents)` и `..._from_display_list` (`:1013-1014`).

Сеттеры (точные имена): `pdf_set_annot_rect/contents/color/interior_color/opacity/flags/author/subject/name/border/border_width/border_style/border_effect/border_effect_intensity/quad_points/quadding/ink_list/vertex/vertices/line/line_start_style/line_end_style/line_ending_styles/line_leader/line_leader_extension/line_leader_offset/line_caption/line_caption_offset/callout_line/callout_point/callout_style/icon_name/default_appearance/rich_contents/rich_defaults/intent/is_open/popup/stamp_image/stamp_image_obj/filespec/language/creation_date/modification_date/field_value/active/hot/hidden_for_editing`. **`pdf_set_annot_border_dash` не существует** — пунктир через `pdf_clear_annot_border_dash(ctx, annot)` + `pdf_add_annot_border_dash_item(ctx, annot, length)` (`:659,664`), чтение `pdf_annot_border_dash_count/_item` (`:545,550`).

Appearance streams синтезируются (`source/pdf/pdf-appearance.c:2995-3066`) для 18 типов: Widget, Ink, Polygon, PolyLine, Line, Square, Circle, Caret, Text, FileAttachment, Sound, Highlight, Underline, StrikeOut, Squiggly, Redact, Stamp, FreeText. Для Link, Popup, Movie, Screen, 3D и т.п. — нет. JS-уровень (reference/javascript/types/PDFAnnotation) подтверждает `update/setAppearance/setAppearanceFromDisplayList/requestSynthesis`; стандартные имена иконок синтезируются, пользовательские — нет.

**Писать самим:** инструменты Qt (хит-тест по `pdf_annot_rect`/`pdf_bound_annot`), панель комментариев (`/IRT`, `/RC`), штампы (`pdf_set_annot_stamp_image` или display list), генератор тестовой матрицы всех подтипов. **Грабли:** кириллица в FreeText/виджетах требует HTML-движка — `FZ_ENABLE_HTML_ENGINE` включается автоматически, если включён любой HTML-агент (HTML/EPUB/MOBI/FB2/TXT/OFFICE), иначе `#error` (`config.h:60-68, 272-290`); после правок — `pdf_update_page`.

### 1.6 Формы AcroForm

`include/mupdf/pdf/form.h`: обход `pdf_first_widget`/`pdf_next_widget`/`pdf_update_widget`, `pdf_widget_type` → `PDF_WIDGET_TYPE_{UNKNOWN,BUTTON,CHECKBOX,COMBOBOX,LISTBOX,RADIOBUTTON,SIGNATURE,TEXT}` (`:32-39`), `pdf_bound_widget`, `pdf_field_flags`, `pdf_field_label`, `pdf_load_field_name`, `pdf_lookup_field`, `pdf_create_field_name`; значения `pdf_field_value`, `pdf_set_field_value`, `pdf_set_text_field_value`, `pdf_set_choice_field_value`, `pdf_edit_text_field_value`, `pdf_choice_widget_options/value/set_value/is_multiselect`, `pdf_choice_field_option(_count)`, `pdf_button_field_on_state`, `pdf_text_widget_max_len`, `pdf_text_widget_format`, `pdf_field_reset`, `pdf_reset_form`, `pdf_calculate_form`; оформление `pdf_field_set_border_style/button_caption/display/fill_color/text_color`; события `pdf_annot_event_enter/exit/down/up/focus/blur/page_*`, `pdf_annot_field_event_keystroke`, `pdf_field_event_calculate/format/keystroke/validate`, `pdf_document_event_will_close/will_save/did_save/will_print/did_print`; `pdf_bake_document(ctx, doc, bake_annots, bake_widgets)` (`:379`).

**Создание полей:** единственная функция — `pdf_annot *pdf_create_signature_widget(ctx, page, char *name)` (`form.h:67`; реализация `pdf-form.c:1052-1095`: `pdf_begin_operation` → `pdf_create_annot_raw(PDF_ANNOT_WIDGET)` → `/FT /Sig`, `/F Print`, `/DA "/Helv 0 Tf 0 g"`, `/T`, push в `/AcroForm/Fields` (создаёт AcroForm при отсутствии), `/Lock << /Action /All >>` → `pdf_end_operation`). В JS-документации — только `PDFPage.createSignature(name)` («only mutool run») и `createAnnotation`; `createWidget`/`createField` не документированы.

**Писать самим:** `createTextField/Checkbox/Radio/Combo` по этому образцу (`/FT /Tx|/Btn|/Ch`, `/Ff`, `/V`, `/Opt`, `/MK`, AP через `pdf_annot_request_synthesis`), Kids/Parent, FDF-экспорт/импорт (низкоуровневое чтение есть, инструментов нет — `docs/examples/import-fdf.js`). XFA не поддерживается (`pdf_was_pure_xfa` в `xref.h` — только диагностика). **Грабли:** `NeedAppearances` соблюдается с 1.27; перечисление виджетов — только через страницу.

### 1.7 JavaScript (MuJS)

`include/mupdf/pdf/javascript.h:29-41`: `pdf_enable_js`, `pdf_disable_js`, `pdf_js_supported`, `pdf_js_execute`, `pdf_js_event_init(_keystroke)`, `pdf_js_event_result(_validate/_keystroke)`, `pdf_js_event_value`. Консоль — `pdf_js_console`, `pdf_js_set_console`/`pdf_js_get_console` объявлены в `include/mupdf/pdf/document.h:82-107`. Требует `FZ_ENABLE_JS=1` (по умолчанию 1, `config.h:264-266`) и mujs (в `.sln` в libthirdparty; в vcpkg-порте **выключен**).
Реализовано (`source/pdf/pdf-js.c`): `app.alert/execMenuItem/launchURL`, `console.clear/hide/println/show`, `Doc.calculateNow/getField/mailDoc/print/resetForm`, свойства `Doc.author/creationDate/creator/keywords/modDate/numPages/producer/subject/title`, `Field.value/name/type/display/borderStyle/fillColor/textColor`, `Field.buttonSetCaption`, `util.printf`; `source/pdf/js/util.js`: `AFNumber_Format/Keystroke`, `AFDate_Format(Ex)/Keystroke(Ex)`, `AFTime_*`, `AFPercent_*`, `AFSpecial_Format/Keystroke(Ex)`, `AFRange_Validate`, `AFSimple_Calculate`, `AFMakeNumber`, `AFMakeDate`, `AFParseDateEx`, `AFMergeChange`, `AFExtractNums` и др.
**Писать самим:** `app.alert` → `QMessageBox` через `pdf_js_set_console`; всё вне этого подмножества отсутствует.

### 1.8 Вымарывание

`include/mupdf/pdf/page.h:245-300`: `pdf_redact_options {black_boxes, image_method, line_art, text}`; `int pdf_redact_page(ctx, doc, page, &opts)`. `image_method`: `PDF_REDACT_IMAGE_NONE|REMOVE|PIXELS|REMOVE_UNLESS_INVISIBLE`; `line_art`: `NONE|REMOVE_IF_COVERED|REMOVE_IF_TOUCHED`; `text`: `REMOVE|NONE|REMOVE_INVISIBLE` (1.27). JS-дефолты (`PDFPage.applyRedactions`): `REDACT_IMAGE_PIXELS`, `REDACT_LINE_ART_REMOVE_IF_COVERED`, `REDACT_TEXT_REMOVE`. **Писать самим:** UI пометки (`PDF_ANNOT_REDACT` + `pdf_set_annot_quad_points`), чистка `/Info`/XMP/вложений. (В Acrobat Redaction — только Pro.)

### 1.9 Страницы и объединение документов

`document.h`, `clean.h`, `page.h`, `font.h`: `pdf_new_graft_map` (`:603`), `pdf_graft_page`/`pdf_graft_mapped_page(ctx, map, page_to, src, page_from)` (`:640-641`; нумерация с 0, **-1 = «в конец»**), `pdf_graft_object`/`pdf_graft_mapped_object`, `pdf_delete_page` (`:725`), `pdf_delete_page_range` (`:739`), `pdf_add_page(ctx, doc, mediabox, rotate /*0,90,180,270*/, resources, contents)` (`:698`), `pdf_insert_page(ctx, doc, at /*-1 или INT_MAX — в конец*/, page)` (`:714`), `pdf_rearrange_pages(ctx, doc, count, pages, structure)` (`clean.h:77`), `pdf_page_write` (`:661`), `pdf_new_pdf_device` (`:672`), `pdf_set_page_box` (`page.h:164`), `pdf_clip_page` (`page.h:306`), `pdf_subset_fonts` (`font.h:165`), `pdf_check_document` (`:209`), `pdf_delete_page_labels` (`:757`), `pdf_set_page_tree_cache` (`page.h:41`).
Поворот: отдельной функции нет (grep `rotat` по `include/mupdf/pdf/*.h` — только `rotate` в `pdf_add_page` и `PDF_NAME(Rotate)`) — писать `/Rotate` через `pdf_dict_put_int`. Разделение — `pdf_create_document` (`:577`) + `pdf_graft_mapped_page`.
**Грабли:** `mutool merge` с 1.27 сохраняет только внешние ссылки — outline/`/Dests` собирать самим; `structure=keep` не уменьшает файл; после удаления страниц — `garbage`.

### 1.10 Сохранение, шифрование, права, оптимизация

`pdf_write_options` (`document.h:767-792`): `do_incremental, do_pretty, do_ascii, do_compress (1 zlib, 2 brotli), do_compress_images, do_compress_fonts, do_decompress, do_garbage (1 gc, 2 renumber, 3 dedupe), do_linear, do_clean, do_sanitize, do_appearance, do_encrypt, dont_regenerate_id, permissions, opwd_utf8[128], upwd_utf8[128], do_snapshot (не использовать напрямую), do_preserve_metadata, do_use_objstms, compression_effort (0 default, 1..100), do_labels, reproducible`; `pdf_default_write_options` (`:794`), `pdf_parse_write_options` (`:807`), `pdf_can_be_saved_incrementally` (`:849`), `pdf_has_unsaved_changes` (`:421`), `pdf_was_repaired` (`:426`); `pdf_save_document(ctx, doc, filename, &opts)`.
Строковые опции (reference/common/pdf-write-options): `decompress, compress[=flate|brotli], compress-fonts, compress-images, compress-effort=N, ascii, pretty, labels, linearize («no longer supported!»), clean, sanitize, garbage[=compact|deduplicate], incremental, objstms, appearance=yes|all, continue-on-error, decrypt (deprecated), encrypt=none|keep|rc4-40|rc4-128|aes-128|aes-256, permissions=N, user-password, owner-password, regenerate-id (default yes), reproducible`.
`do_encrypt`: `PDF_ENCRYPT_KEEP|NONE|RC4_40|RC4_128|AES_128|AES_256` (`crypt.h:31-36`); права `PDF_PERM_PRINT 1<<2, MODIFY 1<<3, COPY 1<<4, ANNOTATE 1<<5, FORM 1<<8, ACCESSIBILITY 1<<9, ASSEMBLE 1<<10, PRINT_HQ 1<<11` (`crypt.h:76-83`). Проверено на сборке (`mutool show -p u … trailer/Encrypt`): `/Filter /Standard /R 6 /V 5 /Length 256 /P -3904`, без пароля — «cannot authenticate password».
**Грабли:** `pdf_write_document` при `do_linear` бросает `FZ_ERROR_ARGUMENT "Linearisation is no longer supported"` (`pdf-write.c:2920-2921`) — при необходимости qpdf; `incremental` несовместим с `garbage` и со сменой шифрования (`:2918-2922`), «Can't do incremental writes on a repaired file», «…on a new document»; `decrypt` устарел; `encrypt=keep` требует пароль владельца.

Перекодирование картинок (`include/mupdf/pdf/image-rewriter.h`, дословно):
```c
enum fz_subsample_method { FZ_SUBSAMPLE_AVERAGE, FZ_SUBSAMPLE_BICUBIC };
enum fz_recompress_method { FZ_RECOMPRESS_NEVER, FZ_RECOMPRESS_SAME, FZ_RECOMPRESS_LOSSLESS, FZ_RECOMPRESS_JPEG, FZ_RECOMPRESS_J2K, FZ_RECOMPRESS_FAX };
enum fz_recompress_condition { FZ_RECOMPRESS_WHEN_SMALLER, FZ_RECOMPRESS_WHEN_ALWAYS };
typedef struct {
  int color_lossless_image_subsample_method, color_lossy_image_subsample_method;
  int color_lossless_image_subsample_threshold, color_lossless_image_subsample_to;   /* dpi: порог и целевое */
  int color_lossy_image_subsample_threshold,   color_lossy_image_subsample_to;
  int color_lossless_image_recompress_method,  color_lossy_image_recompress_method;
  char *color_lossy_image_recompress_quality, *color_lossless_image_recompress_quality;
  /* … аналогично gray_* … */
  int bitonal_image_subsample_method, bitonal_image_subsample_threshold, bitonal_image_subsample_to;
  int bitonal_image_recompress_method; char *bitonal_image_recompress_quality;
  int recompress_when;  /* 0 — только если стало меньше, 1 — всегда */
} pdf_image_rewriter_options;
void pdf_rewrite_images(fz_context *ctx, pdf_document *doc, pdf_image_rewriter_options *opts);
/* pdf/clean.h */
typedef struct { pdf_write_options write; pdf_image_rewriter_options image; int subset_fonts;
                 pdf_clean_options_structure structure /*DROP=0|KEEP=1*/; pdf_clean_options_vectorize vectorize /*NO=0|YES=1*/; fz_archive *dir; } pdf_clean_options;
void pdf_clean_file(fz_context *ctx, char *infile, char *outfile, char *password, pdf_clean_options *opts, int retainlen, char *retainlist[]);
```
Docs `mutool clean` перечисляют метод `jbig2`, но `FZ_RECOMPRESS_JBIG2` в enum на master нет — расхождение docs/кода. CLI-эквивалент «Уменьшить размер»:
```bat
mutool clean -ggg -z -f -i -Z --color-lossy-image-subsample-dpi=150 --color-lossy-image-recompress-method=jpeg --gray-lossy-image-subsample-dpi=150 --bitonal-image-recompress-method=fax --recompress-images-when=smaller in.pdf out.pdf
```
(`-g/-gg/-ggg/-gggg` — удалить неиспользуемые / сжать xref / слить дубликаты объектов / проверить дубликаты потоков; `-z` deflate; `-e` effort; `-f` шрифты; `-i` картинки; `-Z` object streams + xref streams; `-S` субсеттинг шрифтов (EXPERIMENTAL); `-A`/`-AA` внешний вид аннотаций; `-D` снять шифрование; `-E rc4-40|rc4-128|aes-128|aes-256 -O owner -U user -P perm` шифрование; `-m` metadata; `-v` всё в вектор; `--structure=keep|drop`; `-s` санитизация; `-d` распаковка; `-l` — «no longer supported»; диапазоны `1,5,10-15,20-N`.) Отчёт о распределении места: `mutool audit -o report.html file.pdf` (storage / object usage / operator frequency).

Стратегия «Уменьшить размер файла»: `pdf_rewrite_images` (150 dpi / JPEG 75 «экран», 300 dpi / JPEG 85 «печать») → `pdf_save_document` с `garbage=deduplicate,compress,compress-fonts,compress-images,objstms` (+ `pdf_clean_options.subset_fonts=1` через `pdf_clean_file`) → опционально qpdf `--linearize`. **Грабли:** `pdf_rewrite_images` меняет только XObject-изображения; `garbage=deduplicate` может слить одинаковые по байтам, но разные по смыслу объекты.

### 1.11 Редактирование содержимого (текст)

Готового API «изменить текст» **нет**. Есть (`include/mupdf/pdf/interpret.h`): `pdf_new_run_processor` (`:243`), `pdf_new_buffer_processor(ctx, buffer, ahxencode, newlines)` (`:259`), `pdf_new_output_processor` (`:283`), `pdf_filter_options` (`:336`), `pdf_filter_factory_fn` (`:298`), `pdf_sanitize_filter_options` с колбэками `image_filter(ctx, opaque, ctm, name, image, scissor)`, `text_filter(ctx, opaque, ucsbuf, ucslen, trm, ctm, bbox, tr, ca, CA)`, `culler(ctx, opaque, bbox, type)` (`:382-387`), `pdf_new_sanitize_filter` (`:413`), `pdf_new_vectorize_filter` (`:422`), `pdf_new_color_filter` (`:475`), `pdf_filter_xobject_instance` (`:424`), `pdf_processor_push/pop_resources` (`:426,428`); `pdf_filter_page_contents(ctx, doc, page, &options)` (`page.h:235`); генерация `pdf_page_write`/`pdf_new_pdf_device`, `pdf_update_stream(ctx, doc, ref, buf, compressed)` (`xref.h:50`), `pdf_add_stream` (`xref.h:60`), `pdf_add_simple_font`/`pdf_add_cid_font` (`font.h:132,142`), `fz_new_font_from_file`/`fz_new_base14_font` (`fitz/font.h`), `pdf_subset_fonts`.
**Писать самим (самый трудоёмкий блок):** модель текстовых объектов (`BT…ET`, `Tm/Td/TL`, шрифт→Unicode), WYSIWYG-правка, перегенерация фрагмента через `pdf_new_buffer_processor` с подменой `Tj/TJ`, подмена шрифтов для отсутствующих глифов (`pdf_add_cid_font`), перераскладка.

### 1.12 Картинки

`fz_new_image_from_file/_from_buffer/_from_pixmap` (`fitz/image.h`); `pdf_obj *pdf_add_image(ctx, doc, fz_image *image)` (**`resource.h:131`** — файла `pdf/image.h` нет), `pdf_find_image_resource`/`pdf_insert_image_resource` (`:86-87`), `fz_image *pdf_load_image(ctx, doc, obj)` (`:129`); `pdf_update_stream` для замены данных XObject; экспорт `fz_save_pixmap_as_png(ctx, pix, filename)` / `fz_save_pixmap_as_jpeg(ctx, pix, filename, int quality)` / `fz_save_pixmap_as_jpx(…, q)` (`write-pixmap.h`), `fz_get_pixmap_from_image` (`image.h`). Замена картинки = подмена записи в `/Resources/XObject`; вставка = `pdf_add_image` + `pdf_add_page`/`pdf_insert_page` или `q w 0 0 h x y cm /ImN Do Q` в поток (DCT-поток вставлять как есть, без перекодирования). MuPDF открывает JPEG, PNG, TIFF (многостраничный — с 1.4) как документы: `mutool convert -o out.pdf -O compress a.jpg b.png scan.tif`; API `fz_new_document_writer(ctx, "out.pdf", "pdf", "compress,compress-images")` → для каждого изображения `fz_open_document` → `fz_begin_page` → `fz_run_page` → `fz_end_page`. PDF → PNG: `mutool draw -r 150 -o page%d.png file.pdf` (растровые форматы docs: `pbm pgm ppm pam png pwg pcl ps`, векторные `svg pdf`; в суффиксной таблице master ещё `pkm`, `pclm`, `psd`, `j2k`; **`.jpg/.jpeg` в CLI не документированы**, хотя CHANGES 1.22 говорит «Added JPEG output format» — JPEG брать через API). **TIFF на запись MuPDF не умеет** (§5.7 — libtiff); MuPDF грузит TIFF целиком.

### 1.13 OCR — API (подробности, tessdata и препроцессинг — §5.3)

```c
/* include/mupdf/fitz/structured-text.h:997-1000 */
typedef int (fz_ocr_progress_fn)(fz_context *ctx, void *arg, int progress);
fz_device *fz_new_ocr_device(ctx, target, ctm, mediabox, int with_list, const char *language, const char *datadir, fz_ocr_progress_fn *progress, void *progress_arg);
fz_device *fz_new_ocr_device_with_options(…, fz_ocr_progress_fn *progress, void *progress_arg, fz_options *options);
/* with_list=0: графика идёт в target сразу, распознанный текст добавляется при close (Courier поверх контента);
   with_list=1: всё пишется в display list, при close проигрывается через rewrite-device, который подставляет
   OCR-символы в существующие текстовые объекты, остаток — невидимым текстом. language NULL → "eng"; datadir NULL → "". */
/* include/mupdf/fitz/writer.h:149-151 */
fz_document_writer *fz_new_pdfocr_writer(ctx, const char *path /*NULL → out.pdfocr*/, const char *options);
fz_document_writer *fz_new_pdfocr_writer_with_output(ctx, fz_output *out, const char *options);
typedef int (fz_pdfocr_progress_fn)(fz_context *ctx, void *progress_arg, int page, int percent); /* 0 — продолжать, 1 — отмена */
void fz_pdfocr_writer_set_progress(ctx, fz_document_writer *w, fz_pdfocr_progress_fn *p, void *arg);
void fz_save_pixmap_as_pdfocr(...);                                                               /* write-pixmap.h:235 */
```
Опции pdfocr (`source/fitz/output-pdfocr.c:39-48`): `compression=none|flate` (умолч. none), `strip-height=N`, `ocr-language=<lang>` (умолч. eng), `ocr-datadir=<dir>` (иначе `TESSDATA_PREFIX`), `skew=none|auto|<angle>`, `skew-border=increase|maintain|decrease` (умолч. increase); плюс raster-опции (`resolution=…`, `colorspace=…`). Язык по умолчанию `"eng"` — `tessocr.cpp:133`; `tessocr.cpp` вызывает `api->Init(datadir, 0, language, OEM_DEFAULT, …, &tess_file_reader)`, файлы данных читает собственным ридером через **`fopen(filename, "rb")`**. Без Tesseract (`OCR_DISABLED`) `fz_new_ocr_device_with_options` бросает `FZ_ERROR_UNSUPPORTED` «OCR Disabled in this build». Невидимый слой — шрифт `GlyphLessFont` (`output-pdfocr.c:51-57`). Семантика возврата колбэка OCR-устройства в typedef не задокументирована. Deskew без Leptonica: `include/mupdf/fitz/deskew.h` — `enum fz_deskew_border_behavior { FZ_DESKEW_BORDER_INCREASE = 0, MAINTAIN = 1, DECREASE = 2 }`, `fz_pixmap *fz_deskew_pixmap(ctx, src, double degrees, int border)`, `double fz_detect_skew(ctx, pixmap)`; поиск листа — `pixmap.h`: `int fz_detect_document(ctx, fz_quad *points, fz_pixmap *src)` (вход gray/rgb без alpha; 0 — не найден), `fz_warp_pixmap(ctx, src, points, width, height)`, `fz_autowarp_pixmap(ctx, src, points)`.
`mutool draw`: `-t <lang>` (умолч. eng), `-d <dir>` (иначе `TESSDATA_PREFIX`), `-k auto` или `-k 0|1|2[,angle]`, `-r` dpi (умолч. 72), `-O` device-опции; суффиксы `.ocr.txt`, `.ocr.text`, `.ocr.html`, `.ocr.xhtml`, `.ocr.stext`, `.ocr.stext.json`, `.ocr.pdf`, `.ocr.trace`. Пример: `mutool draw -t rus+eng -d tessdata -r 300 -o out.ocr.pdf scan.pdf`. GNU make: `USE_TESSERACT=yes` (не `tesseract=yes`). Живой OCR-тест не выполнен (tessdata не скачивались).

### 1.14 Входные форматы и Office

`source/fitz/document-all.c:40-79`: PDF, XPS, SVG, CBZ, изображения, FB2, HTML/XHTML (+ **Markdown — только при `FZ_ENABLE_HTML && FZ_ENABLE_MD`**, `:60-66`), MOBI, TXT, Office, EPUB, gzip-обёртка; всё отключается `FZ_ENABLE_*=0` (`config.h`). Office-обработчик (`source/html/office.c`, 1356 строк) принимает `docx, xlsx, pptx, hwpx` и MIME (`:1269-1290`, вкл. `application/haansofthwpx`), «только 75 % уверенности», чтобы коммерческий SODoc-обработчик мог перекрыть; обрабатывает абзацы, таблицы, сноски; обработки изображений нет (grep `img|image|blip` пуст).
**Проверено вживую (2026-10-21):** docx с заголовком, абзацами, PNG (`a:blip`) и таблицей 2×2 → `mutool convert`: текст и ячейки таблицы извлечены, заголовок шрифтом Charis SIL Bold, **изображений 0** (`mutool show … grep /Image` пуст). Файлы `verify-out\проба.docx`, `проба-из-docx.pdf`. «Качественный» Office — платный SmartOffice (`HAVE_SMARTOFFICE` только в `*Commercial`, `sodochandler.vcxproj` ссылается на отсутствующий `thirdparty\so\`). xlsx/pptx вживую не проверялись. PDF→DOCX/ODT через `extract` (`fz_new_docx_writer`/`fz_new_odt_writer`, `FZ_ENABLE_DOCX_OUTPUT` = 1 по умолчанию): на двухстрочном кириллическом PDF текст в `word/document.xml` сохранён (`smoke-тест\из-pdf.docx`); качество на сложных документах не проверялось.

### 1.15 Вывод (writers)

`fz_new_document_writer(ctx, path, format, options)` (`source/fitz/writer.c`): `ocr, pdf, cbz, csv, svg, png, pam, pnm, pgm, ppm, pbm, pkm, jpeg/jpg, pcl, pclm, ps, pwg, txt/text, html, xhtml, stext/stext.xml, odt, docx`. Отдельно `fz_new_pdf_writer`, `fz_new_svg_writer`, `fz_new_pcl_writer`, `fz_new_pdfocr_writer`, `fz_new_docx_writer`, `fz_new_odt_writer` (`writer.h`). «Что угодно → PDF» = `fz_run_page` в `fz_begin_page`. Опции: reference/common/document-writer-options.

### 1.16 Undo/redo, журнал

`object.h:395-436`: `pdf_enable_journal`, `pdf_begin_operation(ctx, doc, "name")`, `pdf_end_operation`, `pdf_abandon_operation`, `pdf_undoredo_state`, `pdf_undoredo_step`, `pdf_can_undo/redo`, `pdf_undo/redo`; `pdf_save_journal`/`pdf_load_journal` (`document.h:859,865`); `pdf_count_versions`, `pdf_validate_change_history` (`xref.h:250,267`). Встроенные правки (например, `pdf_create_signature_widget`) уже обёрнуты в операции. **Грабли:** баги кэша страниц при undo исправлялись в 1.25 и 1.26 — держать `pdf_page` недолго.

### 1.17 Ошибки, устойчивость (`fz_try/fz_catch`)

- `fz_try(ctx) { … } fz_always(ctx) { … } fz_catch(ctx) { … }`. «You should **never** return from within a `fz_try` or `fz_always` block!» (разбалансирует стек исключений); выйти досрочно — `break`. Локальные переменные, меняемые в try, — `fz_var(x)`. «Do not call anything in the `fz_always()` section that can throw.» `fz_caught()` «assumes no intervening use of fz_try/fz_catch». `fz_try` = `setjmp/longjmp` — C++-деструкторы внутри при longjmp не выполняются.
- Отчёт об ошибках (с 1.24): «You must call `pdf_report_error` in the final `fz_catch`. Any unreported errors will be automatically reported when a new error is raised, or when closing the fitz context»; `void fz_report_error(fz_context*)`, `fz_ignore_error()` — только для TRYLATER/ABORT.
- Коды: `FZ_ERROR_SYSTEM` (fatal OOM/syscall), `FZ_ERROR_LIBRARY`, `FZ_ERROR_ARGUMENT`, `FZ_ERROR_LIMIT`, `FZ_ERROR_UNSUPPORTED`, `FZ_ERROR_FORMAT` (unrecoverable), `FZ_ERROR_SYNTAX` (diagnose and ignore); внутренние `TRYLATER`, `ABORT`, `REPAIRED`. `fz_caught`, `fz_caught_message`, `fz_caught_errno` (только для SYSTEM). Колбэки `fz_set_error_callback`/`fz_set_warning_callback`: «The callback must not throw exceptions!». Вызов вне `fz_try` при ошибке → библиотека вызывает `exit()`.
- **Писать самим:** обёртка `BnyMu::call(ctx, [&]{ … })` вокруг `fz_try`, запрет `return` внутри (grep/clang-tidy), гарантированный `fz_report_error`; `fz_new_context(NULL, NULL, FZ_STORE_DEFAULT)` и `-rss_limit_mb` для OOM.

### 1.18 Сборка под MSVC 2022 (проверено дважды на этой машине)

| Способ | Статус | Плюсы/минусы |
|---|---|---|
| `platform/win32/mupdf.sln` (29 проектов) | Официальный (guide/install: «For Windows use the Visual Studio solution file»; `README.txt` там — «This MSVC project needs the thirdparty sources to be in place»); конфигурации `Debug/Release/Memento` × `{"", Extra, Commercial, TofuCJKExt}` × `Win32/x64/ARM64`; `ToolsVersion="15.0"`, `PlatformToolset v142` (349 вхождений), `WindowsTargetPlatformVersion 10.0`, `/MD` (`MultiThreadedDLL`), `CharacterSet MultiByte`, `WholeProgramOptimization true` в 9 Release-группах, все библиотеки `StaticLibrary`, `OutDir $(Platform)\$(Configuration)\`, `TargetName` не задан | Работает с Build Tools 2022 через `/p:PlatformToolset=v143`; `Release` включает Tesseract/Leptonica/extract/zxing/HarfBuzz/mujs; **OpenSSL нет** |
| Makefile | Linux/BSD/macOS; `HAVE_LIBCRYPTO` через `pkg-config libcrypto >= 1.1.0` (`Makerules:238`), `USE_TESSERACT=yes`, `barcode=yes`; MinGW/iOS удалены в 1.26 | На Windows только MSYS2/WSL — не для очереди |
| Официальный CMake | **Нет** (`CMakeLists.txt` в корне отсутствует; `scripts/` — ~70 вспомогательных файлов: `bin2coff.c`, `fontdump.nmake`, `libresources.vcxproj.template`, `mupdfwrap.py`, cmap-утилиты…) | — |
| vcpkg `libmupdf` | Порт **1.26.10** (правка 2025-10-13, `license AGPL-3.0-only`); `vcpkg_check_linkage(ONLY_STATIC_LIBRARY) # incomplete DLL exports`; feature `ocr`; `FZ_ENABLE_JS=0, FZ_ENABLE_ICC=0, FZ_ENABLE_BROTLI=0, FZ_ENABLE_DOCX_OUTPUT=0, FZ_ENABLE_ODT_OUTPUT=0`; зависимости freetype, gumbo, harfbuzz, jbig2dec, libjpeg-turbo, openjpeg, zlib; шрифты через `bin2coff` (порт качает `scripts/bin2coff.c` коммита `9c1af80` ради ARM64); таргет `unofficial::libmupdf::libmupdf`; `source/helpers/pkcs7/*` не компилируется, заголовки `include/mupdf/**` устанавливаются | Отстаёт на две ветки, без JS; удобен как шаблон своего CMake; апгрейд = overlay-port |
| DLL | `mupdfcpp.vcxproj` — `DynamicLibrary` (x64 → `mupdfcpp64.dll` через `windows_mupdf.def`) — C++-обёртка; проектов DLL для C-API нет | Для Qt-ядра — статические `.lib` |

Проверенная последовательность:
```bat
git clone --depth 1 --branch 1.28.5 https://github.com/ArtifexSoftware/mupdf.git
cd mupdf
git submodule update --init --depth 1
rem путь к MSBuild: vswhere -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe
"%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" ^
  platform\win32\mupdf.sln /t:mutool /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 /m /v:m
rem быстрый цикл: /p:Configuration=Debug ; целевой набор библиотек: /t:libmupdf;libthirdparty;libtesseract;libleptonica;mutool
```
Измерено (i7-13650HX, `/m`; `build-log\steps.txt`, `steps-debug.txt`):
- рабочее дерево без `thirdparty` и `.git` — **85 МиБ**; `.git` (с модулями) — 158 МиБ; submodules (shallow) — **136,8 с**, 168 МиБ (leptonica 27, curl 24, zint 23, harfbuzz 15, lcms2 13, freetype 13, gumbo 12, tesseract 10);
- **Release x64 `mutool`: 562,6 с, rc=0, 0 ошибок, 23 предупреждения** (C4127×7, C4267×4, C4146×4, C4702×3…); каталог `x64\Release` 2469 МиБ; `libmupdf.lib` 741 МБ (/GL), `libharfbuzz.lib` 321, `libtesseract.lib` 182, `libzxing.lib` 94, `libresources.lib` 46, `libthirdparty.lib` 30, `libleptonica.lib` 21, `libmutool.lib` 3, `libextract.lib` 1,8, `libmubarcode.lib` 0,9, `libpkcs7.lib` 0,1 (заглушка), `libmuthreads.lib` ~0; `mutool.exe` 44,8 МБ;
- **Debug x64 `mutool`: 169,9 с, rc=0, 0 ошибок, 22 предупреждения**; каталог 1415 МиБ; `libmupdf.lib` 425 МБ, `mutool.exe` 73,1 МБ. Debug в 3,3 раза быстрее из-за отсутствия `/GL` — использовать для быстрого цикла очереди.
Линковать в Qt-приложение (по `ProjectReference`: mutool → libmupdf, libmutool, libthirdparty, libresources, libharfbuzz, libtesseract, libextract, libmubarcode, libpkcs7, libmuthreads; транзитивно libtesseract → libleptonica, libmubarcode → libzxing): `libmupdf.lib libthirdparty.lib libresources.lib libharfbuzz.lib libtesseract.lib libleptonica.lib libextract.lib libmubarcode.lib libzxing.lib libpkcs7.lib` (+ `libmuthreads.lib`); include `mupdf\include`; runtime `/MD` как у Qt; потребитель `/GL`-библиотек должен линковаться тем же тулсетом (LTCG, медленная финальная линковка); `sodochandler` и `mupdf-gl` (freeglut/OpenGL) не собирать.
```cmake
target_include_directories(pdfcore PRIVATE ${MUPDF_ROOT}/include)
target_link_libraries(pdfcore PRIVATE libmupdf.lib libthirdparty.lib libresources.lib libmuthreads.lib libharfbuzz.lib
                                      libtesseract.lib libleptonica.lib libextract.lib libmubarcode.lib libzxing.lib libpkcs7.lib)
```

Дымовой тест `mutool.exe` (без экрана, кириллические пути): `convert` txt→PDF с русским текстом (`draw -F txt` вернул «Привет, мир! Это проверка кириллицы в MuPDF 1.28.5.»), png→PDF, `merge`, `draw` в PNG, AES-256 — всё успешно.

**Thirdparty** (readthedocs other/third-party; совпадает с `.gitmodules` — 18 сабмодулей): обязательные FreeType 2.14.3, HarfBuzz 6.0.0, libjpeg 10.0, lcms2 2.16 (несовместимый форк, `HAVE_LCMS2MT`), OpenJPEG 2.5.4, zlib 1.3.2, gumbo-parser 0.10.1, Brotli 1.1.0, cmark-gfm 0.29.0.gfm.13; jbig2dec и MuJS — Artifex. Опциональные: FreeGLUT 3.0.0, curl 7.66.0, JPEG-XR 1.32, Tesseract 5.5.2, Leptonica 1.87.0 (ветка `artifex`), Zint 2.13.0.9, Zxing-cpp 2.3.0; submodule `extract`. **OpenSSL и libarchive не входят.**

**Грабли Windows:**
- `v142` во всех 349 местах — с одними Build Tools 2022 обязателен `/p:PlatformToolset=v143`.
- `*Extra` (OpenSSL + libarchive) не соберутся без ручного `thirdparty\openssl` (`include`, `x64\lib`, PostBuild копирует `libcrypto-1_1-x64.dll` — для OpenSSL 3 править на `libcrypto-3-x64.dll`, `libpkcs7.vcxproj:682-685`) и `thirdparty\libarchive`; MSVC-сборка `ReleaseExtra` не выполнялась.
- UTF-8-пути: `fz_open_document(ctx, utf8)` → `fz_fopen_utf8` (`source/fitz/time.c:118`, `_wfopen`; объявлена в `system.h:212`); **`fz_open_file_w(ctx, const wchar_t*)` публична** (`stream.h:81`); `fz_wchar_from_utf8`/`fz_utf8_from_wchar` (`string-util.h:314-319`); `fz_argv_from_wargv` (`system.h:215`); `mutool` — `wmain`; `PATH_MAX` 4096 (`system.h:80`), `fz_realpath` (`string-util.h:199`). В Qt — `QString::toUtf8()`; надёжнее `fz_open_document_with_stream(fz_open_file_w(ctx, wchar_path))`.
- Шрифты вшиваются `bin2coff.exe` (`libresources`, шаблон `scripts/libresources.vcxproj.template`).
- `/MD` и `MultiByte` не менять. AVX2/AVX/SSE4.1/FMA зашиты в `libtesseract.vcxproj` (x64).
- Огромные `.lib` — следствие `/GL`; каждая линковка exe тянет их целиком; свой `pdfcore.dll` с C-API имеет смысл только ради инкрементальной линковки GUI.

## 2. Qt 6 для настольного PDF-редактора на Windows

Метод исходной проверки: прямые запросы к doc.qt.io, download.qt.io, qt.io/blog, Jira Qt (REST), исходникам qtbase/vcpkg/aqtinstall, PyPI; локально — установленный kit Qt 6.12.0 в scratchpad, venv aqt, winget, реестр. wiki.qt.io отвечала 503 весь день (плановые даты патчей не проверены).

### 2.1 Версии и поддержка

| Версия | Дата | Примечание | Источник |
|---|---|---|---|
| **6.12.0 LTS** | 30.09.2026 | «Qt 6.12 is a Long-Term Support (Qt LTS) release, meaning it gets maintenance over a 5-year period»; «new Qt LTS release every 2 years»; каталог `official_releases/qt/6.12/6.12.0/` датирован 30-Sep-2026; online-репозиторий `qt6_6120_msvc2022_64` — пакеты `6.12.0-0-202609280346` (ReleaseDate 2026-09-28) без beta/rc | qt.io/blog/qt-6.12-released; download.qt.io |
| 6.12.1 / 6.12.2 / 6.12.3 | план 12.11.2026 / 14.01.2027 / 04.03.2027 — **не проверено** | wiki.qt.io недоступна | — |
| 6.11.0 / 6.11.1 / 6.11.2 | 23.03.2026 / 13.05.2026 / 18.08.2026 | каталоги созданы 20.03 / 12.05 / 18.08; 6.11.3 есть только в online-репозитории (`qt6_6113`, `6.11.3-0-202609240253`, 2026-09-24), в `official_releases/qt/6.11/` её нет — причина не выяснена | qt.io/blog; download.qt.io |
| 6.10.0 … 6.10.3 | 06.10.2025 / 20.11.2025 / 28.01.2026 / 02.04.2026 | последняя 6.10.x; ставится релизным aqt 3.3.0 | download.qt.io/official_releases/qt/6.10/ |
| 6.9.x, 6.8.x | — | в `official_releases/qt/` только 6.10, 6.11, 6.12; 6.8.x и 6.5.x — в `archive/qt/` | — |

Поддерживаемые конфигурации 6.12 на Windows (doc.qt.io/qt-6/windows.html): Windows 10 (1809+) x86_64 и Windows 11 x86_64 — MSVC 2022 или MinGW-w64 15.1; Windows on ARM ARM64 — MSVC 2022; ARM64EC — MSVC 2026, без пакетов в инсталляторе; MSVC 2019 не упоминается; «Qt 6.12 will be the last version to support Windows 10». Prebuilt 6.12.0 собран `msvc 19.44.35227.0` (`config_qtbase.summary`) — совпадает с MSVC 14.44 на машине.

**LTS и open source.** Политика Qt Company (qt.io/blog/qt-offering-changes-2020): «Starting with Qt 5.15, long term support (LTS) will only be available to commercial customers», open-source «will receive patch-level releases … until the next minor release». Практика (`archive/qt/`): 6.8.0–6.8.3 открыто 07.10.2024–26.03.2025, затем 6.8.4 — 09.07.2026, 6.8.5 — 16.10.2026; 6.5.0–6.5.3 (04–09.2023), затем 6.5.4 — 01.2025 … 6.5.10 — 09.2026. Ожидание для 6.12: открытые патчи до 6.13 (≈ весна 2027), дальше с задержкой ~1 год; прямого заявления про 6.12 нет.

Что нового, важное для проекта:
- 6.12: `QPdfDocument::render()` получил параметры `format = QImage::Format_ARGB32`, `fillColor = Qt::transparent`; `QPdfView` — ctrl+колесо зум от курсора; `windeployqt --appx`, `--appx-certificate`; фича `static_compiler_runtime` (Hybrid CRT, только при сборке Qt); манифест `qt_add_executable()` «declares Windows 10/11 compatibility, enables long-path awareness, and configures the UAC execution level»; DirectWrite читает GASP; `QWizard` без Aero в тёмном режиме; `QOperatingSystemVersion::Windows11_25H2`.
- 6.11: «On Windows, Qt now automatically generates an application manifest for executables built with CMake»; `QT_WINDOWS_APP_PROJECT_IDENTIFIER` / `QT_WINDOWS_APP_PROJECT_EXECUTION_LEVEL` (asInvoker|highestAvailable|requireAdministrator) — «introduced in Qt 6.11.1»; при наличии своего `.manifest` в sources автогенерация отключается; `QTest::defaultTryTimeout`; `QTest::qWaitForWindowExposed` — с 6.10 перегрузки с `QDeadlineTimer`.
- 6.8: «Changed the font system to use DirectWrite as the default backend» (старый — `-platform windows:fontengine=gdi`); `QStyleHints::setColorScheme()` / `unsetColorScheme()`.
- 6.7: «On Systems running Windows 11 and above the new Windows 11 widget style is used by default. This style supports Windows dark mode»; `qt_add_translations()` вышла из technology preview; `qt_standard_project_setup(I18N_TRANSLATED_LANGUAGES …)`.
- 6.5: `-platform windows:darkmode` — «As of Qt 6.5, the default value is 2»; `QStyleHints::colorScheme`; `QT_WIN_DEBUG_CONSOLE`.

**Рекомендация:** Qt **6.12.0** (LTS). Запасной вариант, если master-aqt сломается: 6.10.3 через релизный aqt 3.3.0.

### 2.2 Установка без аккаунта и без GUI (aqtinstall)

Факты:
- aqtinstall 3.3.0 — последний релиз на PyPI (3.3.0 — 02.06.2025, 3.2.1 — 16.03.2025, 3.2.0 — 28.01.2025); MIT; `requires_python >= 3.9`; зависимости `bs4, defusedxml, humanize, patch-ng, py7zr>=0.22.0, requests>=2.31.0, semantic-version, texttable`; `py7zr` последний — 1.1.4 (`>=3.10`). Standalone `aqt.exe` 15 185 398 байт в GitHub Release v3.3.0.
- Имена архитектур (`aqt list-qt windows desktop --arch 6.12.0`): `win64_mingw win64_msvc2022_arm64_cross_compiled win64_msvc2022_64 win64_llvm_mingw`.
- Модули 6.12.0 msvc2022_64 (`--long-modules`): `debug_info, qt3d, qt5compat, qtactiveqt, qtcanvaspainter, qtcharts, qtconnectivity, qtdatavis3d, qtgraphs, qtgrpc, qthttpserver, qtimageformats, qtlanguageserver, qtlocation, qtlottie, qtmultimedia, qtnetworkauth, qtopenapi, qtpositioning, qtquick3d, qtquick3dphysics, qtquickeffectmaker, qtquicktimeline, qtremoteobjects, qtscxml, qtsensors, qtserialbus, qtserialport, qtshadertools, qtspeech, qttasktree, qtvirtualkeyboard, qtwebchannel, qtwebsockets, qtwebview` + расширения `qtpdf`, `qtwebengine`.
- Widgets, PrintSupport, Svg, Concurrent, Test, Network, LinguistTools — в базовом пакете (архивы `qtbase, qtsvg, qtdeclarative, qtdoc, qttools, qttranslations, d3dcompiler_47-x64, opengl32sw-64-mesa_11_2_2`); `lupdate/lrelease/windeployqt/designer/linguist/qtdiag` — из `qttools`.
- Размеры (Updates.xml): база 278 542 090 → 2 592 069 328 Б (265,6 → 2 472 МБ); `debug_info` 574 МБ → 4,0 ГБ (не нужен); `qtimageformats` 1,0 → 6,2 МБ; `qtpdf` (61400) 5 566 688 → **28 509 721 Б** (5,3 → 27,2 МБ); `qtwebengine` ≈ 240 МБ → 1,2 ГБ (не нужен). **Замер:** `install-qt … 6.12.0 win64_msvc2022_64 -m qtimageformats` — 194–196 с, 2 478 МБ на диске.

Грабли (все воспроизведены):
1. **Python 3.15** (системный 3.15.0b3; финальный 3.15.0 вышел 09.10.2026, PEP 790): у `pyppmd 1.3.1`, `pybcj 1.0.8`, `inflate64 1.0.4`, `brotli 1.2.0` нет wheel `cp315` (есть `cp314-win_amd64`; abi3 — только у `pycryptodomex 3.24.1` и `psutil 7.2.2`) → pip собирает MSVC-расширения из исходников (не завершилось за 25+ мин). Решение — `py -3.14` (3.14.6; также есть `py -3.11`) или `winget install --id Python.Python.3.13 -e --scope user --silent` (3.13.15, burn, per-user: python.org «You will not need to be an administrator», `InstallAllUsers=0` по умолчанию, `%LocalAppData%\Programs\Python\Python313`).
2. **aqt 3.3.0 не умеет Qt ≥ 6.11**: сменилась раскладка репозитория (`qt6_6120/qt6_6120_msvc2022_64/Updates.xml` вместо `qt6_6103/qt6_6103/`) → «Failed to download checksum for the file 'Updates.xml'». В master (CHANGELOG Unreleased: «Support Qt 6.11+ for Windows X64 (#1000)», локально v3.3.1.dev166) исправлено — проверено для 6.12.0. Когда выйдет релиз — неизвестно.
3. **Расширения 6.12 версионируются отдельно.** QTBUG-149519 (26.08.2026, Closed/Done): «Release Qt Webengine and Qt PDF as own product, not part of Qt Framework release» — версии 6.<Chromium>.<patch>, первая 6.140.0; QTBUG-149634 (Closed/Duplicate) — новая структура Extensions с beta 4. На сервере `extensions/qtpdf/6120/` есть старые папки (`msvc2022_64/`, 13-Jul-2026, beta2) и правильная `61400/` (03-Sep-2026, `extensions.qtpdf.6120.61400.win64_msvc2022_64`, `6.12.0-0-202609281005`). aqt (в т.ч. dev166) берёт старую → «procedure entry point not found» (issue #1047, 12.09.2026; PR #1048 «Qt 6.12 compatibility» open, мейнтейнер 19.09 обещал «merge when linger satisfied»).
4. Относительные `--outputdir` и пути с пробелами — избегать (MAX_PATH 260).
5. Вывод `qDebug/qInfo` из конвейера уходит в `OutputDebugString`: `win_message_handler` пишет в stderr только при `QT_FORCE_STDERR_LOGGING` **или** `stderrHasConsoleAttached()` (= `GetConsoleWindow()`, переопределяется `QT_ASSUME_STDERR_HAS_CONSOLE=1`); `QT_FORCE_STDERR_LOGGING` на doc.qt.io не документирована (только `qlogging.cpp`), `QT_WIN_DEBUG_CONSOLE=new|attach` — в debug.html (6.5).

Точные команды (рекомендуемый путь):
```powershell
# 1) venv на стабильном Python
py -3.14 -m venv C:\Tools\aqt
C:\Tools\aqt\Scripts\python.exe -m pip install --disable-pip-version-check "git+https://github.com/miurahr/aqtinstall.git"
C:\Tools\aqt\Scripts\python.exe -m aqt version        # ожидается aqtinstall v3.3.1.devNNN

# 2) проверка имён (без аккаунта)
C:\Tools\aqt\Scripts\python.exe -m aqt list-qt windows desktop --arch 6.12.0
C:\Tools\aqt\Scripts\python.exe -m aqt list-qt windows desktop --long-modules 6.12.0 win64_msvc2022_64

# 3) установка (≈ 3–4 мин, 266 МБ скачать, 2,5 ГБ на диске)
C:\Tools\aqt\Scripts\python.exe -m aqt install-qt windows desktop 6.12.0 win64_msvc2022_64 -m qtimageformats --outputdir C:\Qt
# результат: C:\Qt\6.12.0\msvc2022_64\{bin,lib,plugins,include,lib\cmake}; минимум: --archives qtbase qtsvg qttools

# 4) VC++ redist из того же репо (id проверен: qt.tools.vcredist_msvc2022_x64, версия 2025-11-26)
C:\Tools\aqt\Scripts\python.exe -m aqt install-tool windows desktop tools_vcredist qt.tools.vcredist_msvc2022_x64 --outputdir C:\Qt
# OpenSSL из репо НЕ брать: qt.tools.opensslv3.win_x64 = OpenSSL 3.0.16-1 (MSVC 2019, 2025-02-13) — ветка 3.0 EOL
```
Запасной путь через релизный aqt (только ≤ 6.10.3): `pip install aqtinstall==3.3.0` → `aqt install-qt windows desktop 6.10.3 win64_msvc2022_64 -m qtimageformats qtpdf --outputdir C:\Qt`.
Ручной fallback без aqt (архивы содержат префикс `6.12.0/msvc2022_64/`; SHA1 — в `Updates.xml` того же каталога; распаковка — 7-Zip или `py7zr` из venv aqt):
```powershell
$b='https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_6120/qt6_6120_msvc2022_64/qt.qt6.6120.win64_msvc2022_64/6.12.0-0-202609280346'
foreach ($a in 'qtbase','qtsvg','qttools','qttranslations','qtdeclarative','qtdoc') { curl.exe -L -o "$a.7z" "$b$a-Windows-Windows_11_24H2-MSVC2022-Windows-Windows_11_24H2-X86_64.7z"; & 'C:\Program Files\7-Zip\7z.exe' x -oC:\Qt "$a.7z" }
# QtPdf, правильная сборка для 6.12.0 (6.140.0), если всё же понадобится:
curl.exe -L -o qtpdf.7z 'https://download.qt.io/online/qtsdkrepository/windows_x86/extensions/qtpdf/6120/61400/msvc2022_64/extensions.qtpdf.6120.61400.win64_msvc2022_64/6.12.0-0-202609281005qtpdf-Windows-Windows_11_24H2-MSVC2022-Windows-Windows_11_24H2-X86_64.7z'
```
Установленный kit на машине: `…\scratchpad\Qt\6.12.0\msvc2022_64` (`config_qtbase.summary`: `Compiler: msvc 19.44.35227.0`, `OpenSSL 3.0 yes / directly linked no`, `Schannel yes`, `DirectWrite yes`; `plugins/platforms` = qdirect2d, qminimal, qoffscreen, qwindows (+d); каталогов `plugins/printsupport` и `lib/fonts` нет; в `bin` lupdate/lrelease/windeployqt/qtdiag/designer/linguist; 2 478 МБ).

**vcpkg как альтернатива:** порт `qtbase` — 6.11.2; default-features на Windows `async-io, brotli, concurrent, dnslookup, doubleconversion, freetype, gui, harfbuzz, icu, jpeg, network, opengl, openssl, pcre2, png, sessionmanager, sql, sql-psql, sql-sqlite, testlib, thread, widgets, windeployqt, zstd` — всё из исходников (ICU, HarfBuzz, FreeType, OpenSSL, PCRE2, libpq); **порта `qtpdf` нет** (QtPdf — фича `pdf` порта `qtwebengine`); время сборки не измерялось (оценка 45–90 мин). Имеет смысл только для статической линковки (LGPL-обязательства, §2.10).

### 2.3 Какие модули брать

| Модуль (CMake component / target) | DLL | Плагины (windeployqt кладёт сам) | Зачем |
|---|---|---|---|
| `Widgets` / `Qt6::Widgets` (+ Core, Gui) | Qt6Core, Qt6Gui, Qt6Widgets | `platforms/qwindows.dll`, `styles/qmodernwindowsstyle.dll` (в нём `qwindowsvistastyle.cpp` и `qwindows11style.cpp`), `generic/qtuiotouchplugin.dll` | UI |
| `PrintSupport` / `Qt6::PrintSupport` | Qt6PrintSupport.dll | **плагина нет**: `qprintengine_win.cpp`, `qwindowsprintdevice.cpp`, `qwindowsprintersupport.cpp`, `qprintdialog_win.cpp`, `qpagesetupdialog_win.cpp` вкомпилированы под `CONDITION WIN32` (`src/printsupport/CMakeLists.txt`); `src/plugins/printsupport` — только `cups/`, `ohos/` | QPrinter, нативный QPrintDialog/QPageSetupDialog, QPrintPreviewDialog |
| `Svg` / `Qt6::Svg` (+ `SvgWidgets`) | Qt6Svg.dll | `imageformats/qsvg.dll`, `iconengines/qsvgicon.dll` | иконки тулбара (SVG, HiDPI), штампы |
| `Concurrent` / `Qt6::Concurrent` | Qt6Concurrent.dll | — | `QtConcurrent::run` → `QFuture` (QThreadPool хватает и без него) |
| `Network` / `Qt6::Network` | Qt6Network.dll | `tls/qschannelbackend.dll`, `tls/qcertonlybackend.dll`, `networkinformation/qnetworklistmanager.dll` (`tls/qopensslbackend.dll` только при наличии OpenSSL 3 DLL) | TSA (RFC 3161) и OCSP — HTTP(S) POST через QNetworkAccessManager; Schannel хватает. ssl.html: Qt «will attempt to load the backends in order (with OpenSSL attempted first)», бинарники «require OpenSSL 3 at runtime» |
| `Test` / `Qt6::Test` | Qt6Test.dll (только тесты) | — | QTest, QSignalSpy |
| `LinguistTools` (только CMake) | — | — | `qt_add_translations`, lupdate/lrelease |
| `Pdf`, `PdfWidgets` (расширение `qtpdf`) | Qt6Pdf.dll, Qt6PdfWidgets.dll | — | **не брать** (§2.4) |
| `qtimageformats` (addon) | — | `imageformats/qtiff.dll, qwebp.dll, qtga.dll, qwbmp.dll, qicns.dll` (jpeg/png/gif/ico/bmp — в базе) | импорт TIFF/WebP → PDF |

Минимальный `CMakeLists.txt` (проверен на 6.12.0; `qt_standard_project_setup` ставит `CMAKE_AUTOMOC`/`CMAKE_AUTOUIC`, не AUTORCC; Qt сконфигурирован под политики CMake 3.25):
```cmake
cmake_minimum_required(VERSION 3.25)
project(PdfStudio VERSION 0.1 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(Qt6 6.12 REQUIRED COMPONENTS Widgets PrintSupport Svg Concurrent Network Test LinguistTools)
qt_standard_project_setup(I18N_TRANSLATED_LANGUAGES ru en)
qt_add_executable(pdfstudio src/main.cpp ...)
target_link_libraries(pdfstudio PRIVATE Qt6::Widgets Qt6::PrintSupport Qt6::Svg Qt6::Concurrent Qt6::Network)
set_target_properties(pdfstudio PROPERTIES WIN32_EXECUTABLE ON)   # без консоли
```
Сборка без экрана (проверено; cmake 3.31.6-msvc6 и ninja 1.12.1 из Build Tools):
```bat
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
set CM=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin
"%CM%\cmake.exe" -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.12.0\msvc2022_64 ^
  "-DCMAKE_MAKE_PROGRAM=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
"%CM%\cmake.exe" --build build
```
Configure 14 с; весь цикл configure+build+2 запуска+ctest+windeployqt — 46,5 с.

### 2.4 QtPdf (PDFium) против своего виджета на MuPDF

Что даёт QtPdf («Qt Pdf 6.140.0», PDFium): `QPdfDocument` (`load(QString|QIODevice)`, `setPassword`, `pageCount`, `pagePointSize`, `render(page, QSize, options, format, fillColor)`, `getSelection`, `getAllText`, `pageLabel`, `pageIndexForLabel`, `metaData`, `pageModel`, `close`), `QPdfView` (QAbstractScrollArea; `PageMode::SinglePage|MultiPage`, `ZoomMode::Custom|FitToWidth|FitInView`, `pageSpacing`, `documentMargins`, `zoomFactor`, `searchModel`, `pageNavigator()`; появился в Qt 6.4 — в 6.3 только QPdfDocument/QPdfDocumentRenderOptions/QPdfPageRenderer/QPdfPageNavigation), `QPdfPageRenderer` (очередь, `RenderMode::MultiThreaded|SingleThreaded`), `QPdfSearchModel`, `QPdfBookmarkModel`, `QPdfLinkModel`, `QPdfPageSelector`, `QPdfPageNavigator`. `RenderFlag::Annotations`. **Нет** функций сохранения/редактирования (единственный «setter» — `setPassword`). Лицензия LGPLv3/GPLv2/коммерческая; PDFium — BSD.

| Критерий | QtPdf + MuPDF для правок | Только MuPDF (свой виджет) |
|---|---|---|
| Готовый виджет | есть (QPdfView): экономит ~1–2 сессии | писать самим (≈ 800–1500 строк: раскладка, тайлы, зум, кэш) |
| Согласованность картинки | два движка → разные шрифтовые движки/AA, разный вид аннотаций и форм; после правки через MuPDF — сохранять во временный файл/QIODevice и перезагружать | один рендер для экрана, миниатюр, печати, тестовых снимков, предпросмотра правок |
| Интерактив редактора | оверлеи поверх чужой прокрутки/зума | полный контроль над `paintEvent`, hit-test через `pdf_annot`/`fz_stext_page` |
| Память/размер | +Qt6Pdf.dll (27 МБ установка), документ парсится дважды | один экземпляр |
| Установка/CI | расширение 6.140.0 с граблями aqt | — |

**Вердикт:** свой виджет на MuPDF. Эталонный код — просмотрщик `mupdf-gl` (`platform/gl`: gl-main.c, gl-app.h, gl-annotate.c, gl-form.c, gl-input.c, gl-ui.c, gl-win32.c): зум, кэш страниц, навигация по ссылкам, аннотации, формы.

### 2.5 Свой виджет просмотра: потоки, тайлы, HiDPI

Схема потоков (правила MuPDF — §1.2):
- `DocumentWorker` (один `QThread`/strand): открытие, `fz_load_page`, `fz_new_display_list_from_page` (кэш LRU ~30–60 списков), текст (`fz_new_stext_page_from_page`), все правки (`pdf_*`), сохранение. Все вызовы к документу сериализованы через очередь задач.
- `RenderPool` (`QThreadPool`, `maxThreadCount = idealThreadCount()-2`; на машине 20 потоков): `QRunnable` с `thread_local fz_context*` (клон), рендер тайла `fz_run_display_list` со `scissor` = тайл; результат — `QImage` (implicitly shared) через queued signal в GUI. Отмена: `cookie.abort = 1` при смене зума/прокрутки; приоритет видимых тайлов (`QThreadPool::start(runnable, priority)`).
- Локи: `std::mutex m[FZ_LOCK_MAX]` + `lock/unlock`; `create_external_mutex/destroy_external_mutex` реализовать (`new std::mutex`/`delete`).

Тайлы, кэш, масштаб:
- `PageView : QAbstractScrollArea`: виртуальная раскладка страниц, `verticalScrollBar()` в логических px; режимы непрерывный/постраничный/разворот — одна `layoutPages()`.
- Тайлы 512×512 device px; ключ `{pageIndex, zoomBucket, dpr, tx, ty}`; `QCache<Key, QImage>` с cost = байты, лимит 256–512 МБ; «превью» страницы в низком разрешении, пока тайлы не готовы.
- HiDPI (highdpi.html): «Qt 6 is Per-Monitor DPI Aware V2 by default»; `QT_SCALE_FACTOR_ROUNDING_POLICY` — `Round`, `PassThrough` (по умолчанию); enum `Qt::HighDpiScaleFactorRoundingPolicy`: Round, Ceil, Floor, RoundPreferFloor, PassThrough; `QT_ENABLE_HIGHDPI_SCALING=0` — «intended for testing purposes only»; `qt.conf` `[Platforms] WindowsArguments = dpiawareness=0,1,2`; `QT_SCALE_FACTOR` — «used as-is … isn't affected by the rounding policy». Рендерить с `ctm = fz_scale(zoom*dpr …)`, `QImage::setDevicePixelRatio(dpr)`, рисовать в логических координатах; тайлы выравнивать по целым device-пикселям; отслеживать `QWindow::screenChanged` и сбрасывать кэш по dpr.
- Координаты: страница в points (`fz_bound_page`); одна `fz_matrix` для рендера, hit-test аннотаций и выделения текста.
- Прокрутка: колесо — стандарт QAbstractScrollArea; Ctrl+колесо — зум относительно курсора.

### 2.6 Печать (QPrinter + MuPDF)

- `QPrinter(QPrinter::HighResolution)` — «On Windows, sets the resolution of the printer to the printer's own resolution» (для PDF — 1200 dpi); `PrinterResolution` — «This value is deprecated»; `setResolution()` — «must be called before QPainter::begin()»; `pageRect()`/`paperRect()`; `setFullPage(true)` сдвигает начало координат в угол бумаги; `newPage()` после последней страницы даёт пустой лист; `DuplexMode`: `DuplexNone|DuplexAuto|DuplexLongSide|DuplexShortSide`; `setPaperSource` — «Warning: This is currently only implemented for Windows»; `setFromTo/printRange` — «The programmer is responsible for reading this setting and printing accordingly»; `PrintRange`: AllPages, Selection, PageRange, CurrentPage. `QPrinterInfo::availablePrinterNames()` в offscreen работает (локально: «Adobe PDF», «OneNote», «Microsoft Print to PDF»).
- `QPrintDialog` на Windows — «the native print dialog is used, which means that some QWidget and QDialog properties set on the dialog won't be respected»; `done()` к нативным диалогам не применяется. `QPageSetupDialog`, `QPrintPreviewDialog/QPrintPreviewWidget` — Qt-виджеты.
- Рисование: `QPainter p(&printer)`; на каждую страницу рендерить MuPDF в QImage при `printer.resolution()` (600 dpi × A4 ≈ 5000×7000×4 = 140 МБ) → ограничивать 300 dpi или рендерить полосами (`scissor`). Векторная печать (свой `fz_device` → QPainter) — отдельная большая задача. `QPrinter` на Windows печатает через GDI — большие страницы растрировать полосами.
- **Буклет (в Qt API нет):** N → кратно 4; лист k: лицо = [N−2k, 1+2k], оборот = [2+2k, N−1−2k]; `QPageLayout::Landscape`, две страницы рядом, `DuplexShortSide`; ручной дуплекс — нечётные, затем чётные в обратном порядке. «Вписать», автоповорот, «текущая/диапазон/выделенные», печать комментариев (`fz_run_page` — с аннотациями, `fz_run_page_contents` — без).
- Грабли: `QPrinter::PdfFormat` не нужен (свой PDF-экспорт MuPDF); нативный диалог игнорирует часть свойств. Тихая печать в файл и регистрация обработчика — §5.5.

### 2.7 Тесты без экрана

| | Qt Test | Catch2 v3 | GoogleTest |
|---|---|---|---|
| Установка | в комплекте (`Qt6::Test`) | FetchContent `GIT_TAG v3.8.1 # or a later release` (docs); актуальный релиз **v3.16.1, 08.10.2026**; `Catch2::Catch2WithMain`, `list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)`, `include(CTest)`, `include(Catch)`, `catch_discover_tests`; vcpkg catch2 3.16.1 | FetchContent `URL …/googletest/archive/<commit>.zip`, `set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)` (обязательно на MSVC с DLL-CRT), `GTest::gtest_main`, `gtest_discover_tests`; «at least C++17»; vcpkg gtest 1.18.0 |
| Виджеты | `QTEST_MAIN`: «If QT_WIDGETS_LIB is defined, the application object will be a QApplication, if QT_GUI_LIB … QGuiApplication, otherwise … QCoreApplication»; `QTEST_GUILESS_MAIN` — QCoreApplication; `QTEST_APPLESS_MAIN` — без приложения; `QTest::mouseClick/keyClicks/qWait/qWaitForWindowExposed` («default timeout of 5 seconds»), `QSignalSpy`, `QTRY_COMPARE` (таймаут — `QTest::defaultTryTimeout`, 6.11; «the macros double the timeout once»), `QFETCH` | свой QApplication + `QTest::qWait` | то же |
| CTest | `add_test(NAME t COMMAND t -o -,txt)`; `-o filename,format` с форматами `txt, csv, junitxml, xml, lightxml, teamcity, tap` (повторяемо, «-» = stdout, только один в stdout); `-silent`, `-v2`, `-maxwarnings` (2000); `QTEST_FUNCTION_TIMEOUT` — «interrupted after five minutes by default» (qFatal) | авто-обнаружение | авто-обнаружение |

**Рекомендация:** Qt Test для UI-слоя (JUnit `-o build/test-%NAME%.xml,junitxml`), gtest для движка (структура §6.7). Проверено на 6.12.0: тест с `QPushButton`+`QSignalSpy`+`mouseClick`+`grab()` проходит за 152 мс под offscreen.

Offscreen на Windows (проверено):
- `plugins/platforms/qoffscreen.dll` **есть** в prebuilt (собирается при `if(QT_FEATURE_freetype AND NOT WASM)`). qtest-overview.html: «Currently the offscreen platform plugin is only fully supported on X11» — но QWidget::grab, QTest, энумерация принтеров работают; шрифты/DPI могут отличаться от плагина `windows` — снимки offscreen не сравнивать с экранными.
- Шрифты: на Windows offscreen создаёт `QFreeTypeFontDatabase` (`#elif defined(Q_OS_WIN) m_fontDatabase.reset(new QFreeTypeFontDatabase());`), `populateFontDatabase()` сканирует только `fontDir()` = `QT_QPA_FONTDIR` или `<LibrariesPath>/fonts` по маскам `*.ttf *.pfa *.pfb *.otf` (**без .ttc**), при отсутствии пишет «QFontDatabase: Cannot find font directory %s. Note that Qt no longer ships fonts. Deploy some (from https://dejavu-fonts.github.io/ for example) or switch to fontconfig». С `QT_QPA_FONTDIR=C:\Windows\Fonts`: 176 семейств, кириллица в PNG корректна, **шрифт по умолчанию — «Agency FB»** → `QApplication::setFont(QFont("Segoe UI", 9))` либо папка из 2–3 шрифтов (Segoe UI + DejaVu) для детерминированных снимков.
- Стиль под offscreen по умолчанию — `fusion` (ключи `windows11, windowsvista, Windows, Fusion`); для снимков `QApplication::setStyle("fusion")`, `colorScheme` = Unknown; поведение `windows11`/тёмной палитры под offscreen не проверялось.
- Снимки: `QPixmap pm = widget->grab(); pm.save("snap.png")` («Renders the widget into a pixmap restricted by the given rectangle», дети рисуются); сравнение — pHash/SSIM (§6.2).
- CMake (проверено):
```cmake
enable_testing()
qt_add_executable(test_view tests/test_view.cpp)
target_link_libraries(test_view PRIVATE Qt6::Widgets Qt6::Test pdfcore)
add_test(NAME test_view COMMAND test_view -o -,txt -o ${CMAKE_BINARY_DIR}/junit-test_view.xml,junitxml)
set_tests_properties(test_view PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_FONTDIR=C:/Windows/Fonts;QT_FORCE_STDERR_LOGGING=1")
```
Запуск: `ctest --test-dir build --output-on-failure`. Ненативные диалоги в тестах: `QFileDialog::DontUseNativeDialog`/`Qt::AA_DontUseNativeDialogs` (ставить до остальных свойств и показа); `-platform offscreen` «Overrides the QT_QPA_PLATFORM environment variable».

### 2.8 Развёртывание

**windeployqt (6.12)**, флаги: `--release|--debug`, `--pdb`, `--no-translations` / `--translations <languages>`, `--no-opengl-sw` (opengl32sw.dll), `--no-system-d3d-compiler`, `--no-system-dxc-compiler` (иначе `dxcompiler.dll`+`dxil.dll`), `--compiler-runtime|--no-compiler-runtime` («Deploy runtime compiler (Desktop only)»; по умолчанию копирует CRT с машины разработчика: «These individual DLLs are not intended or licensed for redistribution … Only the official Microsoft Redistributable installer should be used»), `--dir`, `--libdir`, `--plugindir`, `--list source|target|relative|mapping`, `--skip-plugin-types`/`--add-plugin-types`, `--include-plugins`/`--exclude-plugins`, `--include-soft-plugins`, `--no-quick-import`, `--no-ffmpeg`, `--force-openssl`, `--openssl-root`, `--dry-run`, `--force`, `--json`, `--appx`, `--appx-certificate`. «For Windows, the name of the platform plugin is `qwindows.dll`. This file must be located within a specific subdirectory (by default, `platforms`)».

Проверенный результат для Widgets+PrintSupport+Svg+Network (47,3 МБ):
```
Qt6Core.dll Qt6Gui.dll Qt6Widgets.dll Qt6PrintSupport.dll Qt6Svg.dll Qt6Network.dll
platforms\qwindows.dll   styles\qmodernwindowsstyle.dll   generic\qtuiotouchplugin.dll
iconengines\qsvgicon.dll imageformats\{qgif,qicns,qico,qjpeg,qsvg,qtga,qtiff,qwbmp,qwebp}.dll
networkinformation\qnetworklistmanager.dll   tls\qschannelbackend.dll tls\qcertonlybackend.dll
dxcompiler.dll dxil.dll   (уберёт --no-system-dxc-compiler)
```
Плагина для печати нет. Qt6Concurrent.dll появится, как только код вызовет QtConcurrent. Лишнее: `--exclude-plugins qtuiotouchplugin,qicns,qtga,qwbmp`. `qoffscreen.dll` в deploy не нужен. Команда: `windeployqt --release --no-translations --no-opengl-sw --no-system-d3d-compiler --no-system-dxc-compiler --no-compiler-runtime build\deploy\pdfstudio.exe`. CMake-эквивалент: `qt_generate_deploy_app_script(TARGET pdfstudio OUTPUT_SCRIPT s NO_TRANSLATIONS NO_COMPILER_RUNTIME DEPLOY_TOOL_OPTIONS --no-opengl-sw --no-system-d3d-compiler --no-system-dxc-compiler)` + `install(SCRIPT ${s})` после `install(TARGETS … RUNTIME DESTINATION .)` (команда с 6.3; `DEPLOY_TOOL_OPTIONS` — с 6.7; `EXCLUDE_PLUGINS/INCLUDE_PLUGINS/EXCLUDE_PLUGIN_TYPES/NO_PLUGINS` — с 6.10).

**VC++ runtime:** `vc_redist.x64.exe` на машине: `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC\14.44.35112\vc_redist.x64.exe` (25 635 768 Б; x86 13 953 392 Б; есть и `v143\`), либо `aqt install-tool … tools_vcredist qt.tools.vcredist_msvc2022_x64`. Тихая установка `vc_redist.x64.exe /install /quiet /norestart` «If you aren't running from an elevated command prompt, you'll need to respond to a User Account Control prompt» → для per-user инсталлятора — **app-local**: копировать всю папку `VC\Redist\MSVC\14.44.35112\x64\Microsoft.VC143.CRT\` (**10 DLL**: `concrt140, msvcp140, msvcp140_1, msvcp140_2, msvcp140_atomic_wait, msvcp140_codecvt_ids, vccorlib140, vcruntime140, vcruntime140_1, vcruntime140_threads`; минимум — `msvcp140`, `vcruntime140`, `vcruntime140_1`, для Qt 6/C++20 плюс `msvcp140_1/_2/_atomic_wait/_codecvt_ids`). Microsoft: «It's also possible to directly install the Redistributable DLLs in the application local folder … For servicing reasons, we don't recommend»; UCRT входит в Windows 10/11; лицензионно покрывается списком Distributable Code VS 2022.

**Inno Setup** — §3.7.

### 2.9 Стиль и тёмная тема

- Ключи `QStyleFactory::keys()` на Windows: `windows11` (Fluent; по умолчанию на Win11 с Qt 6.7, «This style supports Windows dark mode»), `windowsvista` (тёмный режим не поддерживает), `Windows`, `Fusion` (built-in). Оба платформенных — в `styles/qmodernwindowsstyle.dll`.
- Тёмная тема: `-platform windows:darkmode=[0|1|2]` — 0 выкл, 1 «switch the window borders to black», 2 «will in addition cause the Windows Vista style to be deactivated» (стиль Windows с упрощённой тёмной палитрой, experimental); «As of Qt 6.5, the default value is 2». `QStyleHints::colorScheme()` (6.5, «a hint to the system») + `colorSchemeChanged`; `setColorScheme(Qt::ColorScheme::Dark)`/`unsetColorScheme()` (6.8). Свои цвета пересчитывать на `QEvent::ApplicationPaletteChange`.
- Практика: **Fusion + собственная QPalette (светлая/тёмная) + минимум QSS** — детерминированно и одинаково в offscreen; `windows11` — опция «как в системе». Иконки: SVG через `qsvgicon` с двумя наборами цветов.

### 2.10 Лицензия Qt LGPLv3 (qt.io/licensing/open-source-lgpl-obligations)

- Динамическая линковка позволяет держать исходники приложения закрытыми; «In case of static linking of the library, the application itself may no longer be "work that uses the library"» → либо исходники, либо динамическая линковка.
- «The user is allowed to change and re-link the library used in the application or device» → Qt как отдельные DLL рядом с exe, без проверок целостности.
- Текст LGPLv3 и «displaying a prominent notice about using the LGPL library»; «it is not allowed to hide the fact the LGPL library is used» — окно «О программе» + LICENSE.LGPLv3 в инсталляторе; «the complete corresponding source code has to be delivered even if the library has not been modified at all», или «written offer» — ссылка/копия `qt-everywhere-src-6.12.0`.
- Сторонние (licenses-used-in-qt.html): FreeType 2.14.3 — FTL или GPLv2; HarfBuzz-NG 14.5.0 — MIT; LibPNG 1.6.58; libjpeg-turbo 3.2.0 — IJG + BSD-3; PDFium — BSD. Для программы «для себя» обязательства возникают только при распространении.

### 2.11 Qt Linguist (ru/en)

- `lupdate.exe`, `lrelease.exe`, `linguist.exe` — в `bin` kit.
- CMake: `qt_add_translations` введена в 6.2; сигнатура 6.7+ `qt_add_translations([target] [TARGETS …] [SOURCE_TARGETS …] [TS_FILE_BASE name] [TS_FILES …] [PLURALS_TS_FILE] [NO_GENERATE_PLURALS_TS_FILE] [RESOURCE_PREFIX prefix] [OUTPUT_TARGETS var] [TS_FILES_OUTPUT_VARIABLE var] (6.8) [TS_OUTPUT_DIRECTORY dir] (6.9) [QM_FILES_OUTPUT_VARIABLE var] [QM_OUTPUT_DIRECTORY dir] (6.9) [SOURCES …] [INCLUDE_DIRECTORIES …] [LUPDATE_TARGET] [LUPDATE_OPTIONS …] [LRELEASE_TARGET] [LRELEASE_OPTIONS …] [MERGE_QT_TRANSLATIONS] (6.9) [QT_TRANSLATION_CATALOGS …] [IMMEDIATE_CALL])`; старая форма `qt_add_translations(target TS_FILES …)` — deprecated; `RESOURCE_PREFIX` по умолчанию `/i18n`; цели `update_translations` (lupdate, запускать явно: `cmake --build build --target update_translations`) и `release_translations` (lrelease, по умолчанию). Нужен `find_package(Qt6 REQUIRED COMPONENTS LinguistTools)`.
- Загрузка: `QTranslator t; if (t.load(QLocale(), "pdfstudio", "_", ":/i18n")) app.installTranslator(&t);` + `qtbase_ru.qm` (или `MERGE_QT_TRANSLATIONS`). Строки — `tr("…")`, числа `tr("%n page(s)", nullptr, n)`. `.ts` держать в репозитории; `IMMEDIATE_CALL` нужен, если переводы объявлены до всех `qt_add_executable`.

### 2.12 Известные грабли Qt 6 на Windows (сводка)

1. **Масштабирование**: `PassThrough` по умолчанию → дробные dpr; растровые иконки мылятся — SVG; при проблемах `QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::Round)` до создания QApplication или `QT_SCALE_FACTOR_ROUNDING_POLICY=Round`.
2. **QFileDialog**: статические `getOpenFileName/getSaveFileName/getExistingDirectory` на Windows — «uses the native file dialog and not a QFileDialog»; «some widget-specific APIs (such as layout() and itemDelegate()) may return null»; `DontUseNativeDialog` ставить до остальных свойств; «the native Windows file dialog does not support displaying files in the directory chooser». В offscreen — только ненативные.
3. **Долгий старт**: с 6.8 шрифтовая база DirectWrite; `-platform windows:fontengine=freetype|gdi` (`nodirectwrite` — «since Qt 5.8») — инструмент диагностики. Подтверждённых отчётов о медленном старте 6.12 не найдено (поиск недоступен) — измерять `qtdiag.exe`.
4. `QT_QPA_PLATFORM=windows:darkmode=…`, `dpiawareness=0|1|2` через `qt.conf` `[Platforms] WindowsArguments=…`; `menus=native|none`; `nomousefromtouch`, `nowmpointer`.
5. `OleInitialize` в GUI-потоке (STA) — не вызывать `CoInitializeEx` с другой моделью (важно для Word COM и WIA — отдельный поток).
6. MAX_PATH 260 для сборки — короткие пути; манифест `qt_add_executable` с 6.11 (в 6.12 `longPathAware`).
7. windeployqt копирует CRT разработчика без `--no-compiler-runtime` — не распространять.
8. Qt-логи без консоли уходят в OutputDebugString (debug.html: «if it is a console application, the text is sent to console; otherwise, it is sent to the debugger») — `QT_FORCE_STDERR_LOGGING=1` / `QT_ASSUME_STDERR_HAS_CONSOLE=1` / `QT_WIN_DEBUG_CONSOLE=new|attach`.
9. aqt/Python/расширение QtPdf — §2.2.
10. OpenSSL 3.0 из `tools_opensslv3_x64` — EOL; не класть рядом с exe, иначе Qt попробует его первым (ssl.html).
11. `QFile(const std::filesystem::path&)` — с 6.9 безусловно explicit; `QT_NO_CAST_FROM_ASCII` отключает неявные преобразования; обратно — `QString::fromStdWString(p.native())`.

## 3. Сборка, зависимости и упаковка на MSVC

Метод исходной проверки: WebFetch learn.microsoft.com, cmake.org, doc.qt.io, jrsoftware.org, ccache.dev, GitHub raw/API, PyPI, code.claude.com; локально — vswhere, `state.json`, vcvars64 → cl/cmake/ninja, свежий sparse-клон `microsoft/vcpkg` HEAD `857036234d15…` (2026-10-20 20:10 −0700, «[kcompletion] New port (#52561)»), `winget show`, пробная сборка CMake+Ninja с `<format>`/ranges под `-std:c++20`.

### 3.1 Что реально стоит на машине (проверено 2026-10-21)

| Компонент | Факт |
|---|---|
| VS Build Tools 2022 | `installationVersion` **17.14.37710.0** («17.14.41 (September 2026)»), `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools` |
| MSVC toolset | 14.44.35207; `cl.exe` 19.44.35229 (x64): «Оптимизирующий компилятор Microsoft (R) C/C++ версии 19.44.35229 для x64» |
| Windows SDK | 10.0.26100.0 (`Windows Kits\10\Include\10.0.26100.0`; компоненты `Windows11SDK.26100` + групповой `Windows10SDK`) |
| CMake / Ninja в Build Tools | `cmake 3.31.6-msvc6`, `ninja 1.12.1` в `Common7\IDE\CommonExtensions\Microsoft\CMake\{CMake\bin,Ninja}`; **`vcvars64.bat` кладёт оба в PATH** (`where cmake`/`where ninja` — проверено) |
| Переменные vcvars64 | `VCToolsVersion=14.44.35207`, `WindowsSDKVersion=10.0.26100.0\`, `VCToolsRedistDir=…\VC\Redist\MSVC\14.44.35112\`, `VCINSTALLDIR=…\BuildTools\VC\` |
| Компоненты (state.json) | `VC.Tools.x86.x64`, `VC.CMake.Project`, `VC.ASAN`, `VC.Redist.14.Latest`, `Windows11SDK.26100`, `Vcpkg`, `TestTools.BuildTools`, `VC.CoreIde`, `TextTemplating`, `Workload.VCTools`. **`VC.Llvm.Clang` нет** |
| clang-format / clang-tidy | **Есть**: `VC\Tools\Llvm\x64\bin\clang-format.exe`, `clang-tidy.exe` **19.1.5** (также `Llvm\bin` x86, `Llvm\ARM64\bin`); `clang-cl.exe`/`clang.exe` нет. Компонент `VC.Llvm.Clang` для 17.14 = «C++ Clang Compiler for Windows (19.1.5)» — даст clang-cl той же версии |
| vcpkg, встроенный в Build Tools | `VC\vcpkg\vcpkg.exe` версии `2025-11-19-da1f056d…`; каталог содержит только `scripts`, `triplets`, `vcpkg-init*`, `vcpkg-bundle.json` (`"readonly": true, "deployment": "VisualStudio", "usegitregistry": true`) — **без `ports`**, реестр клонирует в профиль; лежит в Program Files (`downloads/buildtrees` недоступны для записи) |
| VC Redist | `VC\Redist\MSVC\14.44.35112\x64\Microsoft.VC143.CRT\` — 10 DLL (§2.8); `vc_redist.x64.exe` 25 635 768 Б; есть `v143` |
| Языковой пакет MSVC | только русский (`bin\Hostx64\x64\1049`, плюс `onecore`), `1033` нет → `VSLANG=1033` не переключит cl.exe; `setup.exe modify --addProductLang en-US` требует UAC |
| Кодовые страницы | ACP=1252 (локаль de-DE), консоль сессии — 65001 |
| PREfast / санитайзеры | `EspXEngine.dll`, `CppCoreCheck.dll`, `ConcurrencyCheck.dll` в `bin\Hostx64\x64`; 36 rulesets в `Team Tools\Static Analysis Tools\Rule Sets\`; `clang_rt.asan_dynamic-x86_64.dll` (в `bin\Hostx64\x64` и `bin\Hostx86\x64`); `clang_rt.fuzzer_{MT,MD,MTd,MDd}-x86_64.lib` + `*_no_main_*`, `clang_rt.ubsan_standalone-x86_64.lib`/`_cxx` в `lib\x64`; `include\fuzzer\FuzzedDataProvider.h` |
| Inno Setup | 6.7.3 per-user (`HKCU\…\Uninstall\Inno Setup 6_is1`), `%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe`; `Examples\Example3.iss` — пример ассоциации файлов |
| Python / pip / Git / winget | 3.15.0b3 по умолчанию (`%LOCALAPPDATA%\Programs\Python\Python315`); `py -3.14` → 3.14.6 (`C:\Users\WetFingers\AppData\Local\Python\pythoncore-3.14-64\python.exe`); `py -3.11`; pip 26.1.2; Git 2.54.0.windows.1 (с `C:\Program Files\Git\usr\bin\openssl.exe` = OpenSSL 3.5.6, mingw64 — для скриптов/PKI, не для линковки); winget v1.29.380 |
| Java | `java.exe` OpenJDK **25.0.3** (JetBrains Runtime из Android Studio) в PATH, `JAVA_HOME=C:\Program Files\Android\Android Studio\jbr` |
| Office / браузеры | Word 16 x64 (`C:\Program Files\Microsoft Office\root\Office16\WINWORD.EXE`, ClickToRun 16.0.20430.20182, O365HomePremRetail, `MSWORD.OLB` 957 168 Б); Chrome `C:\Program Files\Google\Chrome\Application\chrome.exe`; **msedge.exe нет** (только manifest 149.0.4022.62 + pwahelper) |
| Принтеры | «Microsoft Print to PDF» (драйвер `Microsoft Print To PDF`, порт `PORTPROMPT:`), «Adobe PDF», «OneNote (Desktop)» |
| Пробная сборка | `cmake -S src -B build -G Ninja` + `CMAKE_CXX_STANDARD 20` + `/utf-8`: конфигурация 0,3 с; CMake выставил **`-std:c++20`** и записал в `build\CMakeFiles\rules.ninja:17` `msvc_deps_prefix = Примечание: включение файла:`; тест с `std::format`, `std::views::filter/transform`, `std::format("{:%Y}", std::chrono::year{2026})` вывел `20 60 2026` |

### 3.2 vcpkg

**Установка без администратора** (официальный туториал; прав администратора не требует; хосты: «Full support, tested» — Windows 11 + VS 2022 17.14; 32-битная Windows и VS 2015 не поддерживаются; требования английского языкового пакета в документации больше **нет** — фактическую работу с русским-only `cl.exe` подтвердит первый `vcpkg install`):
```powershell
git clone https://github.com/microsoft/vcpkg.git "$env:LOCALAPPDATA\vcpkg"
& "$env:LOCALAPPDATA\vcpkg\bootstrap-vcpkg.bat" -disableMetrics
$env:VCPKG_ROOT = "$env:LOCALAPPDATA\vcpkg"; $env:PATH = "$env:VCPKG_ROOT;$env:PATH"
```

**Manifest mode.** `vcpkg.json` — строгий JSON (без комментариев и висячих запятых; комментарии — поля с `$`); `"$schema": "https://raw.githubusercontent.com/microsoft/vcpkg-tool/main/docs/vcpkg.schema.json"`; `builtin-baseline` — коммит реестра; `overrides` — пины `"version": "1.2.3#7"`; у зависимости `"version>="`, `"default-features": false`, `"features"`, `"platform"`, `"host"`:
```json
{
  "$schema": "https://raw.githubusercontent.com/microsoft/vcpkg-tool/main/docs/vcpkg.schema.json",
  "name": "pdfstudio",
  "version": "0.1.0",
  "builtin-baseline": "857036234d15fbed8f30fed66e898d7725446c0c",
  "dependencies": [
    { "name": "qpdf", "default-features": false, "features": ["openssl"] },
    "openssl", "tiff", "zlib", "libjpeg-turbo",
    "fmt", "spdlog", "nlohmann-json", "sqlite3"
  ],
  "features": {
    "tests": { "description": "Unit tests", "dependencies": ["gtest"] }
  }
}
```
(MuPDF, Tesseract и Leptonica берутся из `.sln` MuPDF — §1.18; `libmupdf`/`tesseract` в манифест не включать, чтобы не держать две копии Tesseract.)

`CMakePresets.json` (схема: v6 = CMake 3.25, v10 = 3.31, v11 = 4.3; `toolset`/`architecture` со `"strategy": "external"` CMake не применяет — «intended for cases like using the Ninja generator with an IDE that sets up the Visual C++ environment itself»; `testPresets` поддерживают `environment` и `output.outputOnFailure`):
```json
{
  "version": 6,
  "configurePresets": [{
    "name": "msvc-release",
    "generator": "Ninja",
    "binaryDir": "${sourceDir}/build/release",
    "toolset": { "value": "host=x64", "strategy": "external" },
    "architecture": { "value": "x64", "strategy": "external" },
    "cacheVariables": {
      "CMAKE_BUILD_TYPE": "Release",
      "CMAKE_TOOLCHAIN_FILE": "$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake",
      "VCPKG_TARGET_TRIPLET": "x64-windows-static-md-release",
      "VCPKG_MANIFEST_FEATURES": "tests",
      "CMAKE_PREFIX_PATH": "$env{QT_ROOT}",
      "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"
    }
  }],
  "buildPresets": [{ "name": "msvc-release", "configurePreset": "msvc-release" }],
  "testPresets": [{ "name": "msvc-release", "configurePreset": "msvc-release",
    "environment": { "QT_QPA_PLATFORM": "offscreen", "QT_QPA_FONTDIR": "C:/Windows/Fonts", "QT_FORCE_STDERR_LOGGING": "1" },
    "output": { "outputOnFailure": true } }]
}
```
«All vcpkg-affecting variables must be defined before the first `project()` directive»: `VCPKG_TARGET_TRIPLET` (смена требует удалить кэш CMake), `VCPKG_HOST_TRIPLET`, `VCPKG_INSTALLED_DIR` (`${CMAKE_BINARY_DIR}/vcpkg_installed`), `VCPKG_MANIFEST_FEATURES`, `VCPKG_MANIFEST_NO_DEFAULT_FEATURES`, `VCPKG_OVERLAY_PORTS`/`VCPKG_OVERLAY_TRIPLETS`, `VCPKG_INSTALL_OPTIONS`, `VCPKG_APPLOCAL_DEPS` (по умолчанию `ON`), `VCPKG_LOCK_FIND_PACKAGE_<Pkg>`.

**Триплеты** (в клоне реестра): `x64-windows` (DLL, /MD), `x64-windows-static` (/MT), `x64-windows-static-md` (`VCPKG_CRT_LINKAGE dynamic` + `VCPKG_LIBRARY_LINKAGE static`), `x64-windows-release`, `x86-windows`, `arm64-windows`, `arm64-windows-static-md`; `triplets/community/` — **`x64-windows-static-md-release`** (то же + `set(VCPKG_BUILD_TYPE release)`; community = без CI vcpkg, можно скопировать в `--overlay-triplets`) и `x64-windows-static-release`. Так как Qt из aqt собран с `/MD`, нужен `x64-windows-static-md(-release)`. По умолчанию vcpkg собирает release и debug.

**Бинарный кэш:** включён по умолчанию (`files`) в первом существующем из `%VCPKG_DEFAULT_BINARY_CACHE%` → `%LOCALAPPDATA%\vcpkg\archives` → `%APPDATA%\vcpkg\archives`; `VCPKG_BINARY_SOURCES="clear;files,D:\vcpkg-cache,readwrite"`. ABI-hash учитывает файлы порта, триплет, **исполняемые файлы компилятора**, фичи, ABI зависимостей, helper-функции, **версию CMake**, **версию PowerShell**, `VCPKG_ENV_PASSTHROUGH` → обновление Build Tools инвалидирует кэш. Переменные: `VCPKG_DOWNLOADS`, `VCPKG_MAX_CONCURRENCY` («defaults to logical cores + 1» = 21 здесь → задать 10–12), `VCPKG_FORCE_SYSTEM_BINARIES` (иначе vcpkg качает свои CMake/Ninja), `VCPKG_FORCE_DOWNLOADED_BINARIES`, `VCPKG_DISABLE_METRICS`, `VCPKG_KEEP_ENV_VARS`, `VCPKG_OVERLAY_PORTS` (разделитель `;`), `VSLANG` (язык сообщений самого vcpkg).

**Что писать самим:** `vcpkg.json` + `builtin-baseline`, `CMakePresets.json`, overlay-триплет при желании, `.gitignore` для `build/`, `CMakeUserPresets.json` (абсолютные `VCPKG_ROOT`/`QT_ROOT`, не коммитить).
**Грабли:** переменные vcpkg после `project()` не действуют; смена триплета — чистый кэш CMake; первая сборка debug+release — часы; `find_package(unofficial-libmupdf CONFIG REQUIRED)` только при vcpkg-порте.

### 3.3 Порты vcpkg: наличие и версии (HEAD `857036234d15…`)

| Порт | Версия | Лицензия | Примечание |
|---|---|---|---|
| **mupdf** | **нет** | — | порт называется `libmupdf` |
| **libmupdf** | 1.26.10 | AGPL-3.0-only | см. §1.18; апгрейд до 1.28.x = overlay-port (скопировать `ports/libmupdf`, поменять `REF`/`SHA512`, пересмотреть `bin2coff.c`, GLOB исходников и список шрифтов); `pkcs7-openssl.c` из тарбола той же версии можно скомпилировать в составе приложения против vcpkg-`openssl` |
| **pdfium** / pdfium-binaries | **нет** | — | |
| qpdf | 12.4.2 | Apache-2.0 AND MIT | фичи `gnutls`, `openssl`, `zopfli`; deps libjpeg-turbo, zlib; требует C++20, CMake ≥ 3.16 |
| openssl | 3.6.5 #2 | Apache-2.0 | фичи fips/ssl3/tools/weak-ssl-ciphers; `no-tests no-docs`; **ветка 3.6 поддерживается до 2026-11-01** — для LTS 3.5 использовать `overrides` или FireDaemon (§4.11) |
| tesseract | 5.5.3 | Apache-2.0 | deps `cpu-features` (Android), `curl` (без default-фич), `leptonica`, `libarchive` (без default-фич); фича `training-tools` |
| leptonica | 1.87.0 | (null) | deps giflib, libjpeg-turbo, libpng, libwebp (!default), openjpeg, tiff (!default), zlib |
| tiff | 4.7.2 | — | default features `jpeg`, `lzma`, `zip`; опц. `zstd`, `webp`, `lerc`, `libdeflate`, `tools`, `cxx` |
| freetype | 2.14.3 | FTL OR GPL-2.0-or-later | default brotli,bzip2,png,zlib |
| harfbuzz | 14.6.0 | MIT-Modern-Variant AND MIT | сборка **meson** (host-dep `vcpkg-tool-meson`) |
| libjpeg-turbo | 3.2.0 #1 | BSD-3-Clause AND IJG | фичи jpeg7/jpeg8/tools |
| openjpeg | 2.5.4 | BSD-2-Clause | |
| zlib | 1.3.2 #2 | Zlib | |
| gtest | 1.18.0 | BSD-3-Clause | |
| catch2 | 3.16.1 | BSL-1.0 AND BSD-3-Clause | |
| fmt / spdlog / nlohmann-json / sqlite3 | 12.2.0 #1 / 1.17.0 #1 / 3.12.0 #2 / 3.54.1 | MIT / MIT / MIT / blessing | |
| poppler | 26.4.0 | GPL-2.0-or-later | фича `qt` → `ENABLE_QT6`; deps boost-container…; GPL и дублирует MuPDF — не брать |
| qtbase / qttools / qtsvg | 6.11.2 | — | полная сборка из исходников (§2.2) |
| podofo / libharu / vanillapdf | 1.1.1 / 2.4.6 / 2.3.0 | LGPL-2.0+ OR MPL-2.0 / Zlib / Apache-2.0 | альтернативы для генерации |
| mujs / lcms / jbig2dec / gumbo | 1.3.10 / 2.19.1 #1 / 0.20 / 0.14.1 | | пригодятся только для overlay-порта libmupdf (lcms — upstream использует свой thread-safe форк, совместимость не проверена) |

**Оценка времени сборки (не подтверждена источниками; release-only; ×2 без `VCPKG_BUILD_TYPE=release`):** zlib/fmt/spdlog/nlohmann/gtest/catch2 — по 0,5–2 мин; libjpeg-turbo, openjpeg, freetype — 1–3 мин; harfbuzz — 3–6 мин (+ первая загрузка meson/python); openssl — 10–20 мин; qpdf — 3–5 мин; tiff — 2–4 мин; leptonica — 5–10 мин; tesseract — 5–10 мин (+ curl, libarchive); libmupdf — 5–10 мин; poppler + boost — 10–15 мин; qtbase — 45–90 мин. Итого первый `vcpkg install` набора из §3.2 без Qt/MuPDF/Tesseract — ориентировочно 30–60 мин. Замерить в этапе 0.

### 3.4 CMake и Ninja вне Visual Studio

1. **Через `vcvars64.bat`** (проверено): после `call "…\BuildTools\VC\Auxiliary\Build\vcvars64.bat"` в PATH — `cl`, `link`, `rc`, `cmake 3.31.6-msvc6`, `ninja 1.12.1`. Для PowerShell-очереди: генерировать `.cmd` (`call vcvars64.bat && cmake --preset …`) либо один раз снять окружение (`cmd /c "vcvars64.bat && set"`) в `$env:`. `cmake -G Ninja` без vcvars не найдёт `cl`.
2. **winget** (проверено `winget show`): `Kitware.CMake` **4.4.4** (portable zip, 2026-10-02); `Ninja-build.Ninja` **1.13.2** (portable zip, 2025-11-20); portable-пакеты ставятся в `%LOCALAPPDATA%\Microsoft\WinGet\Packages\` (`portablePackageUserRoot`; каталог ярлыков `…\WinGet\Links` документацией settings не подтверждён). Ключи: `--scope user|machine`, `-h/--silent`, `--accept-package-agreements`, `--accept-source-agreements`, `-l/--location`, `-e/--exact`, `--disable-interactivity`:
   ```powershell
   winget install --id Kitware.CMake --exact --scope user --accept-source-agreements --accept-package-agreements --silent
   winget install --id Ninja-build.Ninja --exact --scope user --silent
   ```
   Это **не обязательно**: CMake 3.31.6 поддерживает presets v10, `CMAKE_MSVC_DEBUG_INFORMATION_FORMAT`, unity, PCH. CMake 4.0: «Compatibility with versions of CMake older than 3.5 has been removed» (обход — `CMAKE_POLICY_VERSION_MINIMUM`). Ninja 1.12.1 vs 1.13.2 — для pools/`-j` разницы нет; при смене `CMAKE_MAKE_PROGRAM` нужен чистый `build/`.

### 3.5 clang-format, clang-tidy, cppcheck, статический анализ

| Инструмент | Наличие | Запуск |
|---|---|---|
| **clang-format / clang-tidy 19.1.5** | уже в `VC\Tools\Llvm\x64\bin` — добавить в PATH | `cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` (только Makefile/Ninja), `clang-tidy -p build --checks=-*,bugprone-*,cert-*,clang-analyzer-*,performance-*,readability-container-size-empty src\*.cpp` или `set(CMAKE_CXX_CLANG_TIDY …)` (с 3.25 при `-p` без `--`); `--extra-arg=/EHsc`, `-extra-arg=-fms-compatibility-version=19.44`; `--driver-mode=cl` выводится из имени `cl.exe` |
| **Полный Clang в Build Tools** | компонент `Microsoft.VisualStudio.Component.VC.Llvm.Clang` + `…VC.Llvm.ClangToolset`; «Visual Studio Installer operations currently require administrator privileges»; код 740 = «Elevation required», 3010 = перезагрузка | `"C:\Program Files (x86)\Microsoft Visual Studio\Installer\setup.exe" modify --installPath "…\BuildTools" --add Microsoft.VisualStudio.Component.VC.Llvm.Clang --add Microsoft.VisualStudio.Component.VC.Llvm.ClangToolset --passive --norestart` (**нужен человек/UAC**) |
| **winget LLVM.LLVM 23.1.4** (2026-10-20) | wix MSI `LLVM-23.1.4-win64.msi` 639 641 329 Б (админ); без установщика — `clang+llvm-23.1.4-x86_64-pc-windows-msvc.tar.zst` **276 626 313 Б** или `.tar.xz` 901 281 460 Б → распаковать в `%LOCALAPPDATA%\Programs\LLVM` | нужен только для clang-cl/UBSan (`clang-cl -fsanitize=undefined` — не проверено) |
| **cppcheck 2.22.0** (2026-09-19) | `cppcheck-2.22.0-x64-Setup.msi` 23 908 352 Б; winget `Cppcheck.Cppcheck` — wix, per-user варианта нет; `msiexec /a` не проверялся | `cppcheck --project=build\compile_commands.json --enable=warning,performance,portability --inline-suppr --error-exitcode=2 --xml 2> cppcheck.xml` |
| **MSVC Code Analysis (PREfast)** | есть (§3.1) | `set Esp.Extensions=CppCoreCheck.dll;ConcurrencyCheck.dll`, `set caexcludepath=%include%`; `cl /analyze /analyze:plugin EspXEngine.dll /analyze:ruleset "…\CppCoreCheckRules.ruleset" /analyze:external- /external:I <Qt,MuPDF> /analyze:log out\ /analyze:log:format:sarif /analyze:WX- …`; `/analyze:log path\` (с `\` — каталог; `.sarif` → SARIF); `/analyze:only` — без кодогенерации; `/analyze:quiet`; MS: «We don't guarantee support for these internals in future versions» |

**Что писать самим:** `.clang-format` (опции на пересечении 19.1 и 23.x), `.clang-tidy` (`HeaderFilterRegex: '^src/'`), `tools/lint.ps1`, CMake-пресет `analyze` (SARIF), `tools/analyze_summary.py` → markdown, порог «новых предупреждений 0» через baseline. **Грабли:** `/analyze` на Qt-заголовках шумит — `/external:I` + `/analyze:external-` (с 16.10); MuPDF `fz_try` (setjmp) путает анализаторы — подавлять по файлу; прогон clang-tidy на Qt-коде медленный — только по изменённым файлам.

### 3.6 Ускорение сборки

**ccache / sccache под MSVC:**

| | ccache 4.14.1 (winget portable zip, 2026-09-27; upstream 4.14.2 от 2026-10-18) | sccache 0.18.0 (winget portable zip, 2026-09-14) |
|---|---|---|
| MSVC | с 4.6 (2022-02-27): «Added support for caching calls to Microsoft Visual C++ (MSVC) and clang-cl» | «sccache supports gcc, clang, MSVC, rustc, …» |
| /Zi | 4.9.1: «Fixed an MSVC crash when using `/Zi` with many concurrent compilations»; 4.9: `/Fd`, `/FS`, `/MP` не входят в хэш | `-Zi`/`-ZI` без `-Fd` → `cannot_cache!("shared pdb")`; если PDB из `/Fd` уже существует → `Cacheable::No`; `/FS` → Ignore; `-Z7` — PassThrough |
| Локализованный `/showIncludes` | `msvc_dep_prefix` (`CCACHE_MSVC_DEP_PREFIX`), default «Note: including file:» — **здесь задать `Примечание: включение файла:`**; `msvc_utf8` (default true) добавляет `/utf-8` и требует UTF-8-исходники | `detect_showincludes_prefix` компилирует `test.c` с `-showIncludes -E`, декодирует stderr через **`CP_OEMCP`** — корректность при chcp 65001 не проверена |
| Кэш | `cache_dir`: `%USERPROFILE%\.ccache`, если существует, иначе `%LOCALAPPDATA%\ccache`; `max_size` 5 GiB | `SCCACHE_DIR` default `%LOCALAPPDATA%\Mozilla\sccache`, `SCCACHE_CACHE_SIZE` 10 GB; сервер гаснет через 10 мин простоя |
| PCH | раздел PCH в руководстве описывает только GCC/Clang — поддержка `/Yc`/`/Yu` не проверена | `/Yc`, `/Fp` → TooHard («PCHs are not supported in sccache») |
| C++20 modules | — | `/interface`, `/ifcOutput`, `/experimental:module` → не кэшируются |

CMake ≥ 3.25: `CMAKE_C_COMPILER_LAUNCHER`/`CMAKE_CXX_COMPILER_LAUNCHER` и `-Z7` вместо `/Zi` (`CMAKE_MSVC_DEBUG_INFORMATION_FORMAT`, только при `CMP0141 NEW` до первого `project()`; значения `Embedded` (-Z7), `ProgramDatabase` (-Zi), `EditAndContinue` (-ZI)):
```cmake
cmake_policy(SET CMP0141 NEW)
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")
```
Launcher работает с Ninja, не с генератором Visual Studio. **Вывод:** PCH и компиляторный кэш плохо совместимы — выбрать одно: либо PCH + unity, либо sccache/ccache без PCH.

**Unity builds и PCH:** `UNITY_BUILD`/`CMAKE_UNITY_BUILD` (с 3.16), `UNITY_BUILD_MODE` BATCH/GROUP, `UNITY_BUILD_BATCH_SIZE`, `UNITY_BUILD_UNIQUE_ID`, `SKIP_UNITY_BUILD_INCLUSION`; файлы с непустыми `COMPILE_OPTIONS/COMPILE_DEFINITIONS/COMPILE_FLAGS/INCLUDE_DIRECTORIES` исключаются автоматически. `target_precompile_headers` (с 3.16), `REUSE_FROM` требует одинаковых флагов у обоих таргетов, угловые скобки в genex — `$<ANGLE-R>`. Для Qt: PCH с `<QtWidgets/QtWidgets>` — наибольший выигрыш; батчи 8–16 файлов; `mocs_compilation.cpp` вне unity при конфликтах.

**Параллельность и память:** Ninja `GuessParallelism()`: 0–1 CPU → 2, 2 → 3, иначе `processors + 2` — здесь **22 `cl.exe`** при 15,7 ГБ ОЗУ. Рекомендация (оценка): `cmake --build build/release -j 10`, `-j 6` если параллельно крутится Ollama; линковку ограничить пулом (Ninja-only):
```cmake
set_property(GLOBAL PROPERTY JOB_POOLS compile=10 link=2)
set(CMAKE_JOB_POOL_COMPILE compile)
set(CMAKE_JOB_POOL_LINK link)
```
`/MP` с Ninja не комбинировать. `msvc_deps_prefix` в Ninja «Only needed when `deps = msvc` and no English Visual Studio version is used» — CMake 3.31 выставляет его сам. Пиковое потребление памяти `cl.exe` на Qt-тяжёлых TU не измерено.

### 3.7 Упаковка: Inno Setup, ассоциации, иконка

**Inno Setup 6.7.3** на машине; актуальные релизы (jrsoftware.org/isdl.php): 6.7.3 — 26.05.2026, **7.1.0 — 12.08.2026**. `PrivilegesRequired=lowest`: «Setup will not request to be run with administrative privileges even if it was started by a member of the Administrators group»; `{autopf}` → `{userpf}` = `{localappdata}\Programs`; `HKA` «Equals HKLM in administrative install mode, HKCU otherwise»; `ChangesAssociations=yes` — уведомление Explorer; `PrivilegesRequiredOverridesAllowed=commandline` даёт `/ALLUSERS` и `/CURRENTUSER` (`dialog` — ещё и диалог).
```ini
#define AppName "PdfStudio"
#define AppVer  "0.1.0"
#define AppExe  "PdfStudio.exe"
[Setup]
AppId={{B7C2E6D0-0000-4000-8000-PDFSTUDIO001}
AppName={#AppName}
AppVersion={#AppVer}
DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=commandline
ChangesAssociations=yes
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
SetupIconFile=res\app.ico
UninstallDisplayIcon={app}\{#AppExe}
OutputDir=dist
OutputBaseFilename={#AppName}-{#AppVer}-setup
Compression=lzma2/max
SolidCompression=yes
[Files]
Source: "build\deploy\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
[Icons]
Name: "{group}\{#AppName}"; Filename: "{app}\{#AppExe}"
Name: "{autodesktop}\{#AppName}"; Filename: "{app}\{#AppExe}"; Tasks: desktopicon
[Tasks]
Name: desktopicon; Description: "Ярлык на рабочем столе"; Flags: unchecked
[Registry]
; ProgID
Root: HKA; Subkey: "Software\Classes\{#AppName}.pdf"; ValueType: string; ValueName: ""; ValueData: "PDF-документ"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\{#AppName}.pdf\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\{#AppExe},0"
Root: HKA; Subkey: "Software\Classes\{#AppName}.pdf\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" ""%1"""
Root: HKA; Subkey: "Software\Classes\{#AppName}.pdf\shell\print\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" /p ""%1"""
Root: HKA; Subkey: "Software\Classes\{#AppName}.pdf\shell\printto\command"; ValueType: string; ValueName: ""; ValueData: """{app}\{#AppExe}"" /pt ""%1"" ""%2"" ""%3"" ""%4"""
; предложить в «Открыть с помощью», не отбирая текущую ассоциацию (.pdf в HKCU уже может быть занят Edge)
Root: HKA; Subkey: "Software\Classes\.pdf\OpenWithProgids"; ValueType: string; ValueName: "{#AppName}.pdf"; ValueData: ""; Flags: uninsdeletevalue
; регистрация для «Приложения по умолчанию» (ApplicationDescription обязателен; ApplicationName = имя в RegisteredApplications)
Root: HKA; Subkey: "Software\{#AppName}\Capabilities"; ValueType: string; ValueName: "ApplicationName"; ValueData: "{#AppName}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\{#AppName}\Capabilities"; ValueType: string; ValueName: "ApplicationDescription"; ValueData: "Просмотр и редактирование PDF"
Root: HKA; Subkey: "Software\{#AppName}\Capabilities\FileAssociations"; ValueType: string; ValueName: ".pdf"; ValueData: "{#AppName}.pdf"
Root: HKA; Subkey: "Software\RegisteredApplications"; ValueType: string; ValueName: "{#AppName}"; ValueData: "Software\{#AppName}\Capabilities"; Flags: uninsdeletevalue
```
Компиляция из очереди (ISCC 6; `.iss` с кириллицей — сохранять в UTF-8 с BOM):
```
"%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe" /Qp /Obuild\installer /FPdfStudio-0.1-setup "/DAppVer=0.1.0" "/DDeployDir=build\deploy" installer\pdfstudio.iss
```
Коды выхода: 0 ок, 1 параметры/фатально, 2 компиляция не удалась. **Inno 7**: синтаксис расширен, не сменён — `--output-dir=<path>` (`-o<path>`), `--output-filename=<name>` (`-f<name>`), `--quiet` (`-q`), `--quiet-progress` (`-qp`), `--signtool=<name>=<cmd>`, `--no-signing`, `--messages-jsonl`, `--preprocess`; «Short options accept either `-` or `/` as a prefix and are case-insensitive»; определения — `--define=<name>[=<value>]` / `-d<name>[=<value>]` → команда выше должна работать и на 7 (не проверено).

**Ассоциация .pdf и «приложение по умолчанию»:** страница Default Programs помечена «This topic does not apply for Windows 10» (API deprecated с Windows 8: `SetAppAsDefaultAll` — «prior to Windows 8», `QueryAppIsDefault` — «no longer supported as of Windows 8»); под UAC «defaults must be registered on a per-user level»; «The choice of default should be user driven. An application should never reclaim a default without asking the user»; тезис «с Windows 10 программа не может сама назначить себя» — из блога Windows build 10122, на который ссылается страница. После записи в реестр — `SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_DWORD | SHCNF_FLUSH, nullptr, nullptr);` (+ `Sleep(1000)`); ProgID создавать при установке и удалять при деинсталляции, `.ext` «Leave the file type mappings unchanged at uninstall time». Открыть нужную страницу настроек: `ms-settings:defaultapps?registeredAppUser=PdfStudio` (имя = значение в `HKCU\Software\RegisteredApplications`; Windows 11 21H2/22H2 с CU 2023-04, 23H2+; экранировать через `Uri::EscapeComponent`; также `registeredAppMachine=`, `registeredAUMID=`).

**Иконка:** `.ico` (многоразмерный), `res/app.rc` со строкой `IDI_ICON1 ICON "myappico.ico"`, `.rc` в источники `qt_add_executable(app main.cpp ${app_icon_resource_windows})`; та же иконка — `DefaultIcon` ProgID и `SetupIconFile`.

### 3.8 Кодировки и пути

- **`/utf-8`** «equivalent to specifying `/source-charset:utf-8 /execution-charset:utf-8`», включает `/validate-charset`; без BOM «assumes that the source file is encoded in the current user code page» (здесь 1252). В CMake: `add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")` (+ `/Zc:__cplusplus`; `/permissive-` уже подразумевается `/std:c++20`).
- **`std::filesystem::path`**: `u8path` deprecated в C++20; на Windows `path::value_type` = `wchar_t`; использовать `path(u8"…")`/`path(std::u8string)`; конструктор из `std::string` трактует байты в ANSI (1252) — `/utf-8` на это не влияет.
- **Qt ↔ std**: `QFile(const std::filesystem::path&)`, `setFileName(path)`, `filesystemFileName()` — с 6.0; конструктор explicit с 6.9; обратно — `QString::fromStdWString(p.native())`; `QT_NO_CAST_FROM_ASCII`. MuPDF принимает UTF-8 `const char*`; надёжнее `fz_open_document_with_stream(fz_open_file_w(ctx, wchar_path))`.
- **cl.exe по-русски**: сообщения локализованы; CMake сам пишет `msvc_deps_prefix` (§3.1); для ccache — `CCACHE_MSVC_DEP_PREFIX`. Вывод cl идёт в кодовой странице консоли (65001 → UTF-8 в файле; OEM 850 — кириллица теряется).
- **PowerShell 5.1**: «If you need to use non-Ascii characters in your scripts, save them as UTF-8 with BOM. Without the BOM, Windows PowerShell misinterprets your script as being encoded in the legacy "ANSI" codepage»; `Out-File` и `>`/`>>` — UTF-16LE; `Set-Content`/`Add-Content` — `Default` (ANSI) без `-Encoding`; `New-Item -Type File -Value` — UTF-8 без BOM; `$OutputEncoding` — только обмен с внешними программами. Пролог скриптов очереди:
  ```powershell
  [Console]::OutputEncoding = [Text.Encoding]::UTF8; $OutputEncoding = [Text.Encoding]::UTF8
  $PSDefaultParameterValues['*:Encoding'] = 'utf8'; chcp 65001 | Out-Null
  $env:PYTHONUTF8 = '1'; $env:PYTHONIOENCODING = 'utf-8'
  ```
- Tesseract/MuPDF-OCR открывают traineddata узким `fopen` → tessdata в ASCII-путь (§5.3).

### 3.9 C++17 или C++20 на MSVC 14.44

`/std:c++20` «Enables C++20 standard-specific features and behavior. Enables the standard conformance mode provided by `/permissive-`»; `/std:c++23preview` — «Available starting in Visual Studio 2022 version 17.13 Preview 4»; `/std:c++latest` — черновики. Текущая страница `/std` версию появления `/std:c++20` не называет (исторически 16.11). Сноска 20abi (страница обновлена 2026-10-16): «`<format>`, the formatting parts of `<chrono>` … and the range factories and range adaptors from `<ranges>` … are only available under `/std:c++latest`» — **на 14.44 устарела**: под `-std:c++20` всё компилируется и работает (проверено дважды). **Вывод: C++20 безопасен** (`CMAKE_CXX_STANDARD 20`, `CMAKE_CXX_STANDARD_REQUIRED ON`, `CMAKE_CXX_EXTENSIONS OFF`); не использовать modules и `/std:c++latest`. Qt 6.11/6.12 требует минимум C++17; QtWebEngine 6.12 требует C++20 и MSVC ≥ 14.36; qpdf 12 — C++20.

### 3.10 Запуск сборки из очереди `claude -p`

Документация Claude Code (tools-reference, env-vars, headless; требует Claude Code ≥ v2.1.285):
- Foreground Bash/PowerShell: `BASH_DEFAULT_TIMEOUT_MS` = 120 000 (2 мин); модель может поднять до `BASH_MAX_TIMEOUT_MS` = 600 000; «the effective ceiling is the larger of the two». По истечении команда **переводится в фон** («Command did not complete within its 120s timeout and was moved to the background»), если не `CLAUDE_CODE_DISABLE_BACKGROUND_TASKS=1` и не `--bare`.
- Лимит фоновых команд в unattended-сессиях (`-p`, SDK, CI): **30 мин** по умолчанию (или `timeout` из `run_in_background`), **максимум 2 ч**; команда, ушедшая в фон по таймауту, получает 30 мин «from the move»; **в `-p` с текстовым промптом (без `--input-format stream-json`) оба дефолта — 10 мин**. Поднять: `BASH_DEFAULT_TIMEOUT_MS` > 1 800 000 заменяет 30-минутный дефолт (в `-p` любое значение > 600 000 заменяет 10-минутный); `BASH_MAX_TIMEOUT_MS` > 7 200 000 поднимает 2-часовой максимум.
- После финального ответа `-p` ждёт фоновую работу до `CLAUDE_CODE_PRINT_BG_WAIT_CEILING_MS` (600 000 мс; `0` — бесконечно), **но** «A running background command that the main conversation started holds the run open past it» — ceiling важен для сабагентов/Monitor.
- Вывод команды: inline до ~30 000 символов; **сверх — путь к файлу в каталоге сессии (обрезка после 64 МиБ) + превью 2 000 символов**; `BASH_MAX_OUTPUT_LENGTH` (default 30 000, максимум 150 000) — окно обратного чтения; `bashOutputMaxChars` (до 128 000) — inline-потолок. Сборку всё равно писать в лог-файл и возвращать `Select-String -Pattern 'error|warning C|FAILED' | Select -Last 40`.
- `CLAUDE_CODE_MAX_WEB_SEARCHES_PER_SESSION` — по умолчанию 200 (в `-p` лимит на ход) — именно он исчерпывался при проверках.

Шаблон для очереди (`settings.local.json` → `env`): `BASH_DEFAULT_TIMEOUT_MS=1800000`, `BASH_MAX_TIMEOUT_MS=7200000`, `CLAUDE_CODE_PRINT_BG_WAIT_CEILING_MS=0`, `VCPKG_MAX_CONCURRENCY=10`, `CCACHE_MSVC_DEP_PREFIX=Примечание: включение файла:`, `QT_QPA_PLATFORM=offscreen`, `QT_QPA_FONTDIR=C:/Windows/Fonts`, `QT_FORCE_STDERR_LOGGING=1`, `PYTHONUTF8=1`. Скрипт `tools/build.ps1`: (1) пролог UTF-8; (2) `cmd /c vcvars64 && cmake --preset msvc-release` с логом; (3) `cmake --build --preset msvc-release -j 10 2>&1 | Tee-Object build/build.log`; (4) `ctest --preset msvc-release --output-on-failure`; инкрементальность — Ninja (`ninja -d explain`). Первый `vcpkg install` (>10 мин) — отдельной фоновой командой с `timeout` 7 200 000 мс; повторные — из бинарного кэша.
**Грабли:** `cd`/`pushd` внутри команды, ушедшей в фон, не сохраняется («Session cwd remains <dir>»); параллельные сессии очереди и Ollama конкурируют за 15,7 ГБ — не запускать две сборки одновременно; свежему клону нужен `settings.local.json` (без него `claude -p` всё запрещает — memory).

### 3.11 Прочие инструменты «без кликов» (winget, проверено 2026-10-21)

| Пакет | Версия | Тип | Примечание |
|---|---|---|---|
| Mozilla.sccache | 0.18.0 | portable zip (2026-09-14) | `sccache-v0.18.0-x86_64-pc-windows-msvc.zip` |
| Ccache.Ccache | 4.14.1 | portable zip (2026-09-27) | upstream 4.14.2 (2026-10-18) |
| Kitware.CMake / Ninja-build.Ninja | 4.4.4 / 1.13.2 | portable zip | не обязательны |
| 7zip.7zip | 26.04 | exe | тип установщика/права не проверялись; для `.7z` Qt есть `py7zr` в venv aqt |
| TheDocumentFoundation.LibreOffice | 26.8.1.1 | MSI (2026-10-08), **`Scope: machine`** | per-user установка не проверена (§5.1) |
| UB-Mannheim.TesseractOCR | 5.4.0.20240606 | NSIS (2024-06-18) | **устарел**: актуален `tesseract-ocr-w64-setup-5.5.3.20260724.exe` на GitHub UB-Mannheim; не нужен — OCR из MuPDF |
| Python.Python.3.13 / 3.12 | 3.13.15 / 3.12.10 | burn | per-user по умолчанию |
| FireDaemon.OpenSSL | 4.0.3 (есть 3.5.0–3.5.3, 3.6.0–3.6.1) | exe, scope machine (`/exenoui /qn /norestart REBOOT=ReallySuppress ADJUSTSYSTEMPATHENV=yes`) | zip 3.5.9 (x86/x64/ARM64) `https://download.firedaemon.com/FireDaemon-OpenSSL/openssl-3.5.9.zip` — без админа; в комплекте `bin`, `include`, `lib` (`libcrypto.lib`/`libssl.lib`), динамическая линковка, HYBRIDCRT под VS 2026 |
| ShiningLight.OpenSSL.Dev | 3.5.4, 3.6.0–3.6.2, 4.0.x | MSI/exe | состав `lib/VC/x64/MD*` не подтверждён |
| QPDF.QPDF | 12.4.2 | nullsoft exe, `Scope: machine` | брать zip `qpdf-12.4.2-msvc64.zip` (28 183 147 Б; `qpdf-12.4.2.sha256`) |
| pdfcpu.pdfcpu | 0.16.3 | portable zip, alias `pdfcpu.exe` | без админа; `pdfcpu_0.16.3_Windows_x86_64.zip` 7 071 702 Б (sha256 `282cb436…d55`) |
| SumatraPDF.SumatraPDF | 3.6.1 | installer exe (11 075 960 Б) | portable только с сайта: `https://www.sumatrapdfreader.org/dl/rel/3.6.1/SumatraPDF-3.6.1-64.zip` (10 031 830 Б) |
| LLVM.LLVM / Cppcheck.Cppcheck | 23.1.4 / 2.22.0 | wix MSI | см. §3.5 |
| Temurin 21 JRE | 21.0.12.1+1 | zip `OpenJDK21U-jre_x64_windows_hotspot_21.0.12.1_1.zip` (48 999 141 Б, Adoptium API) | запасной Java для veraPDF, без админа |

## 4. Цифровые подписи PAdES на C++ / OpenSSL / Windows

Метод исходной проверки: ETSI EN 319 142-1 извлечён локально через pypdf; OpenSSL — ветка `openssl-3.5` на GitHub и docs.openssl.org; Microsoft Learn; Adobe devnet-docs (PrefRef, DigSigDC); MuPDF — тег 1.28.5, исходники прогрепаны; RFC; pyHanko; GitHub API для DSS/PDFBox/pyHanko; локально — OpenSSL 3.5.6 из Git, живые запросы к четырём публичным TSA, HEAD-запросы к trustlist.adobe.com.

### 4.0 Резюме и ключевые решения

| Вопрос | Рекомендация | Почему |
|---|---|---|
| SubFilter | По умолчанию **`ETSI.CAdES.detached`**, опция `adbe.pkcs7.detached` для старых читалок | Acrobat допускает оба (`aSignFormat`: `adbe.pkcs7.detached`, `adbe.pkcs7.sha1`, `adbe.x509.rsa_sha1`, `ETSI.CAdES.detached`); ETSI запрещает PKCS#1/PKCS#7-профили ISO 32000-1 §12.8.3.2/12.8.3.3 (EN 319 142-1 §6.3 i)) |
| Уровни PAdES | B-B и B-T сначала, B-LT (DSS) вторым, B-LTA третьим | Acrobat кладёт LTV-данные «в соответствии с Part 4 ETSI TS 102 778» (PAdES-LTV = DSS/VRI + DocTimeStamp) — паритет требует B-LT |
| CMS-движок | **OpenSSL 3.5 LTS** (`CMS_*`) для .pfx и для всей проверки; для ключей Windows/смарт-карт — внешняя подпись `NCryptSignHash` (§4.4) | `CMS_NO_SIGNING_TIME` появился только в 3.5.0; `CMS_CADES` — с 3.0.0; `CMS_final_digest`/`CMS_SignedData_verify` — с 3.2 |
| Хэш ByteRange | Свой потоковый SHA-256, не `pkcs7-openssl.c` MuPDF | Помощник MuPDF: `PKCS7_add_signature(..., EVP_sha1())`, только `contentType`, доверие — только `AdobeCA.p7c` |
| Что брать из MuPDF | Плейсхолдер `/Contents`+`/ByteRange`, инкрементальная запись (`pdf_signature_set_value`, `pdf_write_digest`, `complete_signatures`), интерфейсы `pdf_pkcs7_signer/verifier`, генератор внешнего вида, `pdf_validate_signature` как база | Проверено по исходникам 1.28.5 |
| Корни доверия | Windows ROOT через `org.openssl.winstore:` (OpenSSL ≥ 3.2) + собственный список доверенных, **по умолчанию выкл.** (как `iMSStoreTrusted=0` в Acrobat) | Локально: `openssl list -store-loaders` → `org.openssl.winstore @ default` |
| TSA | RFC 3161 по HTTP (`OSSL_HTTP_transfer` или `QNetworkAccessManager`); тесты — локальный `openssl ts -reply` | Живьём проверены freetsa.org, DigiCert, Sectigo, GlobalSign (все `Status: Granted`) |
| Резерв `/Contents` | **16 КиБ** и для подписи со штампом, и для DocTimeStamp | Токены: 4,6 КБ (freetsa), 6,0 КБ (DigiCert), 6,6 КБ (Sectigo), **7,7 КБ (GlobalSign)** — 8 КиБ впритык |

### 4.1 Уровни PAdES — ETSI EN 319 142-1 V1.2.1 (2024-01)

Источник: `etsi.org/deliver/etsi_en/319100_319199/31914201/01.02.01_60/en_31914201v010201p.pdf` (24 стр.). Каталог содержит `01.00.00_20` (2015-06-29), `01.01.00_30`, `01.01.01_60` (2016-04-12), `01.02.00_20` (2023-10-13), `01.02.01_60` (выложена 2024-06-20) и **черновик `01.03.00_20` от 2026-08-14** (`_20` — проект на утверждении).

Уровни (§6.1): B-B — подписанные (и часть неподписанных) атрибуты при создании; B-T — добавлен доверенный токен времени (signature-time-stamp **или** document-time-stamp, §6.3 n)); B-LT — весь материал проверки в документе (DSS); B-LTA — document-time-stamp поверх DSS (и далее периодически).

Таблица 1 (§6.3), требования «все уровни» одинаковы для B-B/B-T/B-LT/B-LTA:

| Элемент | B-B | B-T | B-LT | B-LTA | Примечание |
|---|---|---|---|---|---|
| `SignedData.certificates` (сертификат подписанта) | обяз. | обяз. | обяз. | обяз. | a) включать подписанта; b) желательно всю цепочку до CA из Trusted List |
| `content-type` = id-data | обяз. (все уровни) | | | | c) |
| `message-digest` | обяз. (все уровни) | | | | |
| ESS `signing-certificate` **или** `signing-certificate-v2` | ровно один из двух, **на всех уровнях** | | | | e) выбор по хэш-функции; f) предпочитать v2 |
| `signing-time` в CMS | **запрещён на всех уровнях** | | | | время — только в `/M` (g): UTC по ISO 32000-1 §7.9.4) |
| `/M` | обяз. (все уровни) | | | | |
| `/Contents`, `/Filter`, `/ByteRange`, `/SubFilter` | обяз. (все уровни) | | | | h) DER SignedData = CAdES по EN 319 122-1; k) ByteRange — весь файл кроме `/Contents`; l) `SubFilter` = `ETSI.CAdES.detached`; j) верификатор может подменить handler из `/Filter` |
| `/Cert` | **запрещён на всех уровнях** | | | | |
| `/Reason` | условно (все уровни) | | | | d), m): нельзя вместе с `commitment-type-indication` или `signature-policy-identifier` |
| `/Location`, `/Name`, `/ContactInfo` | можно (все уровни) | | | | |
| `commitment-type-indication`, `signature-policy-identifier`, `signer-attributes-v2`, `content-time-stamp` | необяз. | | | | |
| signature-time-stamp / document-time-stamp | * | обяз. один из двух | обяз. | обяз. | o) DER; p) может быть несколько; q) B-B может резервировать место |
| DSS | * | * | **обяз.** (≥1) | **обяз.** | r)–u): все сертификаты вкл. якорь и сертификаты TSA, все CRL/OCSP, DER; v) **VRI не следует использовать** |
| document-time-stamp после DSS | — | — | — | **обяз.** (≥1) | w) можно несколько; x) перед ним включить весь материал проверки, в т.ч. для TSA предыдущих штампов; y) `SubFilter` = `ETSI.RFC3161` |

Прочее: MD5 запрещён, алгоритмы по ETSI TS 119 312 (§6.2.1); `/Extensions` в каталоге: `/ESIC <</BaseVersion /1.7 /ExtensionLevel 1>>` (DSS), `/ExtensionLevel 2` (CAdES.detached) либо `/ADBE <</BaseVersion /1.7 /ExtensionLevel 8>>` (§5.6); при шифровании hex-строки `/Contents` не шифруются (§5.5). Требование «один SignerInfo» (§4.1 a)) дословно не перепроверено — оно же в EN 319 122-1 §5.1.

### 4.2 Что делает Acrobat (PrefRef `Security.html`, DigSigDC `planning.html`/`standards.html`)

- **Формат**: `aSignFormat` по умолчанию `adbe.pkcs7.detached`; допустимые `adbe.pkcs7.detached`, `adbe.pkcs7.sha1`, `adbe.x509.rsa_sha1`, `ETSI.CAdES.detached`; GUI (10.0+): Preferences > Security > Advanced Preferences > Creation > «Default Signature Signing Format» (названия пунктов «PKCS#7 Detached / CAdES-Equivalent» в документации не найдены — по памяти).
- **Хэш**: `aSignHash` — SHA1 до 9.0, **SHA256 с 9.1**; поддерживаются MD5, SHA1, SHA256, RIPEMD160, SHA384, SHA512; с 11.x ECDSA P-256/P-384/P-521 с SHA-256/384/512; DSA — только SHA1 и только `adbe.pkcs7.detached`. **RSA-PSS**: ни planning.html, ни standards.html, ни PrefRef не упоминают — не подтверждено.
- **LTV включён по умолчанию**: `bIsEnabled` = 1 с 9.1; `iAutoAddLTV` = 1 (**«спросить, если данные слишком велики»**: 0 — никогда, 2 — всегда; порог — 10 % размера PDF + 10 КБ); `iMaxRevInfoArchiveSize` = 1500 КБ; `iUseArchivedRevInfo` = 2; с 10.1 OCSP-ответы встраиваются всегда. Про `adbe-revocationInfoArchival` документация молчит.
- **Штамп времени**: TSA по умолчанию не задан; `bUseTSAsSigningTime` = 0 (11.0+) — флаг **показа** времени штампа во внешнем виде; `iReqRevCheck` для TSA = 2.
- **Проверка**: `bValidateOnOpen` = 1; `bShowWarningForChanges` = 1 — синий значок «i» на валидной подписи при изменениях после подписи; `bFollowURIsFromAIA` = 0 («The default does not allow phone-home capability. CRLdps and OCSP AIA extensions do allow following URIs» — цепочка по AIA caIssuers не докачивается, CRL DP и OCSP используются); `iReqRevCheck`: OCSP = 2, CRL = 1, при подписании = 0; `iMaxClockSkew` = 65 мин.
- **Доверие Windows**: `iMSStoreTrusted` по умолчанию `0x00`, `0x60` — «Validating Signatures», `0x62` — «Validating Certified Documents and Signatures»; `bCertStoreImportEnable` = 0.
- **AATL/EUTL**: `bLoadSettingsFromURL` (AATL: «9.x и ранее 0; 10: 1»; EUTL: 1, с 11.0.06), `bAskBeforeInstalling` = 1, **`iCheckEvery` = 1 209 600 с (2 недели; допустимые 604800/1209600/2419200/7257600)** по PrefRef (planning.html противоречиво пишет 604800 и «every 30 days»). EUTL — «более 200 TSP». Публичные точки (HEAD 2026-10-21): `https://trustlist.adobe.com/tl12.acrobatsecuritysettings` (PDF, 423 333 Б, Last-Modified 2026-10-16) и `https://trustlist.adobe.com/eutl12.acrobatsecuritysettings` (4 550 731 Б, 2026-10-02); привязка именно этих URL к Acrobat документацией не подтверждена (в PrefRef только `tLoadSettingsURL`).
- Standard vs Pro (compare-versions): e-sign/подписание — в обоих; **OCR, Redaction, Compare — только Pro**; про Certify страница молчит.
- Статусы UI Acrobat — точные формулировки не получены (helpx 403; `sigval.html` пустой).

**Писать самим:** генератор словаря подписи с переключателем SubFilter; все атрибуты CAdES-B; `/M`; запрет `signing-time`; `/Extensions`. **Грабли:** `/Reason` исключает `commitment-type-indication`/`signature-policy-identifier`; `/Cert` запрещён; `signing-time` OpenSSL добавляет по умолчанию — `CMS_NO_SIGNING_TIME` (**только ≥ 3.5**; на 3.4 и старше — `CMS_NOATTR` + ручные атрибуты); один SignerInfo.

### 4.3 Механика подписи в PDF (по MuPDF 1.28.5)

Порядок (ISO 32000-1 §12.8.1): плейсхолдер `/ByteRange [0 0 0 0]` + `/Contents <00…>` фиксированной длины → инкрементальное сохранение → патч ByteRange `[0, a, b, len-b]` (дыра = hex-строка вместе с `<…>`) → хэш двух диапазонов → CMS → hex в плейсхолдер, остаток нулями.

Как в MuPDF:
- `source/pdf/pdf-form.c: pdf_signature_set_value()` пишет в `/V`: `/ByteRange` (4 числа), `/Contents` длиной `signer->max_digest_size()` (нули), `/Filter /Adobe.PPKLite`, **`/SubFilter /adbe.pkcs7.detached`** (жёстко), `/Type /Sig`, `/M`, `/Reference [<</Data Root /TransformMethod /FieldMDP /Type /SigRef /TransformParams <</Action … /Fields … /V /1.2>>>>]` по `/Lock` (или по XFA). Комментарий в коде: «/Filter должен быть первым ключом после Contents — по нему pdf-write.c находит байтовые диапазоны». **`/Reason`, `/Location`, `/Name`, `/ContactInfo` не пишутся** (reason/location идут только в текст внешнего вида через `pdf_format_signature_info`, `pdf-signature.c:295-425`).
- `pdf_sign_signature_with_appearance()` — `enact_sig_locking` (ставит `Ff` ReadOnly заблокированным полям), `AcroForm/SigFlags |= 3`, затем `pdf_signature_set_value`. `pdf_write_digest()`: `fz_open_range_filter` по ByteRange → `signer->create_digest(ctx, signer, in, digest, len)` (ошибка, если вернул 0 или больше `len`) → `%02x` с дополнением нулями.
- `source/pdf/pdf-write.c`: `presize_unsaved_signature_byteranges()` (пары `INT_MAX`); `complete_signatures()` ищет `fz_memmem` маркеры **`/ByteRange`, `/Contents`, `/Filter` строго в этом порядке**, иначе «Failed to determine byte ranges while writing signature»; вывод обязан поддерживать `fz_stream_from_output` («Can't write pdf that has unsaved sigs to a fz_output unless it supports fz_stream_from_output!»; `pdf-write.c:2941-2942`). Инкрементальность библиотека не форсирует — `mutool sign` сам ставит `do_incremental = 1` (`pdfsign.c:381-385`). `pdf-xref.c: ensure_incremental_xref()` — «After a signing, any further document changes require a new increment».
- Интерфейсы (`include/mupdf/pdf/form.h`, дословно):
```c
typedef size_t (pdf_pkcs7_max_digest_size_fn)(fz_context *ctx, pdf_pkcs7_signer *signer);
typedef int (pdf_pkcs7_create_digest_fn)(fz_context *ctx, pdf_pkcs7_signer *signer, fz_stream *in, unsigned char *digest, size_t digest_len);
struct pdf_pkcs7_signer { keep; drop; get_signing_name; max_digest_size; create_digest; };
typedef pdf_signature_error (pdf_pkcs7_check_certificate_fn)(fz_context *ctx, pdf_pkcs7_verifier *verifier, unsigned char *signature, size_t len);
typedef pdf_signature_error (pdf_pkcs7_check_digest_fn)(fz_context *ctx, pdf_pkcs7_verifier *verifier, fz_stream *in, unsigned char *signature, size_t len);
struct pdf_pkcs7_verifier { drop; check_certificate; check_digest; get_signatory; };
```
`create_digest` получает поток, уже ограниченный ByteRange, и возвращает **готовый DER CMS**. Публичный API (`form.h`):
```c
void pdf_sign_signature(ctx, widget, signer, appearance_flags, fz_image *graphic, const char *reason, const char *location);   // form.h:301
void pdf_sign_signature_with_appearance(ctx, widget, signer, int64_t date, fz_display_list *);                                 // form.h:279
fz_display_list *pdf_signature_appearance_signed(ctx, rect, lang, img, left_text, right_text, include_logo); *_unsigned(ctx, rect, lang);
pdf_preview_signature_as_display_list/pixmap(ctx, w, h, lang, signer, flags, graphic, reason, location);
pdf_signature_error pdf_check_widget_digest / pdf_check_widget_certificate(ctx, verifier, widget);                            // form.h:270-271
pdf_signature_get_widget_signatory, pdf_signature_format_distinguished_name, pdf_count_signatures, pdf_clear_signature;
int pdf_signature_incremental_change_since_signing(ctx, doc, sig);                                                             // crypt.h:95
fz_stream *pdf_signature_hash_bytes(ctx, doc, signature); size_t pdf_signature_contents(ctx, doc, signature, char **contents); // pdf-form.c — «ось» своего валидатора
```
Флаги `PDF_SIGNATURE_SHOW_LABELS=1|DN=2|DATE=4|TEXT_NAME=8|GRAPHIC_NAME=16|LOGO=32` (= `PDF_SIGNATURE_DEFAULT_APPEARANCE`, `form.h:282-287`); ошибки `PDF_SIGNATURE_ERROR_{OKAY,NO_SIGNATURES,NO_CERTIFICATE,DIGEST_FAILURE,SELF_SIGNED,SELF_SIGNED_IN_CHAIN,NOT_TRUSTED,NOT_SIGNED,UNKNOWN}` (`:203-211`). `pkcs7_openssl_read_pfx(_from_buffer)`, `pkcs7_openssl_new_verifier`, `pkcs7_openssl_check_digest/check_certificate/distinguished_name` (`helpers/pkcs7-openssl.h:44-49`).

Размеры: CAdES-B RSA-2048 + 1 CA = 2781 байт, ECDSA P-256 = 2384; токены TSA: локальный 2868, freetsa 4645, DigiCert 6006, Sectigo 6635, **GlobalSign 7660**. Рекомендация: **16 КиБ (32768 hex)** для `/Contents` подписи со штампом и цепочкой, **16 КиБ и для DocTimeStamp**.

**Писать самим:** свой `pdf_pkcs7_signer`/`verifier`; патч словаря после `pdf_sign_signature` (сменить `/SubFilter`, добавить `/Reason`/`/Location`/`/ContactInfo`, `/Extensions`) **до** сохранения; отдельный проход «инкрементальная секция без подписи поля» для DSS и DocTimeStamp. **Грабли:** `max_digest_size` в помощнике `pkcs7-openssl.c` реализован «сухим прогоном» (`signer_create_digest(ctx, signer, NULL, NULL, 0)`), ядро лишь вызывает `signer->max_digest_size` — наш signer должен возвращать константу (иначе со смарт-картой лишний PIN); порядок ключей `/ByteRange` → `/Contents` → `/Filter` обязателен.

### 4.4 CMS SignedData через OpenSSL 3

Релизы на 2026-10-21 (openssl-library.org/source): **3.5.9 LTS (29 Sep 2026, поддержка до 08 Apr 2030)**, 4.0.3 (до 14 May 2027), 3.6.5 (до 01 Nov 2026), 3.4.8 (до 22 Oct 2026), 4.1.0-beta2 (09 Oct 2026). Брать 3.5 LTS.

Флаги (`include/openssl/cms.h.in`, ветка 3.5): `CMS_NO_ATTR_VERIFY 0x8`, `CMS_NOINTERN 0x10`, `CMS_NO_SIGNER_CERT_VERIFY 0x20` (= `CMS_NOVERIFY`), `CMS_DETACHED 0x40`, `CMS_BINARY 0x80`, `CMS_NOATTR 0x100`, `CMS_NOSMIMECAP 0x200`, `CMS_NOCRL 0x2000`, `CMS_PARTIAL 0x4000`, `CMS_REUSE_DIGEST 0x8000`, `CMS_USE_KEYID 0x10000`, `CMS_KEY_PARAM 0x40000`, `CMS_CADES 0x100000`, `CMS_NO_SIGNING_TIME 0x400000`. История: `CMS_CADES` и CAdES-BES — 3.0.0 («Add CAdES-BES signature scheme and attributes support (RFC 5126) to CMS API»); `CMS_final_digest`, `CMS_SignedData_verify` — 3.2; **`CMS_NO_SIGNING_TIME` — 3.5.0** («Changes between 3.4 and 3.5.0 [8 Apr 2025]»).

Минимальный CAdES-B (проверено на 3.5.6; man `openssl-cms`: `-cades` «add an ESS signingCertificate or ESS signingCertificateV2 signed-attribute», «ESS signingCertificate only allows for SHA-1» → при SHA-256 будет V2; `-no_signing_time` — «Exclude the signing time from signed attributes»):
```bash
openssl cms -sign -binary -cades -nosmimecap -no_signing_time -md sha256 \
  -signer signer.crt -inkey signer.key -certfile ca.crt -in ranges.bin -outform DER -out sig.p7s
openssl cms -verify -cades -binary -inform DER -in sig.p7s -content ranges.bin -CAfile ca.crt -purpose smimesign
```
API-эквивалент:
```c
CMS_ContentInfo *cms = CMS_sign(NULL, NULL, chain, NULL, CMS_DETACHED|CMS_BINARY|CMS_PARTIAL|CMS_NOSMIMECAP|CMS_CADES|CMS_NO_SIGNING_TIME);
CMS_SignerInfo *si = CMS_add1_signer(cms, cert, pkey, EVP_sha256(), CMS_DETACHED|CMS_BINARY|CMS_NOSMIMECAP|CMS_CADES|CMS_NO_SIGNING_TIME|CMS_PARTIAL);
/* свои signed-атрибуты: int CMS_signed_add1_attr_by_OBJ(CMS_SignerInfo *si, const ASN1_OBJECT *obj, int type, const void *bytes, int len); */
CMS_final(cms, data_bio, NULL, CMS_DETACHED|CMS_BINARY);
/* или по готовому хэшу (с 3.2): int CMS_final_digest(CMS_ContentInfo *cms, const unsigned char *md, unsigned int mdlen, BIO *dcont, unsigned int flags); */
int len = i2d_CMS_ContentInfo(cms, &der);
```
- RSA-PSS: `CMS_KEY_PARAM` + `EVP_PKEY_CTX *CMS_SignerInfo_get0_pkey_ctx(si)` → `EVP_PKEY_CTX_set_rsa_padding(..., RSA_PKCS1_PSS_PADDING)` (поведение в man не описано).
- Штамп времени — unsigned-атрибут после финализации: `CMS_unsigned_add1_attr_by_OBJ(si, OBJ_txt2obj("1.2.840.113549.1.9.16.2.14",1), V_ASN1_SEQUENCE, token_der, token_len)`; подпись — `ASN1_OCTET_STRING *CMS_SignerInfo_get0_signature(si)`.
- Проверка: `int CMS_verify(cms, certs, store, detached_data, out, flags)`; с 3.2 `BIO *CMS_SignedData_verify(sd, detached_data, scerts, store, extra, crls, flags, libctx, propq)`. Цель цепочки по умолчанию — `smimesign`. **Важно (CMS_verify.pod): при `CMS_NO_SIGNER_CERT_VERIFY` цепочка не проверяется «unless CMS_CADES is also set»; то же для `CMS_NO_ATTR_VERIFY`** → стадию «только целостность» делать без `CMS_CADES` (ESS проверить вручную), стадию «доверие» — с `CMS_CADES`.
- Сертификаты/CRL: `CMS_add1_cert`, `CMS_add1_crl`, `CMS_get1_certs`, `CMS_get1_crls`.

**Писать самим:** RAII-обёртку над CMS, BIO по ByteRange, набор атрибутов, парсер CMS для UI. **Грабли:** в Git Bash `MSYS_NO_PATHCONV=1` для `/CN=…`; `-out /dev/null` в mingw-сборке не работает.

### 4.5 Ключи: PKCS#12, хранилище Windows, CNG/смарт-карты

**PKCS#12:** `int PKCS12_parse(PKCS12 *p12, const char *pass, EVP_PKEY **pkey, X509 **cert, STACK_OF(X509) **ca)`; пароль — UTF-8, иначе ISO-8859-1. `openssl pkcs12 -export` в 3.x по умолчанию AES-256-CBC + PBKDF2, MAC SHA-256, 2048 итераций; `-legacy` грузит legacy-провайдер (RC2-40/3DES) — без него RC2-40 .pfx не читается. Windows `PFXImportCertStore` «поддерживает только два алгоритма: **TripleDES-SHA1 и AES256-SHA256**» (команды `openssl pkcs12 -keypbe AES-256-CBC -certpbe AES-256-CBC -macalg sha256 …`); `Export-PfxCertificate` по умолчанию `TripleDES_SHA1`. Тесты без UI: `PFXImportCertStore(PKCS12_NO_PERSIST_KEY|PKCS12_ALWAYS_CNG_KSP)` — ключ только в памяти процесса.

**Хранилище Windows и диалог выбора** (Microsoft Learn):
```cpp
HCERTSTORE st = CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0, CERT_SYSTEM_STORE_CURRENT_USER, L"MY");
PCCERT_CONTEXT c = CryptUIDlgSelectCertificateFromStore(st, hwnd /*NULL = desktop*/, L"Выбор сертификата", L"…", 0, 0, NULL); // cryptuiapi.h, Cryptui.lib; NULL = отмена
HCRYPTPROV_OR_NCRYPT_KEY_HANDLE h; DWORD spec; BOOL mustFree;
CryptAcquireCertificatePrivateKey(c, CRYPT_ACQUIRE_PREFER_NCRYPT_KEY_FLAG|CRYPT_ACQUIRE_WINDOW_HANDLE_FLAG, &hwnd, &h, &spec, &mustFree);
// spec == CERT_NCRYPT_KEY_SPEC -> NCRYPT_KEY_HANDLE; иначе legacy CSP (PREFER_NCRYPT падает обратно на CryptoAPI)
BCRYPT_PKCS1_PADDING_INFO pi{ BCRYPT_SHA256_ALGORITHM };
NCryptSignHash(h, &pi, hash, 32, NULL, 0, &cb, NCRYPT_PAD_PKCS1_FLAG);   // pbSignature=NULL -> размер
NCryptSignHash(h, &pi, hash, 32, sig, cb, &cb, NCRYPT_PAD_PKCS1_FLAG);   // PSS: BCRYPT_PSS_PADDING_INFO + NCRYPT_PAD_PSS_FLAG
```
- `CRYPT_ACQUIRE_WINDOW_HANDLE_FLAG` «Do not use this flag with CRYPT_ACQUIRE_SILENT_FLAG»; `NCRYPT_SILENT_FLAG` → `NTE_SILENT_CONTEXT`, если KSP нужен UI (PIN). `CRYPT_ACQUIRE_CACHE_FLAG` → `pfCallerFree = FALSE`. Освобождение: `NCryptFreeObject` / `CryptReleaseContext`, `CertFreeCertificateContext`.
- ECDSA: CNG возвращает подпись в формате IEEE P1363 (`r||s`) — подтверждено косвенно исходниками .NET (`ECDsaCng.SignVerify.cs`); в CMS нужна DER `ECDSA-Sig-Value` → `ECDSA_SIG_set0` + `i2d_ECDSA_SIG`.

**Корни Windows для OpenSSL:** вариант A — `X509_STORE_load_store(store, "org.openssl.winstore:")`: функция с **3.0** (CHANGES 3.0.0), загрузчик **winstore — с 3.2.0** (23 Nov 2023); `winstore_store.c`: `CertOpenSystemStoreW(0, L"ROOT")`, поиск только `CertFindCertificateInStore(..., CERT_FIND_SUBJECT_NAME, …)` (без subject → EOF, поэтому `storeutl` даёт 0 — норма); man `OSSL_STORE-winstore(7)`: «it is not possible to enumerate all available certificates in the store», единственный URI — `org.openssl.winstore:`; **Windows-ограничения (EKU override, name constraints, expiry override) не учитываются**. Вариант B — `CertEnumCertificatesInStore(hROOT, prev)` → `d2i_X509` → `X509_STORE_add_cert`.

**Подпись неэкспортируемым ключом (CNG) — два пути:**
- **Путь 1 — CryptMsg**: `CryptMsgOpenToEncode(X509_ASN_ENCODING|PKCS_7_ASN_ENCODING, CMSG_DETACHED_FLAG, CMSG_SIGNED, &info, NULL, NULL)`; `CMSG_SIGNER_ENCODE_INFO` имеет union `{hCryptProv; hNCryptKey; hBCryptKey}` («NCryptIsKeyHandle is called to determine the union choice»), `HashAlgorithm`, `rgAuthAttr`/`rgUnauthAttr`, `HashEncryptionAlgorithm` (при `CMSG_SIGNER_ENCODE_INFO_HAS_CMS_FIELDS`); «content type OID and the hash of the message … are automatically added by the system». ESS `signingCertificateV2` кодировать вручную по RFC 5035: `SigningCertificateV2 ::= SEQUENCE { certs SEQUENCE OF ESSCertIDv2, policies … OPTIONAL }`, `ESSCertIDv2 ::= SEQUENCE { hashAlgorithm AlgorithmIdentifier DEFAULT {id-sha256}, certHash OCTET STRING, issuerSerial OPTIONAL }`, OID `1.2.840.113549.1.9.16.2.47`; только signed-атрибут, одна инстанция, одно значение.
- **Путь 2 — OpenSSL PKCS7-API**: `PKCS7_SIGNER_INFO` в `pkcs7.h.in` 3.5 публична (`auth_attr`, `enc_digest` «actually signature», `digest_enc_alg`, `pkey`), `DECLARE_ASN1_ITEM(PKCS7_ATTR_SIGN)`/`PKCS7_ATTR_VERIFY`, `PKCS7_add_signed_attribute`, `PKCS7_add_signature`, `PKCS7_signatureVerify`, `PKCS7_dataFinal`. Схема: атрибуты → `ASN1_item_i2d(si->auth_attr, …, PKCS7_ATTR_SIGN)` → хэш → `NCryptSignHash` → `ASN1_STRING_set(si->enc_digest, …)`; покрыть тестом `cms -verify -cades`.
- **Не брать engine `capi`**: `engines/e_capi.c` — только `CryptSignHash` (legacy CSP), RSA и DSA (DSA — 20-байтовый digest, `CALG_SHA1`), ECDSA нет; ENGINE API deprecated с 3.0.0; в OpenSSL из Git его нет.

### 4.6 RFC 3161 (штампы времени)

OpenSSL `ts.h` (3.5): `TS_REQ_set_version/…/TS_REQ_set_cert_req`, `TS_RESP_get_status_info`, `TS_STATUS_INFO_get0_status` (0 granted, 1 grantedWithMods), `TS_RESP_get_token`, `TS_RESP_get_tst_info`, `TS_TST_INFO_get_time`; `TS_REQ_to_TS_VERIFY_CTX`, `TS_VERIFY_CTX_set0_store/set0_certs/set0_imprint/set0_data`, `TS_VERIFY_CTX_set_flags`, `TS_RESP_verify_response/verify_token` (`TS_VERIFY_CTX_set_store/…` — `OSSL_DEPRECATEDIN_3_4_FOR`). Флаги `TS_VFY_SIGNATURE=1, VERSION=2, POLICY=4, IMPRINT=8, DATA=16, NONCE=32, SIGNER=64, TSA_NAME=128`.
HTTP: `BIO *OSSL_HTTP_transfer(OSSL_HTTP_REQ_CTX **prctx, const char *server, const char *port, const char *path, int use_ssl, const char *proxy, const char *no_proxy, BIO *bio, BIO *rbio, OSSL_HTTP_bio_cb_t bio_update_fn, void *arg, int buf_size, const STACK_OF(CONF_VALUE) *headers, const char *content_type, BIO *req, const char *expected_content_type, int expect_asn1, size_t max_resp_len, int timeout, int keep_alive)` (3.0+). RFC 3161: §2.3 — EKU `id-kp-timeStamping` «MUST be critical»; §3.4 — MIME `application/timestamp-query`/`application/timestamp-reply` (в прозе §3.4 опечатка «application/timestamp-response»; реальные TSA отдают `timestamp-reply` — быть терпимым); Appendix A — `id-aa-timeStampToken 1.2.840.113549.1.9.16.2.14`, messageImprint = хэш **значения подписи**; `id-ct-TSTInfo 1.2.840.113549.1.9.16.1.4`.

Публичные TSA (живые запросы 2026-10-21, `openssl ts -query -sha256 -cert` + curl, все `Status: Granted`, sha256, `application/timestamp-reply`):
- **freetsa.org**: `https://freetsa.org/tsr` (также TCP 318), сертификаты `https://freetsa.org/files/tsa.crt` (SHA-256 `8bfb0305bb64e2571ca507552ef3245cb1c2fee8728e0ff8689225081ea13467`) и `https://freetsa.org/files/cacert.pem` (`2151b61137ffa86bf664691ba67e7da0b19f98c758e3d228d5d8ebf27e044438`); SHA-1/224/256/384/512; «do not abuse»; **с 2026-03-16 новый сертификат на ECC P-384**, штампы действительны до февраля 2040; старый (2016–2026) недействителен; корень самоподписанный — добавлять явно; ответ 4645 байт.
- **DigiCert** `http://timestamp.digicert.com` — политика `2.16.840.1.114412.7.1`, 6006 байт.
- **Sectigo** `http://timestamp.sectigo.com` — `1.3.6.1.4.1.6449.2.1.1`, 6635 байт, «Sectigo Public Time Stamping Signer R37».
- **GlobalSign** `http://timestamp.globalsign.com/tsa/r6advanced1` — `1.3.6.1.4.1.4146.2.3.2.2`, 7660 байт.
- `http://rfc3161.ai.moda` — политика DigiCert, 6007 байт.

Офлайн-TSA для автотестов: `openssl ts -reply -config tsa.cnf …` (man openssl-ts 3.5: `serial` обязателен, `signer_digest`, `default_policy`, `digests`, `accuracy`, `ordering` (no), `tsa_name` (no), `ess_cert_id_chain` (no), `ess_cert_id_alg` (sha256); `-query -digest` по умолчанию SHA-256, nonce 64 бит, `-no_nonce`). Document-time-stamp (ETSI §5.4.3): `/Type /DocTimeStamp`, `/SubFilter /ETSI.RFC3161`, `/Contents` = TimeStampToken (RFC 3161 + RFC 5816), imprint = хэш ByteRange (весь документ кроме токена), `/V` = 0; **запрещены** `Cert, Reference, Changes, R, Prop_AuthTime, Prop_AuthType`; не следует писать `Name, M, Location, Reason, ContactInfo`; при оценке DocMDP наличие DocTimeStamp игнорируется. «Secure time» = время штампа.

### 4.7 LTV: DSS, VRI, OCSP, CRL (ETSI §5.4.2, §6.3 r–y)

DSS — словарь в каталоге с ключами `Type` (= DSS), `VRI`, `Certs` (косвенные ссылки на потоки с DER X.509), `OCSPs` (**тип `OCSPResponse` RFC 6960**, не BasicOCSPResponse), `CRLs`. Ключ VRI — «base-16-encoded (uppercase) SHA1 digest of the signature», где по §5.4.2.2 a) хэшируются «bytes … of the complete hexadecimal string in the entry with the key Contents» — текст ETSI говорит о hex-строке; практика реализаций отличается, поведение Acrobat не проверено (в baseline VRI «should not be used»). VRI-словари — только в инкрементальных секциях после подписи. «DocMDP restrictions … shall not apply to incremental updates to a PDF document containing a DSS dictionary and associated VRI, Certs, CRLs and OCSPs» (§5.4.2.3), то же для DocTimeStamp (§5.4.3).

OpenSSL OCSP: `OCSP_cert_to_id` → `OCSP_REQUEST_new`/`OCSP_request_add0_id`/`OCSP_request_add1_nonce` → POST `application/ocsp-request` через `OSSL_HTTP_transfer` (`OCSP_sendreq_bio` сохранён, «does not support setting a timeout»; `OCSP_REQ_CTX`/`OCSP_sendreq_nbio` deprecated с 3.0) → `d2i_OCSP_RESPONSE` → `OCSP_response_status` → `OCSP_response_get1_basic` → `int OCSP_basic_verify(bs, certs, st, flags)` (флаги `OCSP_NOVERIFY, TRUSTOTHER, NOCHAIN, NOCHECKS, NOEXPLICIT, NOSIGS, NOINTERN, PARTIAL_CHAIN`; учитывает `id-pkix-ocsp-nocheck`) → `OCSP_resp_find_status(bs, id, &status, &reason, &rev, &thisupd, &nextupd)` → `int OCSP_check_validity(thisupd, nextupd, sec, maxsec)` (OCSP без `nextUpdate` → `maxsec`). CRL: `X509_V_FLAG_CRL_CHECK` (leaf) + `X509_V_FLAG_CRL_CHECK_ALL`, `X509_V_FLAG_EXTENDED_CRL_SUPPORT`, `X509_V_FLAG_USE_DELTAS`; `X509_V_FLAG_PARTIAL_CHAIN` — несамоподписанные сертификаты в store становятся якорями; `X509_VERIFY_PARAM_set_time` — момент проверки; `X509_V_FLAG_NO_CHECK_TIME`.

**Писать самим:** дедупликация, слияние DSS, BasicOCSPResponse→OCSPResponse, «ltvfix»; Acrobat «retroactive» — как pyHanko `--retroactive-revinfo` («treat CRLs and OCSP responses as valid infinitely far back into the past»).

### 4.8 Проверка подписи и статусы

1. Структура/покрытие ByteRange (4 числа, от 0, дыра = hex `/Contents`); MuPDF `pdf_signature_incremental_change_since_signing`: `changed = (doc->file_size > end_of_range || doc->num_incremental_sections > 0)`.
2. Целостность: `CMS_verify(..., CMS_DETACHED|CMS_BINARY|CMS_NO_SIGNER_CERT_VERIFY)` **без** `CMS_CADES`; ESS сверять самим.
3. Доверие: `X509_STORE` (winstore + свои + AATL/EUTL), `X509_VERIFY_PARAM_set_time` (штамп или `/M`), purpose `smimesign` либо свой; `PARTIAL_CHAIN` для доверенных промежуточных.
4. Отзыв: DSS/CMS или онлайн; revoked → недействительна; нет данных → неизвестно (аналог `iReqRevCheck`).
5. Штампы: signature-time-stamp (imprint = хэш подписи) и DocTimeStamp (imprint = ByteRange) → «secure time».
6. Изменения после подписи: DSS/DocTimeStamp разрешены всегда; при P≥2 — поля/подписи, P=3 — аннотации; остальное — строго (pyHanko «reject-by-default») или мягко (синий «i» Acrobat). MuPDF `pdf_find_locked_fields` (`pdf-xref.c:4782-4877`) читает `Root/Perms/DocMDP` и `TransformParams/P` (**`if (p == 0) p = 2`**, минимум по всем ссылкам), `pdf_is_field_locked`: при `p == 1` заблокировано всё; **P=2 и P=3 не различаются**; `pdf_validate_changes` допускает изменение `V`/`AP` незаблокированных полей, `Kids` той же длины, `ObjStm`/`XRef`, `Metadata`, `Info` (с FIXME), `Encrypt`; `pdf_validate_signature` идёт по версиям назад до первого провала.

pyHanko: платформенное хранилище доверия deprecated, «a nonsensical source of trust for document signature validation», «maintained for TLS purposes». Модель результата (`intact`, `valid`, `trusted`, `modification_level`, `coverage`, `timestamp_validity`, `bottom_line`) по API-докам не подтверждена — проверить перед заимствованием имён. `pkcs7-openssl.c` MuPDF глушит `X509_V_ERR_INVALID_PURPOSE`, `X509_V_ERR_CERT_HAS_EXPIRED`, `X509_V_ERR_KEYUSAGE_NO_CERTSIGN`, удаляет KeyUsage перед проверкой («Acrobat reader creates self-signed certificates that don't list…»), доверяет только `AdobeCA.p7c` (`pkcs7-openssl.c:53-54, 505-531`), OCSP/CRL/TSA нет, PKCS#12 разбирает вручную (`PKCS12_unpack_authsafes`/`PKCS12_decrypt_skey`/`PKCS12_certbag2x509`; с OpenSSL 3 — через deprecated-совместимые макросы `M_PKCS12_bag_type` и др., Debian trixie собирает mupdf 1.25.1 с `libssl3t64`).

### 4.9 Сертифицирующая подпись (DocMDP)

- Certification = `/Reference [<</Type /SigRef /TransformMethod /DocMDP /TransformParams <</Type /TransformParams /P n /V /1.2>>>>]` + `/Root /Perms <</DocMDP sigdict>>`. Текст ISO 32000-1 §12.8.2.2 не загружен (PDF > 10 МБ); значения P подтверждены тремя реализациями: pyHanko `MDPPerm` (NO_CHANGES=1, FILL_FORMS=2, ANNOTATE=3; «This does not apply to DSS updates and the addition of document time stamps»; «Validating this /DocMDP level [ANNOTATE] is not currently supported»), PDFBox `SigUtils.setMDPPermission` (P ∈ {1,2,3}, `V /1.2`, при отсутствии `/P` — 2; исключение, если уже есть подписи: «Approval signatures shall follow the certification signature»), MuPDF (P по умолчанию 2; создания DocMDP нет — `pdf_signature_set_value` пишет только `/Reference` с FieldMDP, чтение DocMDP/FieldMDP есть).
- FieldMDP: `/Lock << /Action /All|/Include|/Exclude /Fields [...] >>` — MuPDF пишет `/Reference`/FieldMDP и ставит `Ff` ReadOnly (`enact_sig_locking`); pyHanko `FieldMDPAction` ALL/INCLUDE/EXCLUDE.
- **MuPDF не пишет DocMDP/Perms** — добавлять самим в том же инкременте. Может ли Acrobat Standard создавать сертифицирующие подписи — не выяснено.

### 4.10 Видимый вид подписи (/AP) и Fill & Sign

- Виджет `/FT /Sig` с `/Rect`, `/AP /N` — Form XObject. Слоистая структура Acrobat (`/FRM`, `n0`…`n4`): документ `PPKAppearances.pdf` больше не доступен; iText 5 javadoc подтверждает «Acrobat 6.0 and higher recommends that only layer n0 and n2 be present». Любой корректный XObject Acrobat отображает. Adobe: внешний вид «is not part of the actual, hashed signature»; для PDF/A шрифты встраивать; водяной знак — `SignatureLogo.pdf` в папке Security.
- MuPDF 1.28.5: `pdf_sign_signature` с флагами внешнего вида, `pdf_sign_signature_with_appearance`, `pdf_signature_appearance_signed/unsigned`, `pdf_preview_signature_as_pixmap` (§4.3) — использовать.
- Fill & Sign — без криптографии (аннотация `/Ink`/`/FreeText`/`/Stamp` или сплющивание); как хранит Acrobat — не проверено.

### 4.11 Инструменты, установка без кликов, тестовые данные

- **OpenSSL для MSVC**: `FireDaemon.OpenSSL` (winget: 4.0.3 последняя; 3.5.0–3.5.3, 3.6.0–3.6.1; exe scope machine, тихо `/exenoui /qn /norestart REBOOT=ReallySuppress ADJUSTSYSTEMPATHENV=yes`); zip 3.5.9 (x86/x64/ARM64) `https://download.firedaemon.com/FireDaemon-OpenSSL/openssl-3.5.9.zip` без установщика; в комплекте `bin` (DLL + exe), `include`, `lib` (`libcrypto.lib`/`libssl.lib`), пример VS-проекта; динамическая линковка; HYBRIDCRT под VS 2026, нужна только UCRT; автозагрузка openssl.cnf отключена. `ShiningLight.OpenSSL.Dev` 3.5.4 — `Win64OpenSSL-3_5_4.msi`/`.exe`, состав `lib/VC/x64/MD*` не подтверждён. vcpkg `openssl` 3.6.5 (статическая сборка под `x64-windows-static-md`; ≥ 3.5 — флаги есть; для LTS — `overrides`). OpenSSL 3.5.6 из Git (mingw64) — для скриптов и PKI, не для линковки.
- **Тестовый ЦС** (`MSYS_NO_PATHCONV=1` в Git Bash): `openssl req -x509 -newkey rsa:2048 -nodes -keyout key.pem -out cert.pem -days 365 -subj "/CN=bny test"`; .pfx по умолчанию OpenSSL 3 (AES-256/SHA-256) импортируется в Windows, `-legacy` (RC2-40) — для негативного теста. Генерация подписей для тестов — pyHanko (§6.1).
- **Эталонные PDF**: EU DSS `dss-pades/src/test/resources/validation` — **108 PDF + 11 подкаталогов** (`dss-818`, `dss-1188`, `dss-1376`, `dss-1420`, `dss-1696`, `dss-1959`, `dss-2058`, `dss-2236`, `dss-2554`, `dss-PLAIN-ECDSA`, `evidence-record`) + `PA_PAdES_AD_RB_v1_0.der`: `pades-bes.pdf`, `pades-epes.pdf`, `doc-firmado-T.pdf`, `doc-firmado-LT.pdf`, `PAdES-LT.pdf`, `PAdES-LTA.pdf`, `pades-ltv.pdf`, `pades-lt-extended-dss.pdf`, `pades-5-signatures-and-1-document-timestamp.pdf`, `pades-lta-copied-doctst.pdf`, `adbe_ocsp_signed.pdf`, `adbe_crl_signed.pdf`, `modified_after_signature.pdf`, `pdf-byterange-overlap.pdf`, `pdf-signed-out-of-byteRange.pdf`, `pdf-signed-added-page.pdf`, `pdf-removed-pages.pdf`, `pdf-spoofing-attack.pdf`, `pades-spoofing-replaced-reason.pdf`, `pdf-double-signer-info.pdf`, `pkcs7-no-message-digest.pdf`, `pdf-with-vri-timestamp.pdf`, `pades-t-duplicated-doctst.pdf`, `pades-signed-filled-form.pdf`, `51sigs.pdf`, `malformed-pades.pdf`, `pades-unsupported-signature-algorithm.pdf`, `encrypted.pdf` и др. (DSS 6.5.1 от 2026-10-14; репозиторий LGPL-2.1, лицензия самих файлов не оговорена; чужие сертификаты с истёкшими цепочками — валидировать с `--retroactive-revinfo`). PDFBox `examples/.../signature`: `CreateSignature`, `CreateSignatureBase`, `CreateVisibleSignature(2)`, `CreateSignedTimeStamp`, `CreateEmbeddedTimeStamp`, `ValidationTimeStamp`, `TSAClient`, `ShowSignature`, `SigUtils` (DocMDP), `CMSProcessableInputStream`, `CreateEmptySignatureForm`, `cert/`, `validation/`. pyHanko: тесты `pkgs/pyhanko/tests` (31 `.py`: `test_pades.py`, `test_diff_analysis.py`, `test_signing.py`, `test_tsa_fetcher.py`…), данные `internal/common-test-utils/src/pyhanko_testing_commons/test_data/` (`data/`, `samples.py`, `certomancer_trust_lists.py`). Онлайн: EC DSS demo 6.5 («limited to testing purposes only»); ETSI Signature Conformance Checker (`https://signatures-conformance-checker.etsi.org/`) — доступен (HTTP 200, JS-приложение), тестовых файлов не найдено.
- Offscreen-тесты: .pfx-подпись и проверка без GUI; CNG — через `PFXImportCertStore(PKCS12_NO_PERSIST_KEY)`; диалог/PIN — только вручную.

### 4.12 Где MuPDF недостаточен — сводка

| Возможность | MuPDF 1.28.5 | Вывод |
|---|---|---|
| Плейсхолдер, ByteRange, инкрементальная запись | есть | использовать |
| SubFilter | только `adbe.pkcs7.detached` | патчить словарь |
| `/Reason`,`/Location`,`/ContactInfo` в словаре | нет | дописывать |
| CMS-генерация (`pkcs7-openssl.c`) | PKCS7-API, `EVP_sha1()`, только `contentType` (signingTime лишь под `#ifdef CLUSTER`), без ESS/PSS, .pfx только | заменить |
| Проверка | `PKCS7_signatureVerify`, доверие только `AdobeCA.p7c`, без CRL/OCSP/TSA, глушит expired/purpose/keyusage | заменить |
| Изменения после подписи | `pdf_signature_incremental_change_since_signing` + `pdf_validate_signature`/`pdf_validate_changes` (P=1 vs остальные; DSS/DocTimeStamp не учтены) | расширить |
| DocMDP/Perms, TSA, DSS, DocTimeStamp | нет | писать самим |
| Внешний вид | есть | использовать |
| Сборка с OpenSSL на Windows | `libpkcs7.vcxproj`: `HAVE_LIBCRYPTO` в DebugExtra/ReleaseExtra/MementoExtra/*Commercial, include `..\..\thirdparty\openssl\include`, lib `..\..\thirdparty\openssl\x64\lib`, `libcrypto.lib`; OpenSSL не сабмодуль | собирать MuPDF без `HAVE_LIBCRYPTO`, крипта отдельно |
| CLI | `mutool sign [-p pwd] [-v] [-c] [-s cert] [-P pwd] [-o out] in.pdf [objnums]`; сообщения «Certificate is trusted.», «Certificate error: %s», «Digest error: %s», «The document is unchanged since signing.», «The signature is valid but there have been edits since signing.», «This signature was invalidated in the last update…» | smoke-тесты (помнить про SHA-1) |

### 4.13 Нарезка на сессии

1. Крипто-ядро (OpenSSL 3.5, CMS-обёртка, PKI-скрипт, тесты `cms -verify -cades`). 2. B-B .pfx через MuPDF. 3. Валидатор. 4. B-T (локальный TSA; freetsa/DigiCert/Sectigo вручную). 5. B-LT/LTA. 6. Windows-хранилище/смарт-карты. 7. DocMDP. 8. Fill & Sign, AATL/EUTL, регрессия на корпусе EU DSS.

## 5. Конвертация, OCR, печать и сканирование

Метод исходной проверки: прямые загрузки первоисточников (help.libreoffice.org, download.documentfoundation.org, winget-pkgs, исходники LibreOffice/MuPDF/Tesseract/Leptonica, learn.microsoft.com, doc.qt.io, qpdf.readthedocs.io, libtiff.gitlab.io, tessdoc) и локальные read-only проверки (`Get-Printer`, реестр Word, `winget show`, кодовая страница). Вики TDF, portableapps.com и twain.org закрыты (Anubis / 403 / пусто).

### 5.0 Резюме решений

| Задача | Рекомендуемый путь | Запасной путь |
|---|---|---|
| DOCX/XLSX/PPTX/ODF/RTF/TXT/HTML → PDF | LibreOffice headless (`soffice.com --headless --convert-to pdf`), JSON-опции `writer_pdf_Export` (PDF/A, теги, качество картинок) | Для DOCX — Word COM `ExportAsFixedFormat` (PDF/A-1 через `UseISO19005_1`) |
| PDF → DOCX | Word COM (`Documents.Open` PDF → `SaveAs2(…, 16)`), Word 16 x64 (M365) есть на машине | LibreOffice `--infilter=writer_pdf_import` (текст — позиционированными фигурами) |
| PDF → PPTX | LibreOffice `impress_pdf_import` → `Impress MS PowerPoint 2007 XML` | — |
| PDF → XLSX | **Нет** штатного пути ни в LO (только гибридный PDF), ни в Word; только собственное извлечение таблиц (stext `TABLE_HUNT`) | — |
| OCR | Tesseract внутри MuPDF (`fz_new_ocr_device` / `fz_new_pdfocr_writer`); tessdata_fast rus+eng+deu (~9,5 МБ); **без OSD** (`DISABLED_LEGACY_ENGINE`) | Windows.Media.Ocr через C++/WinRT (без tessdata, языки из системы, нет hOCR/PDF — слой текста пишем сами) |
| Deskew/бинаризация | MuPDF `fz_detect_skew`/`fz_deskew_pixmap` (`skew=auto`); бинаризация — внутри Tesseract 5 (Otsu, адаптивный Otsu, Sauvola) | Leptonica `pixDeskew`, `pixSauvolaBinarizeTiled` |
| HTML → PDF | `QTextDocument::setHtml` + `QPdfWriter` (подмножество HTML 4/CSS 2.1, 0 МБ зависимостей) | QtWebEngine `printToPdf` — Chromium, тяжёлый деплой, совместимость с offscreen не подтверждена |
| Печать | Qt `QPrinter` (NativeFormat) + растр страниц MuPDF; «Microsoft Print to PDF» есть — свой драйвер не нужен | GDI `StartDoc` c `DOCINFO.lpszOutput` для тихой печати в файл |
| Сканирование | WIA 2.0 (`IWiaDevMgr2`, `GetImageDlg`, `IWiaTransfer::Download`) | TWAIN не нужен на старте (WIA имеет слой совместимости); WIA Automation (`WIA.CommonDialog`) — для скриптовых тестов |
| Изображения → PDF, PDF → PNG/JPEG | MuPDF (§1.12) | — |
| PDF → многостраничный TIFF | libtiff 4.7.2 (vcpkg `tiff`), `TIFFOpenW` + `TIFFWriteDirectory` на страницу | — |
| Сжатие PDF | MuPDF `pdf_rewrite_images` + `pdf_write_options` + `subset_fonts` (§1.10) | qpdf 12.4.2 (`--optimize-images`, `--linearize`, `--object-streams=generate`) |

### 5.1 LibreOffice

**Версии и дистрибутивы (2026-10-21):** на сайте по умолчанию **26.8.1**, также 26.2.6; 26.2.7.1 RC. Каталог stable на зеркале: 25.8.7 (11-May-2026), 26.2.5, 26.2.6 (03-Sep-2026), 26.8.0 (26-Aug-2026), **26.8.1 (08-Oct-2026)**. MSI x86-64: `LibreOffice_26.8.1_Win_x86-64.msi`, **358 МБ**; SDK MSI 22 МБ; help-pack en-US 2,9 МБ. winget `TheDocumentFoundation.LibreOffice` **26.8.1.1**, `InstallerType: msi`, **`Scope: machine`**, `UpgradeBehavior: install`, switch `INSTALLLOCATION="<INSTALLPATH>"`; SHA256 x64 `C6298B10…B04B5312`.

Установка без админа:
- `winget install TheDocumentFoundation.LibreOffice` — только machine-scope → UAC. Для очереди без кликов **не подходит**.
- **Административная распаковка MSI** (образ-источник без регистрации):
  ```bat
  msiexec /a "%TEMP%\LibreOffice_26.8.1_Win_x86-64.msi" /qn TARGETDIR="C:\tools\libreoffice"
  ```
  Microsoft «Administrative Installation» о необходимости повышения прав **не говорит**; работает ли это для LO-пакета без UAC, точная подпапка (ожидаемо `program\soffice.exe`) и размер на диске — **не подтверждено** → проверить в этапе 0.
- LibreOffice Portable (PortableApps): сайт 403; на SourceForge только Legacy 25.2.7 (17.02.2026; 204,9/275,2 МБ); актуальная 26.x-сборка и ключи тихой распаковки `.paf.exe` не подтверждены.

**Headless CLI** (help «Starting LibreOffice Software With Parameters»): `--convert-to OutputFileExtension[:OutputFilterName[:OutputFilterParams[,param]]] [--outdir output_dir]` (без `--outdir` — текущий каталог); `--infilter=InputFilterName[:InputFilterParam[,param]]`; `-env:UserInstallation=file:///c:/my-test-profile` — отдельный профиль; `--headless`; `--norestore`; `--nologo`; `--invisible` («завершить можно только через Task Manager», несовместим с `--quickstart`). «LibreOffice requires write access to its user profile directory». Формат команды на Windows — `"{install}\program\soffice.com" {parameter}`; `soffice.exe` — «когда консоль не нужна».
```bat
rem Office → PDF (один или много файлов), результат в --outdir
soffice.com --headless --norestore --convert-to pdf --outdir C:\out "C:\in\doc.docx" "C:\in\table.xlsx"
rem явный фильтр экспорта
soffice.com --headless --convert-to pdf:writer_pdf_Export --outdir C:\out doc.docx
rem другие примеры из help
soffice --convert-to "html:XHTML Writer File:UTF8" *.doc
soffice --convert-to "txt:Text (encoded):UTF8" *.doc
```
**Код возврата и признаки ошибки (по исходникам master):** `desktop/win32/source/loader.cxx` — `soffice.com` и `soffice.exe` один загрузчик (`officeloader_impl(bool bAllowConsole)`), запускает `soffice.bin`, ждёт (`MsgWaitForMultipleObjects`), читает `GetExitCodeProcess` и **возвращает код `soffice.bin`** (или −1); при `EXITHELPER_NORMAL_RESTART`/`EXITHELPER_CRASH_WITH_RESTART` перезапускает; разница `.com`/`.exe` — только консоль (`DETACHED_PROCESS` у `.exe`) → stdout/stderr видны только через `soffice.com`. `dispatchwatcher.cxx`: при ошибке в stderr `Error: source file could not be loaded`, `Error: no export filter` / `Error: no export filter for <path> found, aborting.`, `Error: Please verify input parameters... (<сообщение>)`; успех в stdout `convert <src> -> <dst> using filter : <name>` (и `Overwriting: <path>`). `app.cxx`: `WithError` → `SetAllSucceeded(false)` → `doShutdown()` возвращает `EXIT_FAILURE`. Итог: ненулевой код + строка `Error:` в stderr — надёжный признак; дополнительно проверять существование/размер файла. На машине не прогонялось.

Имена фильтров (`filter/source/config/fragments/filters/*.xcu`): `MS Word 2007 XML`, `Calc MS Excel 2007 XML`, `Impress MS PowerPoint 2007 XML`, `writer_pdf_Export`, `calc_pdf_Export`, `impress_pdf_Export`, `draw_pdf_Export`.

**JSON-опции экспорта PDF (с 7.4, 2022):** формат `pdf:<filter>:{"Имя":{"type":"<тип>","value":"<значение>"},…}`. Экранирование: **CMD** — внутренние кавычки `\"`; **PowerShell** — вся строка в одинарных кавычках; bash — одинарные кавычки.
```powershell
# PowerShell: PDF/A-2b + теги + JPEG 80 + даунсэмплинг до 150 dpi
& "C:\tools\libreoffice\program\soffice.com" --headless --norestore `
  --convert-to 'pdf:writer_pdf_Export:{"SelectPdfVersion":{"type":"long","value":"2"},"UseTaggedPDF":{"type":"boolean","value":"true"},"Quality":{"type":"long","value":"80"},"ReduceImageResolution":{"type":"boolean","value":"true"},"MaxImageResolution":{"type":"long","value":"150"}}' `
  --outdir C:\out C:\in\doc.docx
```

| Параметр | Тип | Умолч. | Смысл |
|---|---|---|---|
| `SelectPdfVersion` | long | 0 | 0=PDF 1.7, **1=PDF/A-1b, 2=PDF/A-2b, 3=PDF/A-3b**, 15=1.5, 16=1.6, 17=1.7 |
| `PDFUACompliance` | boolean | false | PDF/UA (ISO 14289) |
| `UseTaggedPDF` | boolean | false | структурные теги |
| `UseLosslessCompression` | boolean | false | PNG-подобное сжатие картинок |
| `Quality` | long | 90 | JPEG 1–100 |
| `ReduceImageResolution` / `MaxImageResolution` | boolean / long | false / 300 | даунсэмплинг до 75/150/300/600/1200 dpi |
| `ExportFormFields` / `FormsType` | boolean / long | true / 0 | формы; 0=FDF,1=PDF,2=HTML,3=XML |
| `ExportBookmarks`, `ExportNotes`, `ExportNotesPages`, `ExportHiddenSlides`, `IsSkipEmptyPages`, `SinglePageSheets` | boolean | — | закладки/заметки/скрытые слайды/пустые страницы/«лист на страницу» (Calc) |
| `EmbedStandardFonts` | boolean | false | внедрить 14 стандартных шрифтов |
| `IsAddStream` | boolean | false | гибридный PDF (ODF внутри) |
| `Watermark`, `TiledWatermark`, `WatermarkColor`, `WatermarkFontName` | string/long | — | водяной знак |
| `PageRange` | string | «» | диапазон, напр. `"2-"` |
| `EncryptFile` + `DocumentOpenPassword`, `RestrictPermissions` + `PermissionPassword`, `Printing` (0/1/2), `Changes` (0–4), `EnableCopyingOfContent`, `EnableTextAccessForAccessibilityTools` | — | — | шифрование и права |
| `SignPDF`, `SignCertificateSubjectName`, `SignaturePassword`, `SignatureTSA`, `SignatureLocation/Reason/ContactInfo` | — | — | подпись сертификатом из хранилища LO |
| `InitialView`, `InitialPage`, `Magnification`, `Zoom`, `PageLayout`, `OpenBookmarkLevels`, `HideViewerMenubar/Toolbar/WindowControls`, `OpenInFullScreenMode`, `DisplayPDFDocumentTitle` | — | — | начальный вид |
| `ExportBookmarksToPDFDestination`, `ConvertOOoTargetToPDFTarget`, `ExportLinksRelativeFsys`, `PDFViewSelection` | — | — | ссылки |

Пример JSON для импорт-фильтра (страница start_parameters): `--infilter='Markdown:{"TemplateURL":{"type":"string","value":"./template.ott"}}' test.md`.

**PDF → DOCX / PPTX / XLSX (честно).** Фильтры импорта (`sdext/source/pdfimport/config/pdf_import_filter.xcu`, 7 штук): `writer_pdf_import` (TextDocument), `impress_pdf_import`, `draw_pdf_import` — `XmlFilterAdaptor` → pdfimport; `writer/impress/draw/calc_pdf_addstream_import` — `HybridPDFImport` только для гибридных PDF. **Calc-фильтра импорта обычного PDF нет.** README `sdext`: pdfimport использует poppler как **отдельный внешний процесс** и импортирует содержимое как **Draw-фигуры** (для Writer — позиционированные текстовые объекты, не текучий абзацный документ; детали «фрейм на строку» — по опыту).
```bat
soffice.com --headless --infilter="writer_pdf_import" --convert-to "docx:MS Word 2007 XML" --outdir C:\out in.pdf
soffice.com --headless --infilter="impress_pdf_import" --convert-to "pptx:Impress MS PowerPoint 2007 XML" --outdir C:\out in.pdf
```

**Параллельные вызовы и изоляция.** LibreOffice однопроцессный на один профиль. Для параллелизма — каждому воркеру свой `-env:UserInstallation=file:///C:/Users/<u>/AppData/Local/Temp/lo-prof-<N>`. unoserver (`server.py`) создаёт временный каталог и передаёт `--headless --invisible --nocrashreport --nodefault --nologo --nofirststartwizard --norestore -env:UserInstallation=… --accept=socket,host=127.0.0.1,port=2002,tcpNoDelay=1;urp;StarOffice.ComponentContext` (какие флаги минимально необходимы — не подтверждено). Первый старт нового профиля медленный — профили переиспользовать (пул из N каталогов). Для длительной нагрузки — UNO-сервер; для первой версии достаточно `--convert-to` с пулом профилей.

**Писать самим:** `OfficeConverter` (поиск soffice: `%ProgramFiles%\LibreOffice\program`, свой `tools\libreoffice\program`, настройка пользователя), пул профилей, `QProcess` + таймаут + принудительное завершение (job object — soffice может молча висеть на диалоге), сборка JSON-опций, разбор результата.
**Грабли:** CMD-экранирование JSON; кириллица в путях (`file:///` URL для `-env:`, пути файлов как есть в UTF-16 через `QProcess`); зависший `soffice.bin` после краха блокирует профиль (`.lock`) — чистить `UserInstallation` при старте; `soffice.exe` не отдаёт консоль; без `--norestore` после падения появляется диалог восстановления даже в headless.

### 5.2 Word COM (Microsoft Word 16, Office16)

Локально: `HKCR\Word.Application\CurVer = Word.Application.16`; ClickToRun `16.0.20430.20182`, x64, `O365HomePremRetail`; `MSWORD.OLB` 957 168 Б.

Что умеет Word: открывает PDF и конвертирует в редактируемый документ (Word для Microsoft 365, 2024, 2021, 2019, 2016): «лучше всего работает с PDF, в основном состоящими из текста»; «может не совпадать постранично»; делает ли OCR сканов — не подтверждено (ожидается: нет). Экспорт: `Document.ExportAsFixedFormat(OutputFileName, ExportFormat, OpenAfterExport, OptimizeFor, Range, From, To, Item, IncludeDocProps, KeepIRM, CreateBookmarks, DocStructureTags=True, BitmapMissingFonts=True, UseISO19005_1=False, FixedFormatExtClassPtr)`; `UseISO19005_1=True` → PDF/A; есть `ExportAsFixedFormat2/3`. `WdExportFormat`: `wdExportFormatPDF=17`, `wdExportFormatXPS=18`. Сохранение: `Document.SaveAs2(FileName, FileFormat, …, CompatibilityMode)`; `WdSaveFormat`: `wdFormatXMLDocument=12`, `wdFormatDocumentDefault=16` (DOCX), `wdFormatPDF=17`, `wdFormatXPS=18`, `wdFormatRTF=6`, `wdFormatHTML=8`, `wdFormatFilteredHTML=10`, `wdFormatText=2`, `wdFormatOpenDocumentText=23`, `wdFormatStrictOpenXMLDocument=24`. `Documents.Open(FileName, ConfirmConversions, ReadOnly, AddToRecentFiles, PasswordDocument, PasswordTemplate, Revert, WritePasswordDocument, WritePasswordTemplate, Format, Encoding, Visible, OpenConflictDocument, OpenAndRepair, DocumentDirection, NoEncodingDialog)` — `ConfirmConversions:=False` подавляет диалог «Convert File» (проверить на машине); `Application.DisplayAlerts = wdAlertsNone` (0).

PowerShell-скрипт (файл с BOM):
```powershell
# pdf2docx.ps1
param([string]$Pdf,[string]$Docx)
$w = New-Object -ComObject Word.Application
$w.Visible = $false; $w.DisplayAlerts = 0   # wdAlertsNone
try {
  $d = $w.Documents.Open($Pdf, $false, $true, $false)   # ConfirmConversions=False, ReadOnly=True, AddToRecentFiles=False
  $d.SaveAs2($Docx, 16)                                  # wdFormatDocumentDefault
  $d.Close(0)                                            # wdDoNotSaveChanges
} finally { $w.Quit(); [Runtime.InteropServices.Marshal]::ReleaseComObject($w) | Out-Null }
```
Из C++: (1) `#import "progid:Word.Application" rename_namespace("MSWord") named_guids rename("ExitWindows","WordExitWindows") rename("FindText","WordFindText")` — зависимость сборки от установленного Office; (2) **поздняя привязка `IDispatch`** (предпочтительно): `CoInitializeEx(NULL, COINIT_APARTMENTTHREADED)` → `CLSIDFromProgID(L"Word.Application")` → `CoCreateInstance(clsid, NULL, CLSCTX_LOCAL_SERVER, IID_IDispatch, …)` → `GetIDsOfNames` → `Invoke`; аргументы в `DISPPARAMS.rgvarg` **в обратном порядке**; запись свойства — `DISPATCH_PROPERTYPUT` с `cNamedArgs=1, rgdispidNamedArgs=&DISPID_PROPERTYPUT`; пропущенные параметры — `VARIANT{vt=VT_ERROR, scode=DISP_E_PARAMNOTFOUND}`; helper `AutoWrap(int autoType, VARIANT *pvResult, IDispatch *pDisp, LPOLESTR ptName, int cArgs...)` — архив MS Learn «How to automate Excel from C++ without using MFC or #import». Microsoft «Considerations for server-side Automation of Office» (KB 257757): «не рекомендует и не поддерживает» серверную автоматизацию (нужен интерактивный профиль, модальные диалоги, нереентерабельность) — очередь работает в интерактивной сессии, допустимо, но Word запускать по одному и убивать по таймауту.

**Писать самим:** `WordConverter` (IDispatch-обёртка, отдельный STA-поток, таймаут + `Quit`, обнаружение Word по `HKCR\Word.Application\CurVer`), fallback на LibreOffice, UI-предупреждение «используется Microsoft Word». **Грабли:** зависший модальный диалог оставляет `WINWORD.EXE`; один экземпляр Word на конвейер; первый запуск после установки может показать мастер; документы с паролем → `PasswordDocument`.

### 5.3 OCR

**Tesseract 5.x:** последний релиз **5.5.3 — 2026-07-24** (5.5.2 — 2025-12-26); vcpkg `tesseract` 5.5.3 (deps `leptonica`, `libarchive`, `curl`), `leptonica` 1.87.0. В MuPDF — сабмодули `thirdparty/tesseract` (5.5.2) и `thirdparty/leptonica` (1.87.0), собираются в `.sln` (§1.18); `libtesseract.vcxproj` определяет `HAVE_LEPTONICA, HAVE_TESSERACT, TESSERACT_IMAGEDATA_AS_PIX, TESSERACT_DISABLE_DEBUG_FONTS, GRAPHICS_DISABLED, DISABLED_LEGACY_ENGINE` (+ `HAVE_AVX;HAVE_AVX2;HAVE_SSE4_1;HAVE_FMA` на x64). **Следствие `DISABLED_LEGACY_ENGINE`** (baseapi.cpp): `DetectOrientationScript`, `DetectOS`, `GetOsdText` и загрузка `osd.traineddata` вырезаны; `OEM_TESSERACT_ONLY` недоступен → нет OSD/автоориентации и `--oem 0`; `osd.traineddata` качать бессмысленно. Ориентацию определять самим (OCR четырёх поворотов + `MeanTextConf`) или собирать Tesseract без этого дефайна. **Не держать две копии Tesseract** (vcpkg + submodule): вариант (а) — MuPDF с его thirdparty и те же `libtesseract.lib`/`libleptonica.lib` + заголовки из `thirdparty/` (рекомендуется); (б) — MuPDF с `OCR_DISABLED` и Tesseract из vcpkg, слой текста самим.

**Данные (tessdoc Data Files):** `tessdata_fast` (целочисленные LSTM, быстрее, менее точный; «большинству нужен именно он»), `tessdata_best` (float, точнее и медленнее, единственный для дообучения), `tessdata` (legacy+LSTM, нужен для `--oem 0`); `best`/`fast` только с `--oem 1`.

| Файл | tessdata_fast | tessdata_best | tessdata (legacy+LSTM) |
|---|---|---|---|
| `rus.traineddata` | 3 861 738 Б | 15 301 764 Б | 19 920 885 Б |
| `eng.traineddata` | 4 113 088 Б | 15 400 601 Б | — |
| `deu.traineddata` | 1 525 436 Б | 8 628 461 Б | — |
| `osd.traineddata` | — | — | 10 562 874 Б (не нужен в этой сборке) |

URL: `https://raw.githubusercontent.com/tesseract-ocr/tessdata_fast/main/rus.traineddata` (аналогично `eng`, `deu`; `tessdata_best`, `tessdata`). Путь к данным: `datapath` в `Init` или `TESSDATA_PREFIX`. `pdf.ttf`: в `tessdata` — симлинк на `tessconfigs/pdf.ttf` (submodule) → реальный файл `https://raw.githubusercontent.com/tesseract-ocr/tessconfigs/main/pdf.ttf` (572 Б); необязателен — `TessPDFRenderer` использует встроенный `pdf_ttf`.

API (`include/tesseract/baseapi.h`, `renderer.h`):
```cpp
#include <tesseract/baseapi.h>
#include <tesseract/renderer.h>
#include <leptonica/allheaders.h>
tesseract::TessBaseAPI api;
if (api.Init("C:/app/tessdata", "rus+eng", tesseract::OEM_LSTM_ONLY)) { /* ошибка */ }
api.SetPageSegMode(tesseract::PSM_AUTO);          // PSM_AUTO_OSD требует legacy-движок + osd.traineddata
api.SetImage(pix);                                // Pix* или SetImage(buf, w, h, bytes_per_pixel, bytes_per_line)
api.SetSourceResolution(300);
char* txt = api.GetUTF8Text();                    // или GetHOCRText(page), GetTSVText(page)
int conf = api.MeanTextConf();
tesseract::ResultIterator* it = api.GetIterator();  // Next(RIL_WORD), GetUTF8Text(level), Confidence(level), BoundingBox(level,&x1,&y1,&x2,&y2)
tesseract::TessPDFRenderer pdf("C:/out/scan", api.GetDatapath(), /*textonly=*/false);
api.ProcessPages("C:/in/scan.tif", nullptr, 0 /*timeout ms*/, &pdf);
api.End();
```
- `TessPDFRenderer(outputbase, datadir, textonly)`: текст режимом `3 Tr`; картинки через Leptonica `l_generateCIDataForPdf` (`/FlateDecode`, `/DCTDecode`, `/CCITTFaxDecode /K -1`, `/JPXDecode`); качество JPEG — переменная `jpg_quality`.
- Потокобезопасность: «safe to use multiple TessBaseAPIs in different threads in parallel, UNLESS you use SetVariable on some of the Params in classify and textord».
- Качество (ImproveQuality): 300 dpi и выше; Otsu по умолчанию, в 5.0.0 добавлены Adaptive Otsu и Sauvola (переменная по памяти `thresholding_method`); перекос ухудшает сегментацию строк; поле ~10 px; тёмные края убирать.
- `src/ccutil/serialis.cpp`: `LoadDataFromFile` → `fopen(filename, "rb")` (узкий путь) — на этой машине ACP 1252, профиль ASCII; у пользователей с кириллическим именем `%LOCALAPPDATA%` сломается → класть tessdata в ASCII-путь, либо UTF-8 ACP через манифест (`activeCodePage`, Windows 10 1903+; по памяти), либо 8.3-имя.

**Препроцессинг (Leptonica `skew.c`, `binarize.c`):**
```c
PIX *pixDeskew(PIX *pixs, l_int32 redsearch);                 // любая глубина; не-1bpp бинаризуется внутри
PIX *pixFindSkewAndDeskew(PIX *pixs, l_int32 redsearch, l_float32 *pangle, l_float32 *pconf);
l_ok pixFindSkew(PIX *pixs, l_float32 *pangle, l_float32 *pconf);  // вход строго 1 bpp; умолчания: sweep 7°, delta 1°, minbs 0.01°, редукции 4/2
l_ok pixOtsuAdaptiveThreshold(PIX*, sx, sy, smoothx, smoothy, scorefract /*~0.1*/, PIX **ppixth, PIX **ppixd);
l_ok pixSauvolaBinarizeTiled(PIX*, whsize /*>=7; пример 25*/, factor /*0.2–0.5, ~0.35*/, nx, ny, PIX **ppixth, PIX **ppixd);
```
Совет из исходников: Otsu «не рекомендуется для слабого текста с заметным шумом фона» — там Sauvola. MuPDF-варианты (`fz_detect_skew`, `fz_deskew_pixmap`, `fz_detect_document`, `fz_warp_pixmap`) — §1.13.

**Windows.Media.Ocr (C++/WinRT) — альтернатива:** `OcrEngine` (Windows 10 10240+, `UniversalApiContract` v1): `TryCreateFromUserProfileLanguages()`, `TryCreateFromLanguage(Language)`, `AvailableRecognizerLanguages`, `IsLanguageSupported`, `static uint32_t MaxImageDimension()` (число не указано), `RecognizeAsync(SoftwareBitmap)` → `OcrResult` (`Lines` → `OcrLine.Words` → `OcrWord.Text`, `BoundingRect`; `TextAngle`). Из Win32: C++/WinRT (NuGet `Microsoft.Windows.CppWinRT`, `/std:c++17`+; страница `…/desktop/modernize/winrt-apis-desktop-apps`) либо `cppwinrt.exe` из SDK 10.0.26100; `winrt::init_apartment()`, `windowsapp.lib`; CMake: `find_program(CPPWINRT cppwinrt.exe)` → `winrt/Windows.Media.Ocr.h`, `winrt/Windows.Graphics.Imaging.h`. Языки — FOD `Language.OCR~~~ru-RU~0.0.1.0` (зависимость `Language.Basic`); `Add-WindowsCapability -Online` — локально `Get-WindowsCapability -Online` даёт «The requested operation requires elevation» → DISM из очереди недоступен; в Windows 11 обычные пользователи ставят языковые FOD через Параметры (Time & Language), если не запрещено `RestrictLanguagePacksAndFeaturesInstall`; проверять из приложения `OcrEngine.AvailableRecognizerLanguages`. Плюсы: без tessdata, быстрый. Минусы: нет hOCR/PDF-рендерера, PSM, deskew (только `TextAngle`), зависимость от FOD, асинхронный API.

**Писать самим:** `OcrService` (очередь задач, прогресс, отмена), режимы «Распознать для поиска» (OCR-device → stext → невидимый текст `BT 3 Tr … ET` поверх оригинала без растеризации) и «Скан → PDF» (pdfocr-writer с `skew=auto`), выбор языка, автодокачка `*.traineddata` (SHA256), определение «страница — скан?», DPI-нормализация (300 dpi), собственное определение ориентации; (опц.) `WinOcrBackend`.
**Грабли:** `fz_new_pdfocr_writer` растеризует страницы — только для сканов; OCR однопоточный на страницу — распараллеливать по страницам через `fz_clone_context` + отдельные `TessBaseAPI`; `tessdata_best` ×4 по размеру; растеризация медленна на больших DPI; AVX2 зашит.

### 5.4 HTML → PDF

| | QTextDocument + QPdfWriter/QPrinter | QtWebEngine `printToPdf` |
|---|---|---|
| Движок | Qt rich text (подмножество HTML 4 + CSS 2.1) | Chromium (Skia PDF) |
| HTML/CSS (richtext-html-subset) | теги `a b br div font h1–h6 hr img li ol p pre span table td th tr ul thead tbody tfoot …` (`del` — с 6.11); CSS: цвета/фон, шрифты, `text-decoration`, `line-height`, `white-space`, margins/paddings/borders, `page-break-before/after` (auto/always), `float` только у таблиц и картинок; все селекторы CSS 2.1 кроме псевдоклассов; `thead` влияет на печать многостраничных таблиц; JS, внешние стили не упомянуты | полный современный HTML/CSS/JS |
| Пагинация | `QTextDocument::print(QPagedPaintDevice*)`: если `pageSize` не задан — временная копия разбивается по `paperRect()`, поля 2 см, номера страниц внизу; `setHtml` сбрасывает undo и применяет `@media screen` (`setMetaInformation(CssMedia)`); картинки — `addResource(ImageResource, url, …)` | `QPageLayout(QPageSize::A4, Portrait, QMarginsF())` + `QPageRanges` |
| PDF-версии | `QPagedPaintDevice::PdfVersion`: `PdfVersion_1_4` (умолч.), `PdfVersion_A1b`, `PdfVersion_1_6` (5.12), `PdfVersion_X4` (6.8); `QPdfWriter`: `setResolution`, `setDocumentXmpMetadata`, `addFileAttachment`, с 6.8 `ColorModel` (RGB/Grayscale/CMYK/Auto), `setOutputIntent`, `setDocumentId` | PDF от Chromium (версия/PDF-A не управляются) |
| API | `QPrinter::setOutputFormat(QPrinter::PdfFormat)`, `setOutputFileName("x.pdf")` (суффикс `.pdf` сам переключает) | `void printToPdf(const QString &filePath, const QPageLayout&, const QPageRanges&)` → `pdfPrintingFinished(filePath, success)`; перегрузка с callback `QByteArray` |
| Требования | QtGui/QtPrintSupport | C++20; VS 2022 с MSVC ≥ 14.36; Windows 11 SDK ≥ 10.0.26100.0; MinGW нет |
| Деплой | 0 доп. файлов | `QtWebEngineProcess.exe`, `qtwebengine_resources*.pak`, `icudtl.dat`, `v8_context_snapshot.bin`, `qtwebengine_locales/*.pak`, `Qt6WebEngineCore.dll`, `Qt6WebEngineWidgets.dll`; по опыту ≥150 МБ (официальной цифры нет) |
| Headless | работает с offscreen | не документировано; песочница `QTWEBENGINE_DISABLE_SANDBOX=1`/`--no-sandbox`; совместимость с offscreen — **риск** |

Рекомендация: в Standard-уровне — `QTextDocument`, честно документировать ограничения; QtWebEngine — отдельной опциональной сессией. **Писать самим:** `HtmlToPdf` (инлайн CSS, `<img src>` через `addResource`, базовый URL), выбор формата/полей, тест на offscreen. **Грабли:** `QTextDocument` игнорирует `@page`, `position:absolute`, веб-шрифты (по опыту); картинки по `http://` не грузятся; `print()` печатает копию с 2-см полями — задавать `setPageSize`.

### 5.5 Печать в PDF и регистрация обработчика

- «Microsoft Print to PDF» на машине есть (`Get-Printer`). Проверять из приложения через `EnumPrinters`/`QPrinterInfo::availablePrinterNames()` — без прав; `Get-WindowsOptionalFeature -Online` требует повышения, имя feature (`Printing-PrintToPDFServices-Features`) не подтверждено. Собственный принт-драйвер не нужен. Тихая печать в файл без диалога — GDI: `CreateDC(L"WINSPOOL", L"Microsoft Print to PDF", NULL, NULL)` + `StartDoc` с `DOCINFO.lpszOutput = L"C:\\out\\x.pdf"` («name of an output file; if NULL, the output will be sent to the device»).
- Регистрация обработчика per-user: HKCU `Software\Classes\<ProgID>\shell\{open,print,printto}\command` (`/p "%1"`, `/pt "%1" "%2" "%3" "%4"`; пример MS fa-verbs `/a /p "%1" "%2"`), `Software\Classes\.pdf\OpenWithProgids`, `Capabilities` + `RegisteredApplications` (`ApplicationDescription` обязателен), `SHChangeNotify` — скрипт Inno в §3.7. Разбор `/p`, `/pt` в `main`; `printto` вызывается без окна — не показывать UI.

### 5.6 Сканирование

**WIA 2.0 (рекомендуется):** с Windows Vista, «нацелен на сканеры»; камеры → WPD; слой совместимости WIA 1.0→2.0 и «прозрачный слой, позволяющий TWAIN-приложениям использовать WIA-драйверы»; WSD-сканеры через встроенный класс-драйвер; драйверы в службе WIA (Local Service), UI-часть в процессе приложения. Заголовок `Wia.h`, `CLSID_WiaDevMgr2`, `IID_IWiaDevMgr2`; методы `CreateDevice`, `EnumDeviceInfo`, `GetImageDlg`, `RegisterEventCallbackCLSID/Interface/Program`, `SelectDeviceDlg`, `SelectDeviceDlgID`.
```cpp
HRESULT GetImageDlg(LONG lFlags /*0 или WIA_DEVICE_DIALOG_USE_COMMON_UI*/, BSTR bstrDeviceID /*NULL → диалог выбора*/, HWND hwndParent,
                    BSTR bstrFolderName, BSTR bstrFilename, LONG *plNumFiles /*инициализировать 0*/, BSTR **ppbstrFilePaths /*массив размера 0*/, IWiaItem2 **ppItem);
// S_OK — успех, S_FALSE — отмена (уже отсканированные страницы всё равно возвращаются!); SysFreeString на каждый путь + CoTaskMemFree массива;
// USE_COMMON_UI без системного и вендорского UI → E_NOTIMPL; папке нужны права записи (Vista+)
```
Программный путь: `CreateDevice` → обход `IWiaItem2` → свойства через `IWiaPropertyStorage` → `QueryInterface(IID_IWiaTransfer)` → `IWiaTransfer::Download(0, IWiaTransferCallback*)`; при загрузке папки «все дочерние элементы передаются, каждый отдельным потоком» (`GetNextStream` на страницу, `TransferCallback` — прогресс). Константы (`wiadef.h`): `WIA_IPS_XRES`/`WIA_IPS_YRES` (VT_I4), `WIA_IPA_DATATYPE` (`WIA_DATA_THRESHOLD`, `GRAYSCALE`, `COLOR`, `AUTO`…), `WIA_IPA_FORMAT` (**VT_CLSID**: `WiaImgFmt_BMP` по умолчанию, `PNG`, `JPEG`, `TIFF`, `PDFA`…). В WIA 2.0 планшет и податчик — отдельные дочерние элементы (`WIA_CATEGORY_FLATBED` / `WIA_CATEGORY_FEEDER`); у `WIA_IPS_DOCUMENT_HANDLING_SELECT` флаги `FEEDER`/`FLATBED`/`NEXT_PAGE`/`AUTO_ADVANCE` устарели с Vista, свойство задаёт дуплекс: `FRONT_ONLY` (умолч.), `DUPLEX`, `FRONT_FIRST`/`BACK_FIRST`/`BACK_ONLY` (только с DUPLEX), `ADVANCED_DUPLEX`. WIA Automation (`CommonDialog`, `DeviceManager`) — для PowerShell и ручной проверки: `(New-Object -ComObject WIA.CommonDialog).ShowAcquireImage()` (имя `wiaaut.dll` — по памяти).
**TWAIN:** DSM v2.5.1 (2023-02-10, LGPL, 32/64-бит); актуальная версия спецификации не получена (twain.org отдаёт только заголовок); на старте не нужен. **Windows Scan** — Store-приложение без API.
**Писать самим:** `IScanSource` (`enumerate()`, `scan(settings, pageCallback)`) с `WiaScanSource` и `FakeScanSource` (тестовые PNG — обязательно для offscreen-тестов, виртуального WIA-драйвера нет), диалог настроек (dpi, цвет, ADF-элемент, дуплекс), конвейер «скан → deskew → (OCR) → PDF». **Грабли:** COM в GUI-потоке Qt (STA уже есть); `GetImageDlg` требует прав записи в папку; `S_FALSE` с частичными результатами; события «кнопка на сканере» — `RegisterEventCallbackInterface`.

### 5.7 TIFF и изображения

Чтение/запись изображений и PDF ↔ картинки — §1.12. **Многостраничный TIFF на запись** — libtiff (vcpkg `tiff` 4.7.2):
```c
TIFF *t = TIFFOpenW(L"out.tif", "w");    // TIFFOpenW(const wchar_t*, const char *mode) документирован
for each page: TIFFSetField(t, TIFFTAG_IMAGEWIDTH, w); TIFFSetField(t, TIFFTAG_IMAGELENGTH, h);
   TIFFSetField(t, TIFFTAG_SAMPLESPERPIXEL, 1|3); TIFFSetField(t, TIFFTAG_BITSPERSAMPLE, 1|8);
   TIFFSetField(t, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_MINISWHITE|PHOTOMETRIC_RGB);
   TIFFSetField(t, TIFFTAG_COMPRESSION, COMPRESSION_CCITTFAX4 /*1 bpp*/ | COMPRESSION_LZW | COMPRESSION_JPEG);
   TIFFSetField(t, TIFFTAG_SUBFILETYPE, FILETYPE_PAGE); TIFFSetField(t, TIFFTAG_PAGENUMBER, i, n);
   TIFFSetField(t, TIFFTAG_XRESOLUTION, dpi); … ; for rows: TIFFWriteScanline(t, row, y, 0);
   TIFFWriteDirectory(t);          // завершает IFD страницы и связывает в цепочку
TIFFClose(t);
// чтение: TIFFOpen → TIFFReadDirectory() в цикле / TIFFSetDirectory(n) / TIFFSetSubDirectory
```
Qt-плагин TIFF одностраничный. **Писать самим:** `ImageToPdf` (EXIF-ориентация, размер страницы = размер картинки при DPI, «вписать в A4»), `PdfToImages`, `TiffWriter`. **Грабли:** JPEG-перекодирование ухудшает качество — вставлять DCT-поток как есть; большие TIFF читать полосами; 1-bpp сканы → CCITT G4, а не Flate.

### 5.8 qpdf (внешний инструмент оптимизации и линеаризации)

**12.4.2 — 2026-09-27** (12.4.1 — 08-27, 12.4.0 — 08-09); требует C++20, CMake ≥ 3.16, zlib + libjpeg; крипто openssl/gnutls опционально (иначе native). CLI: `--optimize-images` (не-DCT → DCT, если меньше; `--oi-min-width/--oi-min-height` 128 px, `--oi-min-area` 16384), `--recompress-flate` + `--compression-level=1..9`, `--object-streams=preserve|disable|generate` (generate → PDF ≥ 1.5), `--stream-data=compress|preserve|uncompress`, `--remove-unreferenced-resources=auto|yes|no`, `--linearize`, `--decrypt`, `--encrypt [--user-password=…] [--owner-password=…] --bits=256 [opts] --` (256 бит → PDF 1.7 ext. level 8, формат PDF 2.0/R6; пустой owner-пароль требует `--allow-insecure`), `--flatten-annotations=all|print|screen`, `--split-pages[=n]`, `--pages … --`, `--empty`, `--json`. Обёртка над qpdf (`QPDFJob` или `qpdf.exe`) для линеаризации; `PdfOptimizer` с пресетами, оценка до/после, отмена.

## 6. Тестовые корпуса, эталоны и качество

Метод исходной проверки: документация, GitHub/gitiles API, PyPI JSON, `winget show`, `curl -I`, `git ls-remote`; локально — Build Tools, Java, Python. Файлы (zip, PDF) не скачивались — только заголовки HTTP. web.archive.org и pdfa.org недоступны инструментам (403).

### 6.0 Главные выводы

| Вопрос | Решение |
|---|---|
| Откуда брать PDF | Не копировать чужие корпуса в репозиторий. Хранить **манифест** (URL + sha256 + лицензия + теги + ожидаемый результат) и скрипт-загрузчик, кэширующий файлы вне репо (как `.link` в pdf.js). В репо — только **свои** сгенерированные файлы и файлы с явной свободной лицензией. |
| Рендер-тесты | Эталоны PNG + «нечёткое» сравнение по схеме pdfium + pHash/SSIM в Python (`py -3.14`: scikit-image 0.26.0 cp314 есть, cp315 нет). |
| Совместимость без Acrobat | Матрица `qpdf --check`, veraPDF 1.30.3, `pdfcpu validate -m strict`, `mutool clean`, pypdfium2 (тот же PDFium, что в Chrome/Edge), pyHanko, Word COM. Всё, кроме LibreOffice, ставится portable-zip без админа. |
| Устойчивость | `fz_try/fz_catch` + `fz_report_error` (§1.17); libFuzzer и ASan уже в Build Tools; публичный корпус OSS-Fuzz MuPDF доступен (232 221 233 Б, Last-Modified 2026-10-20). |
| Статанализ без GUI | `cl /analyze` + CppCoreCheck, clang-tidy 19.1.5 (есть), cppcheck 2.22.0 (только MSI) — §3.5. |
| ctest для очереди | Метки `unit` / `render` / `slow` / `fuzz` / `compat`, `ctest -L unit --timeout 60` на каждом пункте, `ctest -L render -j 8 --output-junit` в конце сессии; `gtest_discover_tests(... DISCOVERY_MODE PRE_TEST)`; `TIMEOUT` на каждом тесте; ASan-DLL в `ENVIRONMENT`. |

### 6.1 Где брать PDF для тестов

Главный указатель — `github.com/pdf-association/pdf-corpora` (README: 53 заголовка, из них ~47 собственно корпусов; лицензии отдельных корпусов не указаны; «All copyright and trademarks remain with their respective owners»).

| Корпус | Что внутри | Объём | Лицензия | Как получить | Проверено |
|---|---|---|---|---|---|
| **pdf.js `test/pdfs`** | **1 462 записи: 999 `.pdf` + 460 `.pdf.link`**; `annotation-*.pdf` — 30 файлов (`-highlight`, `-line`, `-stamp`, `-squiggly`, `-tx/-tx2/-tx3`, `-text-widget`, `-choice-widget`, `-button-widget`, `*-without-appearance`…), `acroform_calculation_order.pdf`; **57 записей `xfa_*`** (все `.link`). `.gitignore` = `*.pdf`, затем `!имя.pdf` | ~1 000 в git + 460 `.link` | **не указана** (нет README/LICENSE в `test/pdfs`; репо Apache-2.0) | `git clone --depth 1 https://github.com/mozilla/pdf.js` (sparse `test/pdfs`) + скрипт, читающий `.link` (часто archive.org) | да |
| **pdfium_tests** (Google) | `fx/` — **25 папок**: `action, color, font, form, FRC_3.5_part1/2, FRC_4.5_part1, FRC_8.2.2_part1/2, FRC_8.2.4_part1, FRC_8.4_part1/2, FRC_8.5_part1/2, image, js, layer, matrix, mulobj, new, other, path, pattern, shading, text`; плюс `pdfium/`, `third_party/`, `xfa_specific/`. Эталоны: `<name>_expected[_agg|_skia|_gdi|_skia_gdi][_{os}].pdf.<page>.png` | тысячи | **BSD-3-Clause** | `git clone https://pdfium.googlesource.com/pdfium_tests` | да |
| **veraPDF-corpus** (ветка `staging`) | **2 906 PDF**: `PDF_A-1a` 34, `PDF_A-1b` 569, `PDF_A-2a` 27, `PDF_A-2b` 986, `PDF_A-2u` 22, `PDF_A-3b` 12, `PDF_A-4` 485, `PDF_A-4e` 19, `PDF_A-4f` 11, `PDF_UA-1` 297, `PDF_UA-2` 138, `ISO 32000-1` 5, `ISO 32000-2` 1, `TWG test files` 85, `Undefined` 10, **`Isartor test files/PDFA-1b/6.x …` — 205**. По именам 1 072 `-pass-`, 1 823 `-fail-` | 2 906 | **CC BY 4.0** (условия Isartor внутри не проверены) | `git clone -b staging https://github.com/veraPDF/veraPDF-corpus` | да |
| **BFO PDF/A testsuite** | 34 записи (9 pass / 25 fail): 33 `pdfa2-*` + 1 `pdfa1-*`; **один файл без расширения** (`pdfa1-6-5-3-bfo-t01-fail-pdf`); `description.txt` | 34 | **CC BY 3.0** | `github.com/bfosupport/pdfa-testsuite` | да |
| **pdf20examples** | 7 PDF 2.0 (`Simple PDF 2.0 file`, `UTF-8 string and annotation`, `image with BPC`, `via incremental save`, `with offset start`, `page level output intent`, `pdf20-utf8-test`) | 7 | **CC BY-SA 4.0** — ShareAlike | git | да |
| **pdf-differences** | 18 кейсов: `Atomic-Fill+Stroke, ColorBurn-ColorDodge, Dashing-Degenerate, Dashing-EndBeforeBend, DefaultColorSpaces, IndexedColor, Inline-Image-Abbreviations, LargeMitreLimit, LineCap-Degenerate, Negative-DashPhase, NegativeFontSize, OverlappingGlyphClipping, PDF-version, PageLabels-UX, TextClipModeChanges, Type3WordSpacing, UnknownFilter, VerticalText`; «Correct renderings are always the _last_ image in the MarkDown» | 18 | PDF — **CC BY 4.0**, код — Apache-2.0 | git | да |
| **safedocs** | `CompactedSyntax`, `Dialects`, `Inline Image Abbreviations`, `Miscellaneous Targeted Test PDFs` (**11 PDF**: `ContentStreamCycleType3insideType3` (негативный), `ContentStreamNoCycleType3insideType3`, `Dual-startxref`, `FontinsideType3insideType3`, `LinkAnnot-appearances`, `PDF-NoPageContents`, `PatternTextInsideText`, `list-of-uri-actions(-with-base)`, `uri-ismap-test`, `utf16LE-test`), `SankeyDiagrams`, `Unicode passwords` | десятки | **Apache-2.0** | git | да |
| **DSS** `dss-pades/src/test/resources/validation` | **108 PDF + 11 подпапок** (§4.11) | 108+ | репо LGPL-2.1; лицензия файлов не оговорена | git sparse checkout | да |
| **CC-MAIN-2021-31-PDF-UNTRUNCATED** (SafeDocs/DARPA) | 7,9 млн уникальных PDF; 7 933 zip по ≤1 000 файлов (1,0–2,8 ГБ); CSV-метаданные `pdfinfo`, Tika, provenance | ~8 ТБ | условия Common Crawl + Digital Corpora | `https://downloads.digitalcorpora.org/corpora/files/CC-MAIN-2021-31-PDF-UNTRUNCATED/zipfiles/0000-0999/0000.zip` (302 → S3, 1 266 879 273 Б) | да |
| **UNSAFE-DOCS** | >5 млн вредоносных/битых | огромный | digitalcorpora | digitalcorpora.org | по индексу |
| **GovDocs1** | «nearly 1 million» файлов (≈231 тыс. PDF, ~308 ГБ), 1 000 zip по 1 000 файлов; многие «PDF» не PDF — фильтр по `%PDF` | ~308 ГБ | «freely available for research»; произведения правительства США — public domain (17 U.S.C. §105(a)) | `…/govdocs1/zipfiles/000.zip` (302 → S3, 486 762 106 Б) | да |
| **SafeDocs Issue Tracker corpus** | >32 000 PDF (>31 ГБ) из багтрекеров | 32k | не указана | `corpora.tika.apache.org` — **DNS не разрешается** | нет |
| **OpenPreserve format-corpus** | `pdfCabinetOfHorrors`, `fully-featured-pdf`, `govdocs1-error-pdfs`, `pdf-handbuilt-test-corpus`, `jhove-errors`… | небольшой | **CC0** | git | лицензия/папки — да |
| **OSS-Fuzz корпус MuPDF** | минимизированный `mupdf_pdf_fuzzer` | **232 221 233 Б** | не оговорена | `https://storage.googleapis.com/mupdf-backup.clusterfuzz-external.appspot.com/corpus/libFuzzer/mupdf_pdf_fuzzer/public.zip` (HTTP 200); pdfium — 404, qpdf — 403 | да |
| **Формы IRS** | `https://www.irs.gov/pub/irs-pdf/f1040.pdf` (220 237 Б, Last-Modified 2026-01-02), `fw4.pdf`, `i1040gi.pdf` (меняются по годам — фиксировать sha256) | сотни | public domain | прямые ссылки | да |
| **Спецификации** | `PDF32000_2008.pdf` (22 491 828 Б), `pdfreference1.7old.pdf` (32 453 129 Б); число страниц (~756 / ~1 310) не подтверждено | 2 | Adobe, свободно | `https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/…` | размер — да |
| **Ghostscript/MuPDF `tests.git`** | публичный корпус Artifex | ? | ? | `git ls-remote https://git.ghostscript.com/tests.git` отвечает (HEAD f7d5087…, ветки `master`, `add_sample_file`); cgit — 401 | состав — нет |
| **Isartor (оригинал), Matterhorn 1.02, PDF/UA Reference Suite** | PDF/A-1b негативные; PDF/UA чек-лист (31 чекпоинт, 136 условий); эталонные PDF/UA-1 | | | pdfa.org — **403**; Isartor брать из veraPDF-corpus | частично |

По категориям: **PDF/A** — veraPDF-corpus (вкл. Isartor), BFO; тест = «veraPDF даёт тот же pass/fail, что в имени файла». **PDF/UA** — `PDF_UA-1` (297), `PDF_UA-2` (138); veraPDF `-f ua1`, `-f ua2`, `wt1r`, `wt1a`. **Формы** — IRS, `pdfium_tests/fx/form`, `fx/FRC_8.*`, pdf.js `annotation-*-widget.pdf`, `acroform_calculation_order.pdf`; **XFA** — `pdfium_tests/xfa_specific/`, pdf.js `xfa_*.pdf.link`. **Аннотации** — pdf.js `annotation-*.pdf` (30), safedocs `LinkAnnot-appearances.pdf`, pdf20examples; полной матрицы 28 подтипов нет — **генерировать самим**. **Подписи** — DSS; собственные через pyHanko. **Шифрование** — генерировать qpdf; Unicode-пароли — safedocs; DSS `encrypted.pdf`. **Битые/фаззинг** — OSS-Fuzz, safedocs, pdf-differences `UnknownFilter`, UNSAFE-DOCS; собственный мутатор. **Большие** — `pdfreference1.7old.pdf` + синтетика. **Кириллица/CID** — готовых наборов не найдено; генерировать Word COM/LibreOffice/MuPDF; реальные — официальные документы РФ (ГК РФ ст. 1259 п. 6: не являются объектами авторских прав официальные документы гос. органов, законы, нормативные акты, судебные решения — по копии на ru.wikisource.org).

**Что писать самим:**
1. `tests/corpus/manifest.json` — для каждого внешнего файла `url`, `sha256`, `license`, `tags` (`annots`, `forms`, `xfa`, `enc-r6`, `sig-lta`, `pdfa-1b-fail`, `big`, `cyr`, `fuzz`), `expect` (`opens` / `needs-password` / `error:FZ_ERROR_FORMAT` / `pages:N`); `tools/fetch_corpus.py` (`py -3.14 -I`) качает в `%LOCALAPPDATA%\bny-acro\corpus\` и проверяет sha256.
2. **Матрица шифрования** (qpdf 12.4.2; синтаксис `--encrypt … --` с 11.7.0; `--bits=256` → «minimum PDF version is 1.7 with extension level 8, and the AES-based encryption format used is the one described in the PDF 2.0 specification» (V=5/R6); 40 бит = всегда RC4 (V=1/R=2); `--use-aes` по умолчанию `n`; `--allow-weak-crypto` обязателен для RC4/40 бит; пустой owner-пароль требует `--allow-insecure`):
   ```bat
   qpdf --allow-weak-crypto --encrypt --user-password=u --owner-password=o --bits=40  -- base.pdf enc-rc4-40.pdf
   qpdf --allow-weak-crypto --encrypt --user-password=u --owner-password=o --bits=128 --use-aes=n -- base.pdf enc-rc4-128.pdf
   qpdf --encrypt --user-password=u --owner-password=o --bits=128 --use-aes=y -- base.pdf enc-aes-128.pdf
   qpdf --encrypt --user-password=u --owner-password=o --bits=256 -- base.pdf enc-aes-256-r6.pdf
   qpdf --encrypt --user-password= --owner-password=o --bits=256 -- base.pdf enc-aes-256-emptyuser.pdf
   ```
   Проверка: `qpdf --is-encrypted f.pdf` (0 = зашифрован, 2 = нет), `qpdf --requires-password --password=u f.pdf` (0 = нужен другой пароль, 2 = не зашифрован, 3 = пароль подошёл), `qpdf --show-encryption f.pdf`.
3. **Подписи** (pyHanko 0.38.0, MIT, чистое колесо; ключи — `C:\Program Files\Git\usr\bin\openssl.exe`):
   ```bat
   openssl req -x509 -newkey rsa:2048 -nodes -keyout key.pem -out cert.pem -days 365 -subj "/CN=bny test"
   pyhanko sign addsig --field Sig1 pemder --key key.pem --cert cert.pem --no-pass in.pdf out-b.pdf
   pyhanko sign addsig --field Sig1 --timestamp-url http://tsa.example.com --with-validation-info --use-pades pemder --key key.pem --cert cert.pem --no-pass in.pdf out-lt.pdf
   pyhanko sign ltaupdate --timestamp-url http://tsa.example.com out-lt.pdf        & B-LTA, правит файл на месте
   pyhanko sign validate --pretty-print out-b.pdf
   pyhanko sign validate --retroactive-revinfo --pretty-print dss-file.pdf
   pyhanko sign ltvfix --field Sig1 file.pdf & pyhanko sign adesverify --eutl-all file.pdf
   pyhanko encrypt --password secret in.pdf enc.pdf   & «PyHanko always uses AES-256 for the actual content encryption»
   pyhanko decrypt password enc.pdf out.pdf
   ```
   `--no-pass` подтверждён в исходнике CLI («assume the private key file is unencrypted»), `--passfile` — файл с парольной фразой. Для B-T/LT/LTA нужен TSA (CLI поддерживает только публичные без аутентификации); для офлайн-очереди — локальный тестовый TSA (CLI для локального TSA не проверен) или B-B + сохранённые LT/LTA из DSS.
4. **Генератор аннотаций** — C++-утилита на MuPDF (`pdf_create_annot` для каждого `PDF_ANNOT_*`, `pdf_update_appearance`), поля форм всех типов, детерминированный (`/ID`, даты фиксированы).
5. **Кириллица**: Word COM (`ExportAsFixedFormat(out, 17)`), LibreOffice headless, ручные PDF-шаблоны (не встроенный TrueType, Type1 с Differences, CID с CIDToGIDMap).
6. **Большой файл**: `qpdf --empty --pages base.pdf 1-z base.pdf 1-z … -- big.pdf` или pikepdf-цикл до 1 200 страниц; 200-страничный файл с 200 изображениями 2 000×2 000.
7. **Мутатор битых файлов** (Python, детерминированный seed): усечение 10/50/90 %, порча `startxref`, неверный `/Length`, рекурсия `/Kids`, `/Prev`-петля, огромный `/Size`, flate-бомба, нулевой `/Count`, вложенность 100 000. Ожидание: процесс жив, `fz_caught()` ∈ {FORMAT, SYNTAX, LIMIT, UNSUPPORTED}, нет ASan-ошибок.

**Грабли:** pdf.js — не коммитить к себе; `node test/add_test.mjs` «only supports adding `eq` tests for all pages»; pdf20examples — ShareAlike; CC-MAIN zip по 1–2,8 ГБ — только `0000.zip` + CSV; BFO — файл без `.pdf`.

### 6.2 Рендер-тесты: эталоны и сравнение

| Проект | Эталон | Сравнение | Обновление |
|---|---|---|---|
| **pdfium** (`testing/tools/pngdiffer.py`) | `name_expected[_agg\|_skia\|_gdi\|_skia_gdi][_{os}].pdf.<page>.png` (поиск от специфичного к общему), страница с 0 | `pdfium_diff --metrics`: размеры, число отличающихся пикселей, max per-channel delta, MSE, windowed MSE, SSIM. **Точное** = `pixels == 0`; **нечёткое** (`delta=3, mse=0.05, win_mse=15.0, ssim=1.0` = `kMaxFuzzy*` из `testing/utils/pixel_diff_util.h`): `max_delta ≤ 3 ∧ MSE ≤ 0.05 ∧ wMSE ≤ 15` **или** `mean_ssim > 1.0` (SSIM выключен; порог 0.0 отключает метрику; `--fuzzy=delta,mse,win_mse,ssim`). При провале — `.pdf.<page>.diff.png` через `pdfium_diff --subtract` | `make_expected.sh your_new_file.pdf` |
| **pdf.js** | снимки canvas (`eq`), слой текста (`text`), аннотации (`annotations`), `load`, `fbf` | против снимков текущего кода; `npx gulp makeref` до изменений, `npx gulp test`, `npx gulp unittest` | makeref |
| **mutool draw -s 5** | md5 растра | точное | регенерация |

Схема для проекта:
1. Рендер — напрямую MuPDF → PNG (`fz_new_pixmap_from_page`), 72 и 150 dpi, RGB без альфы, AA 8 бит (`-A 8`; `0` — без AA, `9` — centre-of-pixel, `10` — any-part-of-pixel), ICC выключен (`-N`). **Не рендерить эталоны через Qt-виджет** (±1 по каналам).
2. Хранение: `tests/render/expected/<corpus>/<file>.<page>.<dpi>.png` + `expected.json` (sha256, версия MuPDF, дата, `fuzzy` на файл). Генерация в этапе 0 (`render-ref --update`), просмотр агентом, коммит; обновление только при смене MuPDF (`--update --reason "MuPDF 1.28.5→…"`). Эталоны зависят от FreeType внутри MuPDF — фиксировать версию. PNG-эталоны пухнут: ≤ 300 страниц и/или `optipng`.
3. Сравнение (C++, без зависимостей): те же четыре метрики; SSIM по серому (окно 8×8 или гаусс σ=1.5, `C1=(0.01·255)²`, `C2=(0.03·255)²`); для текста порог SSIM ≥ 0.98. SSIM на почти белой странице ≈ 1 даже при пропавшей строке — обязательно `max delta`/`MSE`.
4. Второй контур (Python `py -3.14`): `scikit-image 0.26.0` (`structural_similarity(im1, im2, *, win_size=None, gradient=False, data_range=None, channel_axis=None, gaussian_weights=False, full=False)`; `data_range` «recommended to always pass explicitly»), `imagehash 4.3.2` (BSD-2; `phash(img, hash_size=16)`, расстояние `h1 - h2` — Хэмминг), `opencv-python-headless 5.0.0.93` (`cp37-abi3-win_amd64`, работает и под 3.15). На 3.15 **scikit-image колёс нет**; pillow 13.0.0, numpy 2.5.4, scipy 1.18.1, pikepdf 10.17.0 имеют cp315; pymupdf 2.0 — `cp311-abi3`.
5. Снимки окна Qt — §2.7; сравнивать только pHash/SSIM (≥ 0.9), хранить как артефакт сессии.

**Писать самим:** `tools/imgcmp` (C++): `imgcmp a.png b.png [--fuzzy d,mse,wmse,ssim] [--diff out.png]`, код 0/1, одна строка метрик — контракт `pdfium_diff --metrics`; `tests/render/render_test.cpp` (параметризованный gtest по `expected.json`; `BNY_UPDATE_REFS=1` пишет эталоны); `tools/review_refs.py` (контакт-лист «эталон / текущий / diff»).

### 6.3 Проверка совместимости без Acrobat

| Инструмент | Версия/дата | Установка без админа | Команда | Что проверяет | Код возврата |
|---|---|---|---|---|---|
| **qpdf** | 12.4.2 (2026-09-27), Apache-2.0 | `qpdf-12.4.2-msvc64.zip` (28 183 147 Б; `qpdf-12.4.2.sha256` 2 906 Б + `.sigstore`); winget — nullsoft `Scope: machine` | `qpdf --check f.pdf`; `--json` (v2), `--json-key=key`, `--show-npages`, `--check-linearization`, `--show-encryption` | «Check the file's structure as well as encryption, linearization, and encoding of stream data»; «may still have errors in stream data content»; «For erroneous files, --check will cause qpdf to attempt to recover» → требовать 0 | **0** нет ошибок/предупреждений, **2** ошибки, **3** только предупреждения (`--warning-exit-0` → 0) |
| **veraPDF** | **1.30.3** (2026-10-09; «version date» 2026-10-07); `verapdf-installer.zip` 32 971 125 Б | IzPack; unattended: `verapdf-install.bat auto-install.xml` (имя по исходникам `installer/src/main/resources/org/verapdf/scripts/verapdf-install.bat`; docs.verapdf.org пишет `vera-install.bat` — проверить в zip); XML генерируется интерактивным инсталлятором (есть `auto-install-tmp.xml`). Java: инсталлятор «requires Java 8, 11, 17 or 21», ядро с 1.30.1 — Java 25; на машине JBR 25.0.3; запасной — Temurin 21 JRE zip | `verapdf -f 1b f.pdf`, `-f 0` (автодетект), `-f ua1`, `--format json` (по умолчанию xml; `text, raw, html`), `-r`, `-o --extract informationDict`, `--password` | PDF/A-1a…4f, PDF/UA-1/2, WTPDF | `CliConstants.ExitCodes`: 0 VALID, 1 INVALID, 2 BAD_PARAMS, 3 OOM, 4 NO_FILES, 6 IO_EXCEPTION, 7 FAILED_PARSING, 8 ENCRYPTED_FILES, 9 VERAPDF_EXCEPTION, 10 JAXB_EXCEPTION, 11 FAILED_MULTIPROCESS_START, 12 INTERRUPTED_EXCEPTION; приоритет OOM > VERAPDF_EXCEPTION > ENCRYPTED > FAILED_PARSING > INVALID |
| **pdfcpu** | 0.16.3 (2026-10-19) | `pdfcpu_0.16.3_Windows_x86_64.zip` (7 071 702 Б); winget portable zip | `pdfcpu validate -m strict f.pdf` (`strict, relaxed`; по умолчанию relaxed — «may … apply a safe, unambiguous repair»; `-l` проверка ссылок, `--quiet`) | PDF 1.7 + базовые проверки 2.0 | по `cmd/pdfcpu/main.go`: любая ошибка → `os.Exit(1)`, иначе 0 |
| **MuPDF mutool** | 1.28.5 | из исходников (§1.18); winget-пакета нет | `mutool clean -gggg -s in.pdf out.pdf`; `mutool draw -s 5 -o x%d.png f.pdf` (`-s m/f/t/5`: память, фичи, время, md5), `-i` игнорировать ошибки, `-N` без ICC, `-P` параллельно, `-B band -T threads`, `-L` low-mem, `-m bytes`; в 1.28 overprint `-M`, `-O` device-опции | «открывается и переписывается нашим же движком» (без «repairing») | 0/ненулевой + stderr |
| **PDFium (Chrome/Edge)** | pypdfium2 5.14.0 (2026-10-04; `py3-none-win_amd64`), Apache-2.0/BSD-3, бинарники bblanchon/pdfium-binaries | `pip install pypdfium2` | `pypdfium2 render f.pdf -o out --scale 2 --pages 1-3 --draw-forms`, `pdfinfo`, `fonts`, `toc`, `extract-text`, `extract-images`, `arrange`, `attachments`, `imgtopdf`, `tile`, `pageobjects` | «открывает ли Chrome/Edge» | ненулевой при исключении |
| `pdfium_test` | — | depot_tools + gclient + GN/Ninja + clang («No MSVC patches will be taken») — нереально на MSVC; файл `testing/pdfium_test/pdfium_test.cc` | `--ppm --png --bmp --emf --skp --txt --md5 --annot --scale=<n> --pages=<n> --time=<n> --password=<s> --render-oneshot --font-dir=<path> --no-system-fonts --mem-document --disable-javascript --disable-xfa --grayscale --lcd-text --reverse-byte-order --show-metadata --show-pageinfo --show-structure --save-images --save-attachments --fontations --use-renderer`; md5 строкой `MD5:<file>:<hash>` | эталонные тесты pdfium | |
| **Chrome** | есть; **msedge.exe нет** | — | headless-открытие PDF не верифицировано | | |
| **Word 2016 (Office16)** | `WINWORD.EXE` | COM | `Documents.Open("f.pdf", ConfirmConversions=False)` → PDF Reflow; `ExportAsFixedFormat(out, 17)` | | |
| **pyHanko** | 0.38.0 (2026-10-17), MIT | pip | `pyhanko sign validate --pretty-print f.pdf`, `sign adesverify --eutl-all`, `sign ltvfix --field Sig1` | PAdES, ByteRange, LTV | не задокументирован |
| **pikepdf / pypdf** | 10.17.0 (MPL-2.0) / 6.21.1 | pip | `pikepdf.open(...).check()` | дубль qpdf из Python | |

Матрица «что чем ловим»:

| Наш артефакт | Минимум | Полный прогон |
|---|---|---|
| Любая запись (save/incremental) | `qpdf --check` = 0, `mutool clean` без warn, `pypdfium2 pdfinfo` | + `pdfcpu validate -m strict` (exit 0), + повторное открытие нашим движком |
| Формы | `qpdf --json --json-key=acroform` сверка значений | + `pypdfium2 render --draw-forms` ↔ эталон |
| Шифрование | `qpdf --is-encrypted`, `--requires-password`, `--show-encryption` | + pikepdf/pypdfium2 с паролем; Unicode-пароли из safedocs |
| Подписи | `pyhanko sign validate` | + две подписи, подпись после правки → «modified after signature» |
| PDF/A-экспорт | veraPDF `-f 0` (exit 0) | корпус veraPDF pass/fail |
| Office → PDF | `qpdf --check` + число страниц | рендер-эталон |

**Писать самим:** `tools/compat_check.py` (`py -3.14 -I`): прогоняет файл через все инструменты, JSON `{tool, version, exit, warnings[]}`; ctest `LABELS compat`; CMake-опция `BNY_TOOLS_DIR` с portable-инструментами (`qpdf.exe`, `pdfcpu.exe`, `verapdf.bat`), `SKIP_RETURN_CODE 77`, если инструмента нет. Нет публичной проверки «Acrobat откроет без ремонта» — косвенно: `qpdf --check` = 0 ∧ `mutool clean` без «repairing» ∧ pdfcpu strict ok.

### 6.4 Фаззинг и устойчивость

Правила MuPDF — §1.17. Инструменты MSVC (Build Tools 14.44.35207, проверено):
- `/fsanitize=address` (с VS 16.9; x86/x64, Windows 10+), несовместим с `/ZI`, `/INCREMENTAL`, `/RTC` → `/Zi`. С 17.7 Preview 3 и для `/MT`, и для `/MD` линкуются `clang_rt.asan_dynamic-{arch}.lib` + thunk, бинарник `clang_rt.asan_dynamic-x86_64.dll` (в `bin\Hostx64\x64`; vcvars64 добавляет каталог в PATH — для Qt-процесса DLL должна быть в PATH). `ASAN_SAVE_DUMPS=x.dmp`; `/fsanitize-address-use-after-return` + `ASAN_OPTIONS=detect_stack_use_after_return=1`.
- `/fsanitize=fuzzer` (с VS 17.0, experimental): добавляет `clang_rt.fuzzer_{MT,MD,MTd,MDd}-{arch}` (+ `_no_main_*`), ставит `/fsanitize-coverage=edge`, `inline-8bit-counters`, `trace-cmp`, `trace-div`. Опции **по отдельности** (`/fsanitize=address /fsanitize=fuzzer`). `FuzzedDataProvider.h` есть. `-fork`/`-jobs` могут не работать; `-timeout` — на вход.
- Других санитайзеров в MSVC нет («feedback… /fsanitize=thread, /fsanitize=leak, /fsanitize=memory, /fsanitize=undefined»); `clang_rt.ubsan_standalone-x86_64.lib` есть, но без clang-cl бесполезен.
- CMake: `CMAKE_MSVC_DEBUG_INFORMATION_FORMAT` ≠ `EditAndContinue`; флаги через `target_compile_options`; без `/RTC1`.

Корпуса: `mupdf_pdf_fuzzer/public.zip` (232 МБ; содержимое не проверено). OSS-Fuzz `projects/mupdf/build.sh`: `make -j HAVE_GLUT=no build=debug OUT=$WORK libmupdf.a libmupdf-third.a`, единственная цель `pdf_fuzzer.cc` (`-std=c++11 -Iinclude`), `pdf_fuzzer_seed_corpus.zip` + `.dict` + `.options`. pdfium: **35 файлов `*_fuzzer.cc`** в `testing/fuzzers/` (`pdfium_fuzzer, pdf_codec_jbig2_fuzzer, pdf_jpx_fuzzer, pdf_font_fuzzer, pdf_cmap_fuzzer, pdf_streamparser_fuzzer, pdf_hint_table_fuzzer, xfa_fuzzer, …`). Семена из safedocs, pdf-differences, собственные мутанты.

**Писать самим:** `fuzz/pdf_open_fuzzer.cpp`: `LLVMFuzzerTestOneInput` → `fz_open_memory` → `fz_open_document_with_stream` → `fz_count_pages` → рендер 32×32 → текстовый девайс → закрыть. Сборка `/fsanitize=fuzzer /fsanitize=address /Zi /MD`; запуск `pdf_open_fuzzer.exe corpus_dir -max_total_time=600 -rss_limit_mb=4096 -timeout=20 -artifact_prefix=fuzz-out\`. Падения → `tests/corpus/regress/`, `load`-тест (`LABELS unit;regress`).

### 6.5 Производительность

- Публичных чисел Acrobat нет (поиск недоступен). Ориентиры — SumatraPDF и MuPDF на той же машине.
- **SumatraPDF 3.6.1** (2026-04-06): portable `SumatraPDF-3.6.1-64.zip` 10 031 830 Б; CLI `-bench <filepath> [page-range]` («outputs the required rendering times … Often used together with -console»), `-stress-test <path> [file-filter] [range] [cycle-count]` (`-stress-test file1.pdf 25x`, `-stress-test dir *.pdf;*.xps 15- 3x`), `-exit-when-done`; `-start-perf-log`/`-log-perf-file` — только profile-сборка; `-bench` печатает только при `-console`.
- **MuPDF**: `mutool draw -s t -o nul: f.pdf` (время), `-s m` (память), `-P`, `-T n -B band`, `-L`, `-m bytes`.
- **Qt**: `QBENCHMARK { … }`; `-iterations n`, `-median n`, `-minimumvalue n`, `-minimumtotal n`, `-tickcounter`, `-eventcounter`; `-o bench.csv,csv`.
- **Что мерить** (JSON-артефакт, «падение» = +25 % медианы из 5): открытие → первый растр (малый файл, спецификация, reference, 200 стр. с картинками, R6); листание 1…50 при 100 %/200 %; поиск по 1 310 стр.; сохранение после правки (incremental vs full); пиковая память (`PeakWorkingSetSize`).
- **Писать самим**: `tools/bench.exe` (движок) + `tools/bench_ui` (QBENCHMARK, offscreen), `tools/bench_compare.py`. **Грабли**: offscreen не показывает композицию/DWM; i7-13650HX гибридный — affinity на P-ядра или `-T 8`; 15,7 ГБ ОЗУ — `FZ_STORE_DEFAULT`.

### 6.6 Структура тестов в CMake/ctest для очереди

```
tests/
  unit/        # gtest, <1 с каждый, tests/data/mini/*.pdf (свои, ≤ 50 КБ)
  engine/      # тесты движка на корпусе (манифест), 1–10 с
  render/      # эталоны PNG, медленные
  ui/          # QTest + offscreen, снимки
  compat/      # qpdf/veraPDF/pdfcpu/pypdfium2, SKIP если нет
  bench/       # QBENCHMARK / bench.exe, пишут JSON
  corpus/      # manifest.json + fetch_corpus.py; файлы — вне репо
fuzz/          # libFuzzer цели
```
```cmake
include(GoogleTest)
enable_testing()
add_executable(unit_tests ...)
gtest_discover_tests(unit_tests
  DISCOVERY_MODE PRE_TEST            # «delays test discovery until just prior to test execution» — POST_BUILD запускает exe при сборке и падает без QT_QPA_PLATFORM/ASan-DLL
  DISCOVERY_TIMEOUT 30               # по умолчанию 5
  TEST_PREFIX "engine."
  PROPERTIES LABELS "unit" TIMEOUT 60
             ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_FONTDIR=C:/Windows/Fonts;PATH=${ASAN_BIN};$ENV{PATH}")

add_test(NAME render.corpus COMMAND render_tests --manifest ${CMAKE_SOURCE_DIR}/tests/corpus/manifest.json)
set_tests_properties(render.corpus PROPERTIES LABELS "render;slow" TIMEOUT 900
  TIMEOUT_AFTER_MATCH "120;^progress:"   # после совпадения часы сбрасываются, лимит 120 с
  RESOURCE_LOCK corpus_cache)

add_test(NAME corpus.fetch COMMAND py -3.14 -I ${CMAKE_SOURCE_DIR}/tests/corpus/fetch_corpus.py --only tag:render)
set_tests_properties(corpus.fetch PROPERTIES FIXTURES_SETUP corpus LABELS "slow" TIMEOUT 1800)
set_tests_properties(render.corpus PROPERTIES FIXTURES_REQUIRED corpus)   # setup добавится автоматически даже при -L/-R

add_test(NAME compat.qpdf COMMAND ${Python3_EXECUTABLE} -I tools/compat_check.py --tool qpdf ...)
set_tests_properties(compat.qpdf PROPERTIES LABELS "compat" SKIP_RETURN_CODE 77 TIMEOUT 300)
```
Семантика ctest (4.5): несколько `-L` — «form an AND relationship»; `--timeout` «effectively sets a timeout on all tests that do not already have a timeout set» (не перекрывает `TIMEOUT`); `TIMEOUT_AFTER_MATCH "<s>" "<regex>"`; `FIXTURES_SETUP/REQUIRED` (`-FS/-FC/-FA` отключают автодобавление); `RESOURCE_LOCK`; `--repeat until-pass:N`; `--output-junit file` (перезаписывает); `--no-tests=error|ignore`; `--stop-on-failure`; `-j N`; `--test-dir`. `py -I` запрещает импорт из cwd.
```bat
:: на каждом пункте плана
ctest --test-dir build -L unit -LE slow -j 12 --timeout 60 --output-on-failure --no-tests=error --output-junit build\unit.xml
:: в конце сессии
ctest --test-dir build -L "render|engine|compat" -j 6 --output-on-failure --repeat until-pass:2 --output-junit build\slow.xml
:: отдельно
build\fuzz\pdf_open_fuzzer.exe %CORPUS%\fuzz -max_total_time=600 -timeout=20 -rss_limit_mb=4096
```
Именование: `<слой>.<объект>.<свойство>`; gtest `TEST(Annot, CreateAllSubtypes)` + `TEST_PREFIX "engine."`. Таймауты: unit 60 с, engine 120 с, render 900 с, fuzz — `-max_total_time`. `requirements-test.txt` (версии текущие на 2026-10-21): `scikit-image==0.26.0 opencv-python-headless==5.0.0.93 imagehash==4.3.2 pillow==13.0.0 numpy==2.5.4 scipy==1.18.1 pypdfium2==5.14.0 pikepdf==10.17.0 pyhanko==0.38.0 pypdf==6.21.1`.

## 7. Этап 0 — что ставить и как (пошагово для этой машины)

Принципы: всё, что помечено «очередь», выполняется сессией `claude -p` без человека и без прав администратора; «человек» — требует UAC или кликов и делается отдельно. Загрузки — каждая в свой пустой каталог (`C:\Tools\dl\<имя>`), скрипты — отдельно (`C:\Tools\scripts`, UTF-8 с BOM); Python над скачанным — `py -3.14 -I`. Перед шагами 3–4 стоит проверить, что уже собрано в scratchpad (MuPDF и Qt можно перенести, а не ставить заново).

| № | Шаг | Кто | Время | Объём (скачать / на диске) |
|---|---|---|---|---|
| 0 | Окружение очереди: `settings.local.json`, пролог скриптов, каталоги | очередь | 5 мин | — |
| 1 | Python 3.14 venv для aqt и тестов | очередь | 1–2 мин | ~150 МБ (venv с scikit-image/opencv) |
| 2 | Qt 6.12.0 msvc2022_64 + qtimageformats + vcredist-tool | очередь | 4–6 мин | 266 МБ / 2,5 ГБ |
| 3 | MuPDF 1.28.5: клон, submodules, сборка Debug (+ Release) | очередь | клон+submodules ~3 мин; Debug 170 с; Release 563 с | ~0,5 ГБ исходники / 1,4 ГБ Debug + 2,4 ГБ Release |
| 4 | vcpkg + манифест (openssl, qpdf, tiff, fmt, spdlog, nlohmann-json, sqlite3, gtest) | очередь (фоновая команда с `timeout` 7 200 000) | 30–60 мин (оценка) | ~1–3 ГБ `buildtrees` + кэш |
| 5 | Portable-инструменты: qpdf, pdfcpu, SumatraPDF, ccache/sccache (по желанию) | очередь | 5 мин | ~60 МБ |
| 6 | veraPDF 1.30.3 (+ при отказе JBR 25 — Temurin 21 JRE zip) | очередь, проверка | 5–10 мин | 33 МБ (+49 МБ JRE) |
| 7 | tessdata_fast rus/eng/deu | очередь (нужно явное разрешение на загрузку) | 1 мин | 9,5 МБ |
| 8 | Корпуса и эталоны (клоны, fuzz-корпус, IRS, спецификации) | очередь (разрешение на загрузку) | 10–30 мин | ~1–2 ГБ |
| 9 | LibreOffice 26.8.1 | **человек** (UAC) или пробный `msiexec /a` | 5 мин | 358 МБ / ~1 ГБ (не измерено) |
| 10 | Необязательное с UAC: LLVM 23 (можно tar.zst без админа), cppcheck MSI, Clang-компонент Build Tools, en-US пакет MSVC, Windows OCR FOD | человек | — | LLVM 264 МиБ zst / MSI 610 МиБ |
| 11 | Скелет проекта и smoke-тесты (CMake+Ninja+Qt+MuPDF, offscreen, imgcmp, fuzz-цель) | очередь | 1–2 сессии | — |

### 7.0 Окружение очереди (очередь, 5 мин)

- `settings.local.json` → `env`: `BASH_DEFAULT_TIMEOUT_MS=1800000`, `BASH_MAX_TIMEOUT_MS=7200000`, `CLAUDE_CODE_PRINT_BG_WAIT_CEILING_MS=0`, `VCPKG_MAX_CONCURRENCY=10`, `VCPKG_ROOT=%LOCALAPPDATA%\vcpkg`, `QT_ROOT=C:\Qt\6.12.0\msvc2022_64`, `QT_QPA_PLATFORM=offscreen`, `QT_QPA_FONTDIR=C:/Windows/Fonts`, `QT_FORCE_STDERR_LOGGING=1`, `PYTHONUTF8=1`, `PYTHONIOENCODING=utf-8`, `CCACHE_MSVC_DEP_PREFIX=Примечание: включение файла:`, `TESSDATA_PREFIX=C:\Tools\tessdata`. Свежему клону репозитория файл нужен обязательно (без него `claude -p` всё запрещает).
- Пролог каждого `.ps1` (§3.8); все `.ps1`/`.iss` с кириллицей — UTF-8 **с BOM**.
- Каталоги: `C:\Tools\dl\` (загрузки, по подпапке на архив), `C:\Tools\scripts\`, `C:\Tools\aqt`, `C:\Tools\venv-test`, `C:\Tools\qpdf`, `C:\Tools\pdfcpu`, `C:\Tools\verapdf`, `C:\Tools\tessdata` (ASCII-путь!), `C:\Qt`, `%LOCALAPPDATA%\vcpkg`, `%LOCALAPPDATA%\bny-acro\corpus`. Пути без пробелов и короткие (MAX_PATH).
- vcvars: `tools/vcenv.cmd` = `call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"` — даёт `cl`, `cmake 3.31.6`, `ninja 1.12.1`, `clang_rt.asan_dynamic-x86_64.dll` в PATH; `clang-tidy`/`clang-format` добавить: `set PATH=%VCINSTALLDIR%Tools\Llvm\x64\bin;%PATH%`.

### 7.1 Python (очередь, 1–2 мин)

```powershell
py -0p                                   # ожидается 3.14.6 (C:\Users\WetFingers\AppData\Local\Python\pythoncore-3.14-64\python.exe), 3.11, 3.15.0b3
py -3.14 -m venv C:\Tools\aqt
C:\Tools\aqt\Scripts\python.exe -m pip install --disable-pip-version-check "git+https://github.com/miurahr/aqtinstall.git"
C:\Tools\aqt\Scripts\python.exe -m aqt version
py -3.14 -m venv C:\Tools\venv-test
C:\Tools\venv-test\Scripts\python.exe -m pip install -r requirements-test.txt   # scikit-image==0.26.0 opencv-python-headless==5.0.0.93 imagehash==4.3.2 pillow==13.0.0 numpy==2.5.4 scipy==1.18.1 pypdfium2==5.14.0 pikepdf==10.17.0 pyhanko==0.38.0 pypdf==6.21.1
C:\Tools\venv-test\Scripts\pypdfium2 --help ; C:\Tools\venv-test\Scripts\pyhanko --help
```
Не использовать системный `python` (3.15.0b3) — нет wheel cp315 у pybcj/pyppmd/inflate64/brotli/scikit-image. Альтернатива: `winget install --id Python.Python.3.13 -e --scope user --silent` (3.13.15, per-user).

### 7.2 Qt 6.12.0 (очередь, 4–6 мин, 266 МБ / 2,5 ГБ)

```powershell
C:\Tools\aqt\Scripts\python.exe -m aqt list-qt windows desktop --arch 6.12.0           # должен показать win64_msvc2022_64
C:\Tools\aqt\Scripts\python.exe -m aqt install-qt windows desktop 6.12.0 win64_msvc2022_64 -m qtimageformats --outputdir C:\Qt
C:\Tools\aqt\Scripts\python.exe -m aqt install-tool windows desktop tools_vcredist qt.tools.vcredist_msvc2022_x64 --outputdir C:\Qt
Test-Path C:\Qt\6.12.0\msvc2022_64\plugins\platforms\qoffscreen.dll                    # обязательно True
C:\Qt\6.12.0\msvc2022_64\bin\qtdiag.exe                                                 # версия, компилятор msvc 19.44
```
Либо перенести уже установленный kit из `scratchpad\Qt\6.12.0` в `C:\Qt`. QtPdf и qtwebengine не ставить. Запасной вариант — 6.10.3 через `aqtinstall==3.3.0`; ручной — `.7z` по URL из `Updates.xml` (§2.2). Smoke: проект `scratchpad\smoke` (configure 14 с, цикл 46,5 с) — QPushButton + QSignalSpy + grab() под offscreen с `QT_QPA_FONTDIR`.

### 7.3 MuPDF 1.28.5 (очередь; Debug 170 с, Release 563 с)

```bat
git clone --depth 1 --branch 1.28.5 https://github.com/ArtifexSoftware/mupdf.git C:\src\mupdf
cd C:\src\mupdf && git submodule update --init --depth 1                      rem 137 с, 168 МиБ
"%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" platform\win32\mupdf.sln /t:mutool /p:Configuration=Debug   /p:Platform=x64 /p:PlatformToolset=v143 /m /v:m > C:\Tools\logs\mupdf-debug.txt
"%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" platform\win32\mupdf.sln /t:mutool /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 /m /v:m > C:\Tools\logs\mupdf-release.txt
platform\win32\x64\Release\mutool.exe draw -F txt -o - test.pdf              rem дымовой тест с кириллицей
platform\win32\x64\Release\mutool.exe draw -t rus+eng -d C:\Tools\tessdata -r 300 -o out.ocr.pdf scan.pdf   rem после шага 7: первый живой OCR-тест
```
Готовые сборки уже есть в `scratchpad\mupdf-src\mupdf\platform\win32\x64\{Release,Debug}` — достаточно перенести каталог. Release — долгая команда (>120 с) → фоновая с `timeout`; Debug укладывается в 3 мин. Не собирать `mupdf-gl`, `sodochandler`, `*Extra`. Зафиксировать список `.lib` для CMake (§1.18) и путь `include`.

### 7.4 vcpkg (очередь; первая установка 30–60 мин — фоновая команда)

```powershell
git clone https://github.com/microsoft/vcpkg.git "$env:LOCALAPPDATA\vcpkg"
& "$env:LOCALAPPDATA\vcpkg\bootstrap-vcpkg.bat" -disableMetrics
# vcpkg.json + CMakePresets.json из §3.2; триплет x64-windows-static-md-release (community) или overlay-копия
# первый configure проекта (тянет vcpkg install) — в фоне с timeout 7200000, лог в файл:
cmd /c "tools\vcenv.cmd && cmake --preset msvc-release > C:\Tools\logs\configure.txt 2>&1"
```
Замерить реальное время портов openssl/qpdf/tiff (оценки в §3.3 не подтверждены). Если OpenSSL нужен строго 3.5 LTS — `"overrides": [{ "name": "openssl", "version": "3.5.x" }]` по версиям из `versions/o-/openssl.json`, либо FireDaemon zip 3.5.9 (`C:\Tools\openssl`, динамическая линковка — тогда `libcrypto-3-x64.dll` рядом с exe, что заставит Qt пробовать OpenSSL-бэкенд первым; учесть).

### 7.5 Portable-инструменты (очередь, 5 мин)

```powershell
curl.exe -L -o C:\Tools\dl\qpdf\qpdf-12.4.2-msvc64.zip https://github.com/qpdf/qpdf/releases/download/v12.4.2/qpdf-12.4.2-msvc64.zip      # 28 183 147 Б; sha256 из qpdf-12.4.2.sha256
curl.exe -L -o C:\Tools\dl\pdfcpu\pdfcpu_0.16.3_Windows_x86_64.zip https://github.com/pdfcpu/pdfcpu/releases/download/v0.16.3/pdfcpu_0.16.3_Windows_x86_64.zip   # 7 071 702 Б; sha256 282cb436…d55
curl.exe -L -o C:\Tools\dl\sumatra\SumatraPDF-3.6.1-64.zip https://www.sumatrapdfreader.org/dl/rel/3.6.1/SumatraPDF-3.6.1-64.zip          # 10 031 830 Б
Expand-Archive C:\Tools\dl\qpdf\qpdf-12.4.2-msvc64.zip C:\Tools\qpdf ; Expand-Archive C:\Tools\dl\pdfcpu\pdfcpu_0.16.3_Windows_x86_64.zip C:\Tools\pdfcpu
winget install --id Mozilla.sccache --exact --scope user --silent     # или Ccache.Ccache — portable zip; необязательно
```
(Точные URL релизов qpdf/pdfcpu сверить по GitHub releases API — в отчётах подтверждены имена и размеры файлов, не полные URL.) Проверка: `qpdf --version`, `pdfcpu version`, `qpdf --check base.pdf` → 0.

### 7.6 veraPDF (очередь; Java проверить)

```powershell
java -version                                    # OpenJDK 25.0.3 (JBR); инсталлятор декларирует 8/11/17/21, ядро с 1.30.1 поддерживает 25
curl.exe -L -o C:\Tools\dl\verapdf\verapdf-installer.zip https://software.verapdf.org/rel/verapdf-installer.zip   # 32 971 125 Б
Expand-Archive C:\Tools\dl\verapdf\verapdf-installer.zip C:\Tools\dl\verapdf\x
# в распакованном: verapdf-install.bat auto-install.xml (имя батника по исходникам; docs пишут vera-install.bat — проверить); auto-install.xml с install path C:\Tools\verapdf и паками CLI
# при отказе JBR: Temurin 21 JRE zip (OpenJDK21U-jre_x64_windows_hotspot_21.0.12.1_1.zip, 48 999 141 Б, Adoptium API) → C:\Tools\jre21, $env:JAVA_HOME
C:\Tools\verapdf\verapdf.bat -f 0 --format json sample.pdf ; $LASTEXITCODE   # 0 valid / 1 invalid / 7-9 сбой
```

### 7.7 tessdata (очередь, 9,5 МБ; требует явного разрешения на загрузку)

```powershell
foreach ($l in 'rus','eng','deu') { curl.exe -L -o "C:\Tools\tessdata\$l.traineddata" "https://raw.githubusercontent.com/tesseract-ocr/tessdata_fast/main/$l.traineddata" }
curl.exe -L -o C:\Tools\tessdata\pdf.ttf https://raw.githubusercontent.com/tesseract-ocr/tessconfigs/main/pdf.ttf   # 572 Б, необязателен
# ожидаемые размеры: rus 3 861 738; eng 4 113 088; deu 1 525 436 — зафиксировать sha256 в tools/get-tessdata.ps1
```
`osd.traineddata` не качать (в сборке MuPDF `DISABLED_LEGACY_ENGINE`). Путь только ASCII.

### 7.8 Корпуса и эталоны (очередь; явное разрешение на загрузку)

```powershell
git clone --depth 1 https://pdfium.googlesource.com/pdfium_tests $C\pdfium_tests                 # BSD-3
git clone --depth 1 -b staging https://github.com/veraPDF/veraPDF-corpus $C\verapdf-corpus      # CC BY 4.0, 2 906 PDF
git clone --depth 1 https://github.com/pdf-association/pdf-differences $C\pdf-differences       # CC BY 4.0
git clone --depth 1 https://github.com/pdf-association/safedocs $C\safedocs                     # Apache-2.0
git clone --depth 1 https://github.com/pdf-association/pdf20examples $C\pdf20examples           # CC BY-SA 4.0
git clone --depth 1 https://github.com/bfosupport/pdfa-testsuite $C\bfo                         # CC BY 3.0
git clone --depth 1 --filter=blob:none --sparse https://github.com/mozilla/pdf.js $C\pdfjs; git -C $C\pdfjs sparse-checkout set test/pdfs
git clone --depth 1 --filter=blob:none --sparse https://github.com/esig/dss $C\dss; git -C $C\dss sparse-checkout set dss-pades/src/test/resources/validation
curl.exe -L -o $C\fuzz\public.zip https://storage.googleapis.com/mupdf-backup.clusterfuzz-external.appspot.com/corpus/libFuzzer/mupdf_pdf_fuzzer/public.zip   # 232 221 233 Б
curl.exe -L -o $C\irs\f1040.pdf https://www.irs.gov/pub/irs-pdf/f1040.pdf                        # 220 237 Б
curl.exe -L -o $C\spec\PDF32000_2008.pdf https://opensource.adobe.com/dc-acrobat-sdk-docs/pdfstandards/PDF32000_2008.pdf   # 22 491 828 Б
# опционально: git clone https://git.ghostscript.com/tests.git (размер неизвестен)
```
где `$C = $env:LOCALAPPDATA\bny-acro\corpus`. Затем сгенерировать свои наборы (§6.1): шифрование (5 файлов qpdf), подписи (B-B через pyHanko), аннотации (утилита на MuPDF), формы, кириллица (Word COM / LibreOffice), big-1200.pdf, мутанты (100, seed=1); эталоны `render/expected` (MuPDF 1.28.5, `-A 8 -N`, 72/150 dpi), контакт-листы, коммит с `expected.json`.

### 7.9 LibreOffice (человек — UAC; или пробный путь без прав)

```bat
rem вариант A (без админа — НЕ ПОДТВЕРЖДЁН): административная распаковка
curl.exe -L -o C:\Tools\dl\lo\LibreOffice_26.8.1_Win_x86-64.msi https://download.documentfoundation.org/libreoffice/stable/26.8.1/win/x86_64/LibreOffice_26.8.1_Win_x86-64.msi   rem 358 МБ; SHA256 из winget-манифеста C6298B10…B04B5312
msiexec /a "C:\Tools\dl\lo\LibreOffice_26.8.1_Win_x86-64.msi" /qn TARGETDIR="C:\Tools\libreoffice"
rem вариант B (человек): winget install --id TheDocumentFoundation.LibreOffice -e  (Scope: machine → UAC)
rem smoke: 
"C:\Tools\libreoffice\program\soffice.com" --headless --norestore -env:UserInstallation=file:///C:/Tools/lo-prof-0 --convert-to pdf --outdir C:\Tools\out C:\Tools\in\проба.docx
rem успех: код 0, stdout «convert … using filter : writer_pdf_Export», файл есть; ошибка: код ≠ 0 и «Error:» в stderr
```
Проверить подпапку `program\soffice.exe` и размер на диске; зафиксировать в `tools/get-libreoffice.ps1`.

### 7.10 Необязательное, требует человека

- Clang-компонент Build Tools (`setup.exe modify … --add Microsoft.VisualStudio.Component.VC.Llvm.Clang --add …ClangToolset --passive --norestart`) — UAC; даёт clang-cl 19.1.5.
- LLVM 23.1.4: без админа — `clang+llvm-23.1.4-x86_64-pc-windows-msvc.tar.zst` (276 626 313 Б) в `%LOCALAPPDATA%\Programs\LLVM` (распаковка zst: проверить `tar.exe` Windows или `zstd`); MSI — UAC. Нужен только для UBSan.
- cppcheck 2.22.0 — MSI (UAC); `msiexec /a` не проверен.
- `setup.exe modify --addProductLang en-US` — английские сообщения cl.exe (UAC); без него работают `msvc_deps_prefix`/`CCACHE_MSVC_DEP_PREFIX`.
- Windows OCR языки (`Language.OCR~~~ru-RU~0.0.1.0`) — через Параметры → Время и язык (DISM требует повышения).
- Inno Setup 6.7.3 уже установлен; обновление до 7.1.0 не требуется.

### 7.11 Скелет проекта (очередь, 1–2 сессии)

`CMakeLists.txt` (§2.3, `/utf-8`, `CMP0141`, `JOB_POOLS`), `CMakePresets.json` + `CMakeUserPresets.json`, `vcpkg.json`, `.clang-format`, `.clang-tidy`, `tools/{vcenv.cmd,build.ps1,lint.ps1,install-qt.ps1,get-tessdata.ps1,get-libreoffice.ps1,compat_check.py,fetch_corpus.py}`, `tests/` по §6.6, `tools/imgcmp`, `fuzz/pdf_open_fuzzer.cpp` (ASan+fuzzer), `installer/pdfstudio.iss` (§3.7), smoke-тесты: открыть PDF через `pdfcore` (MuPDF static libs + Qt offscreen), отрендерить страницу в QImage zero-copy, `QWidget::grab()`, `ctest -L unit`.

---

## 8. Что писать самим — сводная таблица (потребность → библиотека → разрыв)

Трудоёмкость: S — до сессии, M — 1–3 сессии, L — 3–8 сессий, XL — больше (с рисками).

| Потребность | Библиотека / инструмент | Что есть | Разрыв (писать самим) | Оценка |
|---|---|---|---|---|
| Просмотр страниц | MuPDF display lists + draw device; Qt Widgets | рендер, кэш store, многопоточность через клоны контекста | `MuContext` (замки, 5 полей), `DocumentWorker` + `RenderPool`, `PageView : QAbstractScrollArea` (раскладка, тайлы 512², `QCache`, HiDPI, zero-copy `QImage`), отмена через `fz_cookie` | L |
| Текст: выделение, копирование, поиск | `fz_stext_*`, `fz_search_stext_page`, `fz_new_search` (regex) | всё, кроме «целое слово» и замены | UI выделения, индекс по документу, панель поиска; «найти и заменить» = редактор текста | M |
| Аннотации (все типы) | `pdf_create_annot`, сеттеры, синтез AP для 18 типов | создание/правка/удаление, внешний вид | инструменты рисования, хит-тест, панель комментариев (`/IRT`, `/RC`), штампы (custom), Popup-логика, тестовая матрица 28 подтипов | L |
| Формы: заполнение | `pdf_*_widget`, события, MuJS + AF* | чтение/заполнение/вычисления | `app.alert` → Qt, UI виджетов, FDF-экспорт/импорт (только низкоуровневое чтение есть) | M |
| Формы: создание полей | `pdf_create_signature_widget` (образец) | только подписное поле | `createTextField/Checkbox/Radio/Combo/List` через `pdf_create_annot_raw` + `/FT /Ff /V /Opt /MK`, Kids/Parent, AP через `pdf_annot_request_synthesis`; XFA — нет и не будет | M |
| Упорядочивание страниц | `pdf_graft_mapped_page`, `pdf_rearrange_pages`, `pdf_delete_page_range`, `pdf_insert_page` | всё, кроме поворота | `/Rotate` через `pdf_dict_put_int`, панель миниатюр, drag-n-drop, объединение с outline/`/Dests` (merge их теряет) | M |
| Редактирование текста | `pdf_processor`/фильтры, `pdf_new_buffer_processor`, `pdf_update_stream`, `pdf_add_cid_font` | низкоуровневые кирпичи | модель текстовых объектов (`BT…ET`, `Tm/Td/TL`, шрифт→Unicode), WYSIWYG-правка, перегенерация `Tj/TJ`, подмена шрифтов, перераскладка | **XL** |
| Редактирование картинок | `pdf_add_image`, `pdf_update_stream`, `/Resources/XObject` | вставка/замена | UI (перемещение/масштаб через `cm`), экспорт `fz_save_pixmap_as_png/jpeg` | S–M |
| Office → PDF | LibreOffice headless; Word COM | конвертер есть, JSON-опции (PDF/A, теги) | `OfficeConverter` (поиск soffice, пул профилей, `QProcess` + таймаут/job object, разбор `Error:`), `WordExporter` (опц.), установка LO (UAC-вопрос) | M |
| PDF → DOCX/PPTX | Word COM; LibreOffice `writer/impress_pdf_import`; MuPDF `extract` | три пути разного качества | `WordConverter` (IDispatch, STA-поток, таймаут), fallback LO, честный UI; PDF → XLSX — только своё извлечение таблиц (`TABLE_HUNT`) | M |
| Пароли, права | `pdf_write_options` (AES-256 R6 проверено), `pdf_authenticate_password` | всё | диалоги, UI прав; линеаризация — qpdf | S |
| Подписи B-B/B-T | MuPDF плейсхолдер/ByteRange/инкремент, внешний вид; OpenSSL 3.5 `CMS_*` | каркас | свой `pdf_pkcs7_signer` (SHA-256, ESS v2, `CMS_NO_SIGNING_TIME`), патч `/V` (`ETSI.CAdES.detached`, `/Reason`…), TSA-клиент (RFC 3161), резерв 16 КиБ, PKCS#12 | L |
| Подписи B-LT/B-LTA, проверка | OpenSSL OCSP/CRL/TS API; `pdf_validate_signature` | ничего для LTV | DSS/VRI, DocTimeStamp (отдельный инкремент), валидатор (целостность без `CMS_CADES`, доверие с `CMS_CADES`, winstore + AATL/EUTL, retroactive), анализ изменений (P=2/3), DocMDP/Perms | **XL** |
| Windows CertStore / смарт-карты | CryptoAPI/CNG (`CryptUIDlgSelectCertificateFromStore`, `NCryptSignHash`) | — | внешняя подпись через CryptMsg или PKCS7-API OpenSSL + `NCryptSignHash`, P1363→DER для ECDSA, PIN-диалоги | L |
| Вымарывание | `pdf_redact_page` (image/line art/text режимы) | всё | UI пометки (`PDF_ANNOT_REDACT` + quad points), чистка `/Info`/XMP/вложений | S–M |
| OCR | Tesseract внутри MuPDF (`fz_new_ocr_device`, `fz_new_pdfocr_writer`), deskew | движок, невидимый слой, deskew | `OcrService` (очередь, прогресс, отмена), «распознать для поиска» без растеризации, выбор языка, загрузчик tessdata (инсталлятор), DPI-нормализация, **определение ориентации** (OSD недоступен) | M–L |
| HTML → PDF | `QTextDocument` + `QPdfWriter` | подмножество HTML 4/CSS 2.1 | `HtmlToPdf` (нормализация, `addResource`), ограничения в UI; QtWebEngine — опц. | S |
| Печать | `QPrinter` + растр MuPDF; GDI `StartDoc` | нативный диалог, растр | буклет, «вписать», автоповорот, печать комментариев, полосовой рендер, `/p` `/pt`, тихая печать в файл | M |
| Сканирование | WIA 2.0 | `GetImageDlg` одним вызовом; `IWiaTransfer` | `IScanSource` + `WiaScanSource` + `FakeScanSource`, диалог (dpi/цвет/ADF/дуплекс), конвейер скан → deskew → OCR → PDF | M |
| Изображения ↔ PDF, TIFF | MuPDF; libtiff 4.7.2 | открытие JPEG/PNG/TIFF, PNG/JPEG экспорт | `ImageToPdf` (EXIF, DCT как есть), `PdfToImages`, `TiffWriter` (многостраничный, CCITT G4) | S–M |
| Сжатие/оптимизация | `pdf_rewrite_images`, `pdf_clean_file`, `mutool audit`; qpdf | всё | `PdfOptimizer` с пресетами, «было/стало», линеаризация через qpdf | S–M |
| Undo/redo | `pdf_enable_journal`, `pdf_begin/end_operation` | есть | обёртка операций в UI, короткое удержание `pdf_page` | S |
| Устойчивость | `fz_try/fz_catch`, ASan, libFuzzer | есть в Build Tools | `BnyMu::call` обёртка, fuzz-цель, мутатор, регрессионные `load`-тесты | S–M |
| Тесты/качество | gtest/QtTest/ctest; qpdf, veraPDF, pdfcpu, pypdfium2, pyHanko | инструменты portable | `imgcmp`, `render_tests` + эталоны, `compat_check.py`, `fetch_corpus.py` + манифест, генераторы (шифрование, подписи, аннотации, кириллица, большие, мутанты), `bench` | M |
| Сборка/CI очереди | CMake 3.31 + Ninja + vcpkg + aqt | всё есть | `vcpkg.json`, пресеты, `build.ps1`, `JOB_POOLS`, sccache (без PCH) или PCH+unity, `.clang-tidy`, `/analyze`-пресет, SARIF-сводка | S–M |
| Упаковка | windeployqt, Inno Setup 6.7.3, app-local CRT | всё есть | `.iss` per-user, ProgID/Capabilities, `ms-settings:defaultapps`, LGPL-уведомления, иконка `.rc` | S |
| Локализация ru/en | `qt_add_translations` (6.7+ сигнатура), Linguist | есть | `.ts` в репо, `update_translations` явно, `qtbase_ru.qm` | S |

Крупные блоки (XL): **редактор текста** и **подписи B-LT/LTA + валидатор**. Крупные (L): просмотрщик с тайлами, аннотации, подписи B-B/B-T, CertStore/смарт-карты, OCR-сервис.

---

## 9. Открытые вопросы и риски

Собрано из блоков «Не выяснено» всех отчётов и утверждений с низкой уверенностью. Пометка **[риск]** — влияет на план; **[проверить]** — закрывается первой же сессией.

### 9.1 Общее по методу
- Во всех пяти проверках WebSearch был исчерпан (`CLAUDE_CODE_MAX_WEB_SEARCHES_PER_SESSION`) — сторонние отчёты о «граблях» (форумы, issues, QTBUG о медленном старте 6.12, сравнения SSIM-библиотек, малоизвестные корпуса) не просматривались; выводы основаны на документации, исходниках и собственных прогонах. wiki.qt.io (503), wiki TDF (Anubis), portableapps.com (403), twain.org (пусто), pdfa.org (403), helpx.adobe.com (403), web.archive.org — недоступны.

### 9.2 MuPDF
- **[риск]** MSVC-сборка `ReleaseExtra` с OpenSSL 3 не выполнялась; компилируемость `pkcs7-openssl.c` с OpenSSL 3 подтверждена только косвенно (заголовок OpenSSL 3.0, Debian trixie). Решение — не зависеть: собирать без `HAVE_LIBCRYPTO`, крипта своя.
- Поведение Acrobat и других валидаторов на подписи MuPDF с SHA-1 — не проверялось (нерелевантно при своём signer).
- Живой OCR-тест не выполнен (tessdata не скачивались — требуется разрешение на загрузку); проверены только сборка `libtesseract` и API. **[проверить]** в этапе 0 (`mutool draw -t rus`).
- Время сборки полного решения (mupdf-gl, javaviewer, mupdfcpp) не измерялось; измерены только `mutool` Release/Debug.
- Качество PDF→DOCX/ODT через `extract` на сложных документах не проверялось; xlsx/pptx через MuPDF вживую не тестировались (только docx).
- Содержимое патч-релизов 1.28.1/1.28.2/1.28.4/1.28.5 в CHANGES не детализировано.
- Размер «клона без submodules» из исходного отчёта (127 МБ) не воспроизведён — измерено 85 МиБ.
- Совместимость `platform/win32/mupdf.sln` с MSVC 14.44/SDK 26100 подтверждена сборкой; интеграция `.lib` в CMake-проект Qt — **[проверить]** (LTCG, размер линковки, время).
- Docs/код расхождения: `jbig2` в `mutool clean` без `FZ_RECOMPRESS_JBIG2`; JPEG-вывод в CLI `mutool draw` не документирован; семантика возврата `fz_ocr_progress_fn` не задокументирована; с какой версии `pdf_clean_options.subset_fonts` в API.
- Совместим ли vcpkg-порт lcms 2.19.1 с MuPDF для `FZ_ENABLE_ICC` (upstream — свой форк) — только при overlay-порте.

### 9.3 Qt
- Плановые даты патчей 6.12.1/6.12.2/6.12.3 (12.11.2026 / 14.01.2027 / 04.03.2027) и 6.11.3 (23.10.2026) не проверены (wiki.qt.io 503).
- **[риск]** Релиз aqtinstall с поддержкой Qt 6.11+/6.12 и версионированных расширений: PR #1048 open; 3.3.0 — последний на PyPI → очередь зависит от master GitHub (зафиксировать коммит в `install-qt.ps1`).
- Время сборки qtbase через vcpkg и возможность собрать QtPdf через `qtwebengine[pdf]` без полной сборки Chromium — не измерялось/не проверено (не нужно при своём виджете).
- Поведение стиля `windows11` и тёмной палитры под offscreen — не проверялось.
- Отчёты о «долгом старте» Qt 6.12 на Windows — не найдены (измерять `qtdiag`).
- Применимость ISCC-команды с `/Qp /O /F /D /S` к Inno 7.1.0 — по документации да, не запускалась.
- Открытая длительность патчей 6.12.x — выведена из политики 2020 г. и практики 6.8/6.5; прямого заявления нет.
- Причина формулировки aqt «As of Qt 6.7 this also lists extensions» при каталоге extensions с 680 — не выяснялась.
- Практическое поведение offscreen на Windows (шрифты, DPI, снимки) подтверждено одним smoke-тестом — **[проверить]** на реальных виджетах.

### 9.4 Сборка, зависимости, очередь
- **[риск]** Реальное время сборки тяжёлых портов vcpkg (openssl, tiff, qpdf; при overlay — tesseract+leptonica, libmupdf, harfbuzz) на этой машине — только оценки автора; замерить с триплетом `x64-windows-static-md-release`.
- Фактическая работа vcpkg с русским-only MSVC — `vcpkg install` ещё не запускался.
- Корректно ли sccache определяет русский префикс `/showIncludes` при chcp 65001 (`CP_OEMCP`) — не проверено; для ccache префикс задаётся явно. Поддерживает ли ccache PCH для MSVC — не найдено; sccache PCH не поддерживает точно. Версия ccache, в которой появился `msvc_dep_prefix`, не найдена.
- Пиковое потребление памяти `cl.exe` на Qt-тяжёлых TU и безопасное `-j` при 15,7 ГБ — не измерено (старт с `-j 10`).
- Ставятся ли LLVM.LLVM (wix MSI), Cppcheck (wix MSI), QPDF (nullsoft `Scope: machine`), LibreOffice (MSI), Temurin MSI через winget без UAC — не проверено; есть portable-альтернативы для всего, кроме LibreOffice и cppcheck.
- Работает ли UBSan через clang-cl из отдельного LLVM 23 с MSVC-линковкой — не проверено; распаковка `.tar.zst` штатным `tar.exe` — не проверена.
- Размер статической `libmupdf.lib` со вшитыми шрифтами известен (741 МБ Release / 425 МБ Debug), время линковки GUI-приложения — не измерено.
- Каталог ярлыков winget для portable-пакетов (`…\WinGet\Links`) документацией не подтверждён.
- Итог фоновой установки aqt на Python 3.15.0b3 не перепроверялся (причина ясна — нет cp315-wheel).

### 9.5 Подписи
- **[риск]** Точные формулировки статусов подписи в UI Acrobat не получены (helpx 403) — UI-тексты проектировать по ETSI/pyHanko, не копировать Acrobat.
- Может ли Acrobat Standard (не Pro) создавать сертифицирующие подписи (DocMDP) — не выяснено (влияет на объём: оставить DocMDP в плане как «паритет с Pro»).
- Механизм LTV Acrobat при `adbe.pkcs7.detached` (`adbe-revocationInfoArchival` в CMS vs DSS) — документация говорит лишь о соответствии ETSI TS 102 778-4.
- Поддержка RSA-PSS в Acrobat и ограничения (CNG vs CryptoAPI) — в Adobe-доках не найдена.
- Как Acrobat хранит Fill & Sign (тип аннотации или сплющивание) — не проверялось.
- Состав полного MSI ShiningLight (`lib/VC/x64/MD*`, MT) — не подтверждён.
- Содержимое `pyhanko_testing_commons/test_data/data` (список файлов) не загружено; модель результата pyHanko по API-докам не подтверждена; CLI pyHanko для локального тестового TSA — не проверен.
- ETSI Signature Conformance Checker — доступен, но тестовых файлов не найдено; корпус pdf.js на подписи не проверялся.
- Точный текст ISO 32000-1 §12.8.2.2 (DocMDP, единственность сертифицирующей подписи) и §12.8.3.3 — PDF > 10 МБ лимита загрузчика; P=1/2/3 и «certification first» подтверждены pyHanko/PDFBox/MuPDF.
- Что именно Acrobat хэширует для ключа VRI (hex-строку по букве ETSI или байты) — не проверено; в baseline VRI не рекомендован.
- Дословный текст EN 319 142-1 §4.1 a) про единственный SignerInfo — найден только заголовок; требование взято из EN 319 122-1.
- Названия пунктов GUI Acrobat «PKCS#7 Detached / CAdES-Equivalent» — по памяти.
- Привязка `tl12`/`eutl12.acrobatsecuritysettings` именно к Acrobat — документацией не подтверждена (файлы существуют и свежие).
- Утверждение «`-out /dev/null` в mingw-сборке не работает» — из исходного отчёта, не перепроверялось.

### 9.6 Конвертация, OCR, печать, сканирование
- **[риск]** Работает ли `msiexec /a` для LibreOffice MSI без прав администратора, подпапка `soffice.exe` в `TARGETDIR` и размер на диске — проверить в этапе 0; при провале LibreOffice ставит человек (UAC). Актуальная 26.x-сборка LibreOffice Portable и ключи тихой распаковки `.paf.exe` — не подтверждены.
- Какие флаги soffice минимально необходимы для headless-конвертации (утверждение из README unoserver не найдено).
- Точная структура результата `writer_pdf_import` в Writer (фреймы на строку/абзац) — README говорит только о Draw-фигурах.
- Имя optional feature «Microsoft Print to PDF» документацией не подтверждено; принтер проверять через `EnumPrinters`.
- Делает ли Word PDF Reflow OCR сканов и появляется ли диалог «Word will now convert…» при COM-открытии с `ConfirmConversions:=False` — **[проверить]** на машине.
- Требует ли `Add-WindowsCapability -Online` повышения официально (локально — да для `Get-WindowsCapability`).
- Значение `OcrEngine.MaxImageDimension` и официальные метрики качества Windows.Media.Ocr — не указаны.
- Совместимость QtWebEngine `printToPdf` с offscreen и реальный размер деплоя (≥150 МБ — по опыту) — не проверены.
- Актуальная версия спецификации TWAIN; имя `wiaaut.dll` — по памяти.
- Имя переменной Tesseract 5 для Sauvola/адаптивного Otsu (`thresholding_method` — по памяти); манифест `activeCodePage` UTF-8 как обход узкого `fopen` — по памяти.
- «`QTextDocument` игнорирует `@page`, `position:absolute`, веб-шрифты» — по опыту, документацией не подтверждено.
- Определение ориентации страницы без OSD — **[риск]** для качества OCR: нужен собственный алгоритм (4 поворота + `MeanTextConf`) или сборка Tesseract без `DISABLED_LEGACY_ENGINE`.

### 9.7 Тесты и корпуса
- Лицензия тестовых PDF в mozilla/pdf.js `test/pdfs` не задокументирована — не коммитить.
- pdfa.org (Isartor оригинал, Matterhorn, PDF/UA Reference Suite, ISO 32000-2 бесплатно) — 403; условия использования Isartor внутри veraPDF-corpus (CC BY 4.0 репозитория vs собственные условия) не проверены.
- Состав и размер Ghostscript/MuPDF `tests.git` — ls-remote работает, клон не выполнялся.
- SafeDocs Issue Tracker corpus — `corpora.tika.apache.org` не разрешается DNS; зеркала не найдены.
- Лицензия отдельных подписанных PDF в esig/dss (помимо LGPL-2.1 репозитория) не оговорена.
- Открытые наборы PDF с кириллицей (CID/Identity-H, не встроенные шрифты, Type1 с Differences) — не найдены; официальный текст ГК РФ ст. 1259 п. 6 подтверждён только по копии на ru.wikisource.org.
- Полная матрица «все подтипы аннотаций» в открытых корпусах не найдена — генерировать.
- Содержимое `mupdf_pdf_fuzzer/public.zip` (число файлов, не-PDF) не проверено; число страниц `PDF32000_2008.pdf` (~756) и `pdfreference1.7old.pdf` (~1 310) не подтверждено.
- Может ли headless Chrome открыть PDF для проверки «открывается ли в браузере» — не проверено (Edge на машине отсутствует).
- Запустится ли инсталлятор/CLI veraPDF 1.30.3 на JBR OpenJDK 25.0.3 — не проверено; реальное имя batch-файла (`verapdf-install.bat` vs `vera-install.bat`) — проверить распаковкой.
- Публичных ориентиров производительности Acrobat не найдено.

### 9.8 Решения, которые нужно принять до старта
1. Профиль подписи по умолчанию: `ETSI.CAdES.detached` + B-T (TSA какой? DigiCert/Sectigo публичные без аутентификации; офлайн-очередь — локальный `openssl ts`).
2. OpenSSL: vcpkg 3.6.5 (статика, проще сборка, поддержка до 2026-11-01) vs 3.5 LTS через overrides/FireDaemon (динамика, DLL рядом с exe).
3. LibreOffice: ждать человека (UAC) или начать с Word COM + «LibreOffice не найден» в UI.
4. OCR: мириться с отсутствием OSD (своё определение ориентации) или пересобирать `libtesseract` без `DISABLED_LEGACY_ENGINE` (+10 МБ osd, legacy-движок).
5. Кэш компиляции: sccache без PCH или PCH+unity без кэша.
6. Debug-сборка MuPDF для цикла очереди и Release — только для эталонов рендера и релизов (разные эталоны? — фиксировать, что эталоны от Release).
7. Лицензионная политика корпусов: только CC BY/BSD/Apache/CC0 в репозитории; CC BY-SA (pdf20examples) и DSS — только через манифест.

---

## 10. Источники (основные)

- **MuPDF:** mupdf.readthedocs.io/en/latest (index; guide/install; reference/c/overview; reference/c/fitz/error; cookbook/c/multi-threaded; reference/common/pdf-write-options, search-options, document-writer-options; reference/javascript/types/PDFAnnotation, PDFPage; tools/mutool-draw, mutool-clean, mutool-audit; other/third-party, other/changes); artifex.com/blog/mupdf-removes-linearisation; `gh api` теги/коммиты ArtifexSoftware/mupdf; локальный клон тега 1.28.5 (`CHANGES`, `COPYING`, `include/mupdf/**`, `source/**`, `platform/win32/*.vcxproj`, `docs/**`, `.gitmodules`, `Makerules`, `Makethird`) и артефакты `scratchpad\build-log`, `smoke-тест`, `verify-out`, `verify-scripts`; vcpkg raw `ports/libmupdf/*`; openssl-3.0 `pkcs12.h.in`; packages.debian.org/trixie/mupdf; OSS-Fuzz `projects/mupdf/build.sh`.
- **Qt:** qt.io/blog (6.12, 6.11.x, offering-changes-2020); download.qt.io (official_releases, archive, online/qtsdkrepository `Updates.xml`); doc.qt.io/qt-6 (windows, supported-platforms, whatsnew65/67/68/611/612, qpdfdocument, qtpdf-index 6.3/6.4, qimage, highdpi, qprinter, qprintdialog, qtest, qtest-overview, qguiapplication, qstylehints, qstylefactory, qfiledialog, ssl, debug, windows-deployment, qt-generate-deploy-app-script, qtlinguist-cmake-qt-add-translations, licenses-used-in-qt, appicon, richtext-html-subset, qtextdocument, qpagedpaintdevice, qpdfwriter 6.8); doc-snapshots.qt.io (qtpdf, qtwebengine); qt-project.atlassian.net REST (QTBUG-149519, QTBUG-149634); qtbase (qoffscreenintegration.cpp, qfreetypefontdatabase.cpp, qlogging.cpp, src/plugins/platforms/CMakeLists.txt, src/printsupport/CMakeLists.txt, styles/modernwindows/CMakeLists.txt); aqtinstall (PyPI JSON, GitHub CHANGELOG, issue #1047, PR #1048); qt.io/licensing/open-source-lgpl-obligations.
- **Сборка:** learn.microsoft.com (vcpkg get-started, supported-hosts, vcpkg-json, cmake-integration, triplets, binarycaching, config-environment; /utf-8, /std, visual-cpp-language-conformance, /analyze, /fsanitize, asan, workload-component-id-vs-build-tools, use-command-line-parameters, redistributing-visual-cpp-files, about_Character_Encoding, winget install/settings, default-programs, launch-default-apps-settings, register-a-file-type, docinfow, fa-verbs); cmake.org (cmake-presets, UNITY_BUILD, target_precompile_headers, CMAKE_MSVC_DEBUG_INFORMATION_FORMAT, JOB_POOLS, LANG_CLANG_TIDY, ctest, GoogleTest, TIMEOUT*, FIXTURES_*, release/4.0); ccache.dev (manual, releasenotes); sccache (README, Local.md, msvc.rs); ninja-build.org manual, ninja.cc; jrsoftware.org (privilegesrequired, consts, registry, changesassociations, overridesallowed, compilercmdline, isppcc, isdl); code.claude.com/docs (tools-reference, env-vars, headless); openssl-library.org (source, releasestrat); kb.firedaemon.com 4000121705; winget-pkgs манифесты; GitHub releases API (LLVM, cppcheck, qpdf, pdfcpu, SumatraPDF, Catch2, veraPDF); Adoptium API; PyPI JSON; PEP 790; локальные проверки (vswhere, state.json, vcvars64, `winget show`, реестр, `Get-Printer`, `py -0p`, `java -version`).
- **Подписи:** ETSI EN 319 142-1 V1.2.1 (PDF) и каталог редакций; openssl-3.5 (CHANGES.md, cms.h.in, pkcs7.h.in, ts.h, CMS_verify.pod, CMS_final.pod, PKCS12_parse.pod, OCSP_*.pod, OSSL_HTTP_transfer.pod, X509_VERIFY_PARAM_set_flags.pod, OSSL_STORE-winstore(7), winstore_store.c, e_capi.c, openssl-cms/openssl-ts/openssl-pkcs12 man); adobe.com/devnet-docs/acrobatetk (PrefRef/Windows/Security.html, DigSigDC/planning.html, standards.html, appearances.html, compare-versions); Microsoft Learn (CryptUIDlgSelectCertificateFromStore, CryptAcquireCertificatePrivateKey, NCryptSignHash, PFXImportCertStore, Export-PfxCertificate, CryptMsgOpenToEncode, CMSG_SIGNER_ENCODE_INFO); RFC 3161, 5035, 5816, 6960; MuPDF 1.28.5 (`pdf-form.c`, `pdf-signature.c`, `pdf-write.c`, `pdf-xref.c`, `pdfsign.c`, `pkcs7-openssl.c`, `form.h`, `crypt.h`, `libpkcs7.vcxproj`); pyHanko docs/исходники; PDFBox `SigUtils`; esig/dss contents API; iText 5 javadoc; freetsa.org; живые TSA-запросы; trustlist.adobe.com (HEAD).
- **Конвертация/OCR/печать/скан:** libreoffice.org/download; download.documentfoundation.org; help.libreoffice.org (start_parameters, pdf_params); vmiklos.hu/blog/pdf-convert-to; LibreOffice core (pdf_import_filter.xcu, sdext/README.md, filters/*.xcu, dispatchwatcher.cxx, app.cxx, loader.cxx); unoserver (server.py, README.rst); support.microsoft.com (edit-pdf-content-in-word, server-side automation of Office); learn.microsoft.com (word.document, documents.open, saveas2, wdsaveformat, wdexportformat, exportasfixedformat, #import, IDispatch::Invoke, automate-excel-from-c, OcrEngine, features-on-demand-language-fod, add-windowscapability, winrt-apis-desktop-apps, WIA start page/IWiaDevMgr2/GetImageDlg/IWiaTransfer::Download, wiaaut, wia-ips-*/wia-ipa-*); Tesseract (releases, baseapi.h, renderer.h, pdfrenderer.cpp, baseapi.cpp, serialis.cpp; tessdoc Data-Files, ImproveQuality; tessdata/tessdata_fast/tessdata_best/tessconfigs contents API); Leptonica (skew.c, binarize.c); twain-dsm; libtiff (multi_page, TIFFOpen); qpdf docs (installation, cli, encryption); vcpkg ports (tesseract, leptonica, tiff, qpdf).
- **Тесты:** pdf.js (test/pdfs trees API, .gitignore, wiki Contributing); pdfium (pdfium_tests, fx/, LICENSE, pngdiffer.py, README, pdfium_test.cc, fuzzers/); veraPDF (corpus trees API, veraPDF-apps, CliConstants.java, VeraPdfCliProcessor.java, library releases, software.verapdf.org, docs install/cli); BFO; pdf-differences; safedocs; pdf20examples LICENSE.md; pdf-corpora README; digitalcorpora (CC-MAIN, GovDocs1); OSS-Fuzz corpora; skimage.metrics; imagehash; pypdfium2 (shell_api); SumatraPDF CLI docs; cornell.edu 17 U.S.C. §105; ru.wikisource ГК РФ гл. 70; `curl -I`, `git ls-remote` (2026-10-21).
