<#
.SYNOPSIS
    Office-файлы из Word: tests/fixtures/office/проба.docx и заметка.rtf (кириллица и картинка).

.DESCRIPTION
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\gen\make_office_fixtures.ps1 [-Out <корень>] [-Python <python.exe>]

    Что: Word через COM (невидимый, без диалогов):
      проба.docx  — заголовок «Привет», абзац кириллицы, картинка tests/fixtures/images/photo.jpg;
      заметка.rtf — заголовок «Заметка», абзацы кириллицы и латиницы, картинка logo.png.
    Затем tools/gen/normalize_word.py убирает из файлов всё, что Word пишет от сессии и
    пользователя: rsid, paraId/textId, docId, автора, даты, время правки; ZIP — с датой 1980-01-01.
    xlsx/pptx/odt делает tools/gen/make_office_fixtures.py (openpyxl, python-pptx, odfpy).
    Откуда: Microsoft Word (COM), Python — venv C:\Tools\venv-test ($env:OTTISK_PYTHON).
    Детерминизм: после normalize_word.py; если Word всё же даёт разные байты — файл помечен
    в tools/gen/nondeterministic.txt и исключён из check_determinism.ps1.
    Грабли COM: путь в метод Word передавать как [string] (обёрнутая PSObject-строка из Join-Path
    подвешивает SaveAs2), каталог — полным именем, не 8.3; Close/Quit — без аргументов.
    Код выхода 0 — оба файла созданы. Файл в UTF-8 с BOM: Windows PowerShell 5.1 иначе читает кириллицу неверно.
#>
[CmdletBinding()]
param(
    [string]$Out = '',
    [string]$Python = ''
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding $false

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $Out) { $Out = $repo }
if (-not $Python) { $Python = $env:OTTISK_PYTHON }
if (-not $Python) { $Python = 'C:\Tools\venv-test\Scripts\python.exe' }

$dst = Join-Path $Out 'tests\fixtures\office'
New-Item -ItemType Directory -Force -Path $dst | Out-Null
$work = Join-Path (Get-Item -LiteralPath $env:TEMP).FullName 'ottisk-gen\word-office'
New-Item -ItemType Directory -Force -Path $work | Out-Null
$rawDocx = [string](Join-Path $work 'proba.raw.docx')
$rawRtf = [string](Join-Path $work 'zametka.raw.rtf')
$images = Join-Path $Out 'tests\fixtures\images'
if (-not (Test-Path (Join-Path $images 'photo.jpg'))) { $images = Join-Path $repo 'tests\fixtures\images' }
$photo = [string](Join-Path $images 'photo.jpg')
$logo = [string](Join-Path $images 'logo.png')
foreach ($f in @($photo, $logo)) { if (-not (Test-Path $f)) { throw "нет картинки $f — сначала make_images.py" } }
foreach ($f in @($rawDocx, $rawRtf)) { if (Test-Path $f) { Remove-Item -LiteralPath $f -Force } }

$t0 = Get-Date
function Step([string]$m) { [Console]::Error.WriteLine(('  {0,6:N1} s  {1}' -f ((Get-Date) - $t0).TotalSeconds, $m)) }

function Add-Para([object]$sel, [string]$text, [string]$font, [double]$size) {
    $sel.Font.Name = $font
    $sel.Font.Size = $size
    $sel.TypeText($text)
    $sel.TypeParagraph()
}

$word = $null
$doc = $null
$doc2 = $null
try {
    $word = New-Object -ComObject Word.Application
    $word.Visible = $false
    $word.DisplayAlerts = 0
    Step 'Word started'

    # --- проба.docx ---
    $doc = $word.Documents.Add()
    $sel = $word.Selection
    $sel.Style = $doc.Styles.Item(-2)            # wdStyleHeading1
    $sel.TypeText('Привет')
    $sel.TypeParagraph()
    $sel.Style = $doc.Styles.Item(-1)            # wdStyleNormal
    Add-Para $sel 'Это проба для преобразования Office → PDF. Оттиск открывает документы, показывает страницы и помогает работать с ними без лишних окон. Съешь же ещё этих мягких французских булок, да выпей же чаю.' 'Times New Roman' 12
    Add-Para $sel 'Вторая строка: латиница Hello, цифры 1234567890, знаки «» — №.' 'Arial' 11
    $pic = $sel.InlineShapes.AddPicture($photo)
    $pic.Width = 300
    $pic.Height = 200
    $sel.TypeParagraph()
    Add-Para $sel 'Подпись под картинкой.' 'Arial' 10
    $doc.SaveAs2($rawDocx, 16)                   # wdFormatDocumentDefault
    Step 'docx saved'

    # --- заметка.rtf ---
    $doc2 = $word.Documents.Add()
    $sel = $word.Selection
    $sel.Style = $doc2.Styles.Item(-2)
    $sel.TypeText('Заметка')
    $sel.TypeParagraph()
    $sel.Style = $doc2.Styles.Item(-1)
    Add-Para $sel 'Короткая заметка в формате RTF: кириллица, Latin, Grüße, числа 3,14.' 'Arial' 11
    Add-Para $sel 'Каждая страница рисуется заново при изменении масштаба, поэтому мелкий шрифт остаётся чётким.' 'Times New Roman' 12
    $pic2 = $sel.InlineShapes.AddPicture($logo)
    $pic2.Width = 96
    $pic2.Height = 96
    $sel.TypeParagraph()
    $doc2.SaveAs2($rawRtf, 6)                    # wdFormatRTF
    Step 'rtf saved'
}
finally {
    # Close/Quit с аргументом требуют [ref] — закрываем без сохранения через Saved = true
    foreach ($d in @($doc, $doc2)) {
        if ($d) { $d.Saved = $true; $d.Close(); [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($d) }
    }
    if ($word) { $word.Quit(); [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($word) }
    [GC]::Collect()
    [GC]::WaitForPendingFinalizers()
}

$norm = Join-Path $PSScriptRoot 'normalize_word.py'
$docx = Join-Path $dst 'проба.docx'
$rtf = Join-Path $dst 'заметка.rtf'
& $Python -X utf8 -I $norm docx $rawDocx $docx
if ($LASTEXITCODE -ne 0) { throw "normalize_word.py docx: код $LASTEXITCODE" }
& $Python -X utf8 -I $norm rtf $rawRtf $rtf
if ($LASTEXITCODE -ne 0) { throw "normalize_word.py rtf: код $LASTEXITCODE" }
Write-Output ("office: проба.docx {0} байт, заметка.rtf {1} байт, Word {2:N1} с" -f `
    (Get-Item -LiteralPath $docx).Length, (Get-Item -LiteralPath $rtf).Length, ((Get-Date) - $t0).TotalSeconds)
exit 0
