# АкроБатя

Настольный PDF-редактор для Windows уровня Adobe Acrobat Standard — личная программа семьи: просмотр и печать,
правка текста в PDF, комментарии и Fill & Sign, формы, подписи PAdES, страницы, создание и конвертация, защита,
OCR и сканирование. Интерфейс — как у нового Acrobat, по-русски. Ставится на пользователя, без прав администратора.

C++20 · Qt 6.12 Widgets · MuPDF 1.28.5 (с Tesseract) · OpenSSL 3.5 · CMake + Ninja · MSVC 2022.
Что и зачем — `docs/design.md` (эталон); план — `docs/plan.md`; правила работы — `CLAUDE.md`; очередь — `QUEUE.md`.

## Что нужно на машине

Build Tools 2022 17.14 (MSVC 14.44, CMake, Ninja, clang-format 19.1.5), Qt 6.12.0 в `C:\Qt\6.12.0\msvc2022_64`,
MuPDF 1.28.5, собранный в `C:\src\mupdf` (`tools\mupdf-build.ps1`), vcpkg в `C:\Tools\vcpkg`, Python 3.14
(`py -3.14`) и venv тестов `C:\Tools\venv-test` (`tools\requirements-test.txt`), инструменты `C:\Tools\{qpdf,pdfcpu,
verapdf,tessdata}`, Inno Setup 6 (per-user). Проверить всё разом: `.\run-queue.ps1 -Setup` (ничего не ставит).

## Сборка и проверка

```powershell
cmd /c "tools\vcenv.cmd && cmake --preset msvc-debug"                       # configure (первый раз ставит порты vcpkg, 30–60 мин)
cmd /c "tools\vcenv.cmd && cmake --build --preset msvc-debug"               # сборка в build\debug
cmd /c "tools\vcenv.cmd && ctest --test-dir build\debug -L unit --output-on-failure"
py -3.14 tools\verify.py                                                     # всё сразу: сборка, формат, стражи, тесты
```

`tools\vcenv.cmd` — окружение MSVC (vcvars64) плюс clang-format/clang-tidy в `PATH`. Программа из сборки:
`build\debug\bin\AcroBatya.exe` (с `C:\Qt\6.12.0\msvc2022_64\bin` в `PATH`); `--self-check` — самопроверка без окна,
`--shots home --out <каталог>` — снимок окна без экрана. Выпуск и установщик: `tools\build.ps1` →
`dist\AcroBatya-<версия>-setup.exe` (тихо: `/VERYSILENT`).

## Лицензии

MuPDF — AGPL-3.0 (программа не продаётся; исходники и текст лицензии — в «О программе»), Qt — LGPL-3.0
(динамические DLL), остальное — `THIRD-PARTY-NOTICES.md` и `installer/licenses/`.
