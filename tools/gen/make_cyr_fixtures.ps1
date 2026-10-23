<#
.SYNOPSIS
    Кириллица из Word: tests/fixtures/cyr/word-truetype.pdf (TrueType-подмножества Word, теги, закладки).

.DESCRIPTION
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\gen\make_cyr_fixtures.ps1 [-Out <корень>] [-Python <python.exe>]

    Что: Word через COM (Word.Application, невидимый, без диалогов) собирает документ: заголовок,
    абзацы Times New Roman и Arial с кириллицей, таблица, картинка tests/fixtures/images/photo.jpg —
    и экспортирует PDF (ExportAsFixedFormat, формат 17 = wdExportFormatPDF, с тегами и закладками
    по заголовкам). Затем tools/gen/normalize_word.py делает файл детерминированным: /Info
    (автор — не пользователь машины), даты, XMP, теги подмножеств шрифтов, /ID.
    CID (Identity-H) и Type1 с /Differences — в tests/data/mini (make_mini.py).
    Откуда: Microsoft Word (COM), Python — venv C:\Tools\venv-test ($env:OTTISK_PYTHON).
    Детерминизм: после normalize_word.py; если Word всё же даёт разные байты (раскладка
    зависит от версии Word и Normal.dotm) — файл помечен в tools/gen/nondeterministic.txt.
    Word COM — только отсюда: в venv нет pywin32. Word закрывается в finally.
    Код выхода 0 — файл создан. Файл в UTF-8 с BOM: Windows PowerShell 5.1 иначе читает кириллицу неверно.
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

$dst = Join-Path $Out 'tests\fixtures\cyr'
New-Item -ItemType Directory -Force -Path $dst | Out-Null
# Word.SaveAs2 зависает на коротких путях 8.3 (C:\Users\WETFIN~1\…) — берём полное имя каталога
$work = Join-Path (Get-Item -LiteralPath $env:TEMP).FullName 'ottisk-gen\word-cyr'
New-Item -ItemType Directory -Force -Path $work | Out-Null
$docx = Join-Path $work 'word-truetype.docx'
$rawPdf = Join-Path $work 'word-truetype.raw.pdf'
$photo = Join-Path $Out 'tests\fixtures\images\photo.jpg'
if (-not (Test-Path $photo)) { $photo = Join-Path $repo 'tests\fixtures\images\photo.jpg' }
if (-not (Test-Path $photo)) { throw "нет картинки $photo — сначала make_images.py" }
foreach ($f in @($docx, $rawPdf)) { if (Test-Path $f) { Remove-Item -LiteralPath $f -Force } }

function Add-Para([object]$sel, [string]$text, [string]$font, [double]$size, [int]$bold = 0) {
    $sel.Font.Name = $font
    $sel.Font.Size = $size
    $sel.Font.Bold = $bold
    $sel.TypeText($text)
    $sel.TypeParagraph()
}

$t0 = Get-Date
function Step([string]$m) { [Console]::Error.WriteLine(('  {0,6:N1} s  {1}' -f ((Get-Date) - $t0).TotalSeconds, $m)) }
$word = $null
$doc = $null
try {
    $word = New-Object -ComObject Word.Application
    $word.Visible = $false
    $word.DisplayAlerts = 0
    Step 'Word started'
    $doc = $word.Documents.Add()
    $sel = $word.Selection
    $sel.Style = $doc.Styles.Item(-2)            # wdStyleHeading1 — даёт закладку в PDF
    $sel.Font.Name = 'Arial'
    $sel.TypeText('Кириллица в Word: TrueType-подмножество')
    $sel.TypeParagraph()
    Step 'heading'
    $sel.Style = $doc.Styles.Item(-1)            # wdStyleNormal
    Add-Para $sel 'Абзац шрифтом Times New Roman. Оттиск открывает документы, показывает страницы и помогает работать с ними без лишних окон. Съешь же ещё этих мягких французских булок, да выпей же чаю.' 'Times New Roman' 12
    Add-Para $sel 'Абзац шрифтом Arial. Каждая страница рисуется заново при изменении масштаба, поэтому мелкий шрифт остаётся чётким. Ёжик в тумане — «кавычки», №5, 1234,56 ₽.' 'Arial' 11
    Add-Para $sel 'Полужирный и смешанный текст: Привет, world! Grüße aus Berlin.' 'Arial' 11 1
    $sel.Style = $doc.Styles.Item(-3)            # wdStyleHeading2
    $sel.Font.Name = 'Arial'
    $sel.TypeText('Таблица и картинка')
    $sel.TypeParagraph()
    $sel.Style = $doc.Styles.Item(-1)
    Step 'paragraphs'
    $tbl = $doc.Tables.Add($sel.Range, 3, 3)
    $tbl.Borders.Enable = 1
    $cells = @(@('Город', 'Население', 'Регион'), @('Казань', '1 300 000', 'Татарстан'), @('Сочи', '470 000', 'Краснодарский край'))
    for ($r = 0; $r -lt 3; $r++) {
        for ($c = 0; $c -lt 3; $c++) {
            $tbl.Cell($r + 1, $c + 1).Range.Text = $cells[$r][$c]
        }
    }
    Step 'table'
    $sel.EndKey(6) | Out-Null                    # wdStory — в конец документа
    $sel.TypeParagraph()
    $pic = $sel.InlineShapes.AddPicture([string]$photo)
    $pic.Width = 320
    $pic.Height = 213.33
    $sel.TypeParagraph()
    Add-Para $sel 'Подпись к картинке: процедурный «пейзаж» из tests/fixtures/images/photo.jpg.' 'Times New Roman' 10
    Step 'picture'
    $doc.SaveAs2([string]$docx, 16)
    # ExportAsFixedFormat(путь, 17 PDF, открыть=нет, 0 печать, 0 весь документ, 1, 1, 0 содержимое,
    #   свойства=да, IRM=да, 1 закладки по заголовкам, теги=да, растр недостающих шрифтов=да, PDF/A=нет)
    Step 'saved docx'
    $doc.ExportAsFixedFormat([string]$rawPdf, 17, $false, 0, 0, 1, 1, 0, $true, $true, 1, $true, $true, $false)
}
finally {
    # Close/Quit с аргументом требуют [ref] — закрываем без сохранения через Saved = true
    if ($doc) { $doc.Saved = $true; $doc.Close(); [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($doc) }
    if ($word) { $word.Quit(); [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($word) }
    [GC]::Collect()
    [GC]::WaitForPendingFinalizers()
}
$secs = ((Get-Date) - $t0).TotalSeconds

$norm = Join-Path $PSScriptRoot 'normalize_word.py'
$target = Join-Path $dst 'word-truetype.pdf'
& $Python -X utf8 -I $norm pdf $rawPdf $target --title 'Кириллица в Word: TrueType-подмножество' --subject 'tests/fixtures/cyr/word-truetype.pdf'
if ($LASTEXITCODE -ne 0) { throw "normalize_word.py pdf: код $LASTEXITCODE" }
$len = (Get-Item -LiteralPath $target).Length
Write-Output ("cyr: word-truetype.pdf {0} байт, Word {1:N1} с" -f $len, $secs)
exit 0
