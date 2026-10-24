<#
.SYNOPSIS
    Фикстуры редактора текста (сессия 3.1): tests/fixtures/edit/ — Word, LibreOffice (при наличии) и свои PDF.

.DESCRIPTION
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\gen\make_edit_fixtures.ps1 [-Out <корень>] [-Python <python.exe>] [-SkipWord]

    Что:
      1) Word через COM (Word.Application, невидимый, без диалогов) по заданию из make_edit_fixtures.py
         (--word-spec) собирает и экспортирует в PDF (ExportAsFixedFormat 17, теги, закладки):
         word-paragraphs.pdf (латиница, кириллица, смешанный текст; влево, по центру, вправо, по ширине; отступ
         первой строки), word-lists.pdf (маркированный и нумерованный списки), word-columns.pdf (раздел в две
         колонки, раздел в три колонки), word-table.pdf (таблица 3 × 3), word-rotated.pdf (надпись с текстом,
         повёрнутым на 90°). Затем tools/gen/normalize_word.py делает файлы детерминированными.
      2) LibreOffice при наличии (soffice.com): word-paragraphs.docx → lo-paragraphs.pdf (тот же текст, другой
         производитель). Нет LibreOffice — шаг пропускается с сообщением (до сессии 7.3 его ставит человек).
      3) make_edit_fixtures.py: свои PDF (MuPDF writer — mutool create для Type1 base14 без встраивания; pikepdf —
         TJ-кернинг, Tz/Tc/Tw, повёрнутый текст, два потока /Contents, XObject-форма, колонки, выравнивание,
         inline image) и expected.json — ожидания всех фикстур (для Word — по заданию и рамкам pypdfium2).
    Откуда: Microsoft Word (COM), LibreOffice (если есть), Python — venv C:\Tools\venv-test ($env:OTTISK_PYTHON).
    Детерминизм: Word — после normalize_word.py (как cyr/word-truetype.pdf); свои — _genlib.save_pdf.
    Word COM — только отсюда: в venv нет pywin32. Word закрывается в finally.
    Код выхода 0 — файлы созданы. Файл в UTF-8 с BOM: Windows PowerShell 5.1 иначе читает кириллицу неверно.
#>
[CmdletBinding()]
param(
    [string]$Out = '',
    [string]$Python = '',
    [switch]$SkipWord
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding $false

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $Out) { $Out = $repo }
if (-not $Python) { $Python = $env:OTTISK_PYTHON }
if (-not $Python) { $Python = 'C:\Tools\venv-test\Scripts\python.exe' }
$gen = Join-Path $PSScriptRoot 'make_edit_fixtures.py'
$norm = Join-Path $PSScriptRoot 'normalize_word.py'

$dst = Join-Path $Out 'tests\fixtures\edit'
New-Item -ItemType Directory -Force -Path $dst | Out-Null
# Word.SaveAs2 зависает на коротких путях 8.3 (C:\Users\WETFIN~1\…) — берём полное имя каталога
$work = Join-Path (Get-Item -LiteralPath $env:TEMP).FullName 'ottisk-gen\word-edit'
New-Item -ItemType Directory -Force -Path $work | Out-Null
$specPath = Join-Path $work 'word-spec.json'

& $Python -X utf8 -I $gen --out $Out --word-spec $specPath
if ($LASTEXITCODE -ne 0) { throw "make_edit_fixtures.py --word-spec: код $LASTEXITCODE" }
$spec = Get-Content -Raw -Encoding UTF8 -LiteralPath $specPath | ConvertFrom-Json

$t0 = Get-Date
function Step([string]$m) { [Console]::Error.WriteLine(('  {0,6:N1} s  {1}' -f ((Get-Date) - $t0).TotalSeconds, $m)) }

$alignCode = @{ 'left' = 0; 'center' = 1; 'right' = 2; 'justify' = 3 }

function Set-Font([object]$sel, [string]$font, [double]$size, [int]$bold) {
    $sel.Font.Name = $font
    $sel.Font.Size = $size
    $sel.Font.Bold = $bold
}

function Add-Paragraphs([object]$sel, [object[]]$paras) {
    $listKind = ''
    foreach ($p in $paras) {
        $kind = ''
        if ($p.PSObject.Properties['list']) { $kind = [string]$p.list }
        if ($kind -ne $listKind) {
            if ($listKind) { $sel.Range.ListFormat.RemoveNumbers() }
            if ($kind -eq 'bullet') { $sel.Range.ListFormat.ApplyBulletDefault() }
            if ($kind -eq 'number') { $sel.Range.ListFormat.ApplyNumberDefault() }
            $listKind = $kind
        }
        $bold = 0
        if ($p.PSObject.Properties['bold']) { $bold = [int]$p.bold }
        Set-Font $sel ([string]$p.font) ([double]$p.size) $bold
        $align = 'left'
        if ($p.PSObject.Properties['align']) { $align = [string]$p.align }
        if (-not $kind) {
            $sel.ParagraphFormat.Alignment = $alignCode[$align]
            $indent = 0
            if ($p.PSObject.Properties['indent']) { $indent = [double]$p.indent }
            $sel.ParagraphFormat.FirstLineIndent = [single]$indent
        }
        $sel.TypeText([string]$p.text)
        $sel.TypeParagraph()
    }
    if ($listKind) { $sel.Range.ListFormat.RemoveNumbers() }
    $sel.ParagraphFormat.Alignment = 0
    $sel.ParagraphFormat.FirstLineIndent = [single]0
}

function Export-Word([object]$word, [string]$name, [scriptblock]$build) {
    $base = [IO.Path]::GetFileNameWithoutExtension($name)
    $docx = Join-Path $work ($base + '.docx')
    $raw = Join-Path $work ($base + '.raw.pdf')
    foreach ($f in @($docx, $raw)) { if (Test-Path $f) { Remove-Item -LiteralPath $f -Force } }
    $doc = $word.Documents.Add()
    try {
        $doc.PageSetup.TopMargin = 56
        $doc.PageSetup.BottomMargin = 56
        $doc.PageSetup.LeftMargin = 64
        $doc.PageSetup.RightMargin = 64
        $sel = $word.Selection
        $sel.Style = $doc.Styles.Item(-1)            # wdStyleNormal
        $sel.ParagraphFormat.SpaceAfter = 8
        $sel.ParagraphFormat.SpaceBefore = 0
        & $build $doc $sel
        $doc.SaveAs2([string]$docx, 16)
        # ExportAsFixedFormat(путь, 17 PDF, открыть=нет, 0 печать, 0 весь документ, 1, 1, 0 содержимое,
        #   свойства=да, IRM=да, 1 закладки по заголовкам, теги=да, растр недостающих шрифтов=да, PDF/A=нет)
        $doc.ExportAsFixedFormat([string]$raw, 17, $false, 0, 0, 1, 1, 0, $true, $true, 1, $true, $true, $false)
    }
    finally {
        $doc.Saved = $true
        $doc.Close()
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($doc)
    }
    $target = Join-Path $dst $name
    & $Python -X utf8 -I $norm pdf $raw $target --title ([string]$spec.$name.about) --subject ('tests/fixtures/edit/' + $name)
    if ($LASTEXITCODE -ne 0) { throw "normalize_word.py pdf ${name}: код $LASTEXITCODE" }
    Step $name
    return $docx
}

if (-not $SkipWord -and -not [type]::GetTypeFromProgID('Word.Application')) {
    Write-Warning 'Word не зарегистрирован в COM — фикстуры word-*.pdf пропущены'
    $SkipWord = $true
}

$paragraphsDocx = ''
if (-not $SkipWord) {
    $word = $null
    try {
        $word = New-Object -ComObject Word.Application
        $word.Visible = $false
        $word.DisplayAlerts = 0
        Step 'Word started'

        $paragraphsDocx = Export-Word $word 'word-paragraphs.pdf' {
            param($doc, $sel)
            Add-Paragraphs $sel @($spec.'word-paragraphs.pdf'.paragraphs)
        }
        Export-Word $word 'word-lists.pdf' {
            param($doc, $sel)
            Add-Paragraphs $sel @($spec.'word-lists.pdf'.paragraphs)
        } | Out-Null
        Export-Word $word 'word-columns.pdf' {
            param($doc, $sel)
            $sections = @($spec.'word-columns.pdf'.sections)
            for ($i = 0; $i -lt $sections.Count; $i++) {
                if ($i -gt 0) { $sel.InsertBreak(3) }      # wdSectionBreakContinuous
                $doc.Sections.Item($i + 1).PageSetup.TextColumns.SetCount([int]$sections[$i].columns)
                $doc.Sections.Item($i + 1).PageSetup.TextColumns.Spacing = [single]18
                foreach ($text in @($sections[$i].paragraphs)) {
                    Set-Font $sel 'Arial' 10 0
                    $sel.ParagraphFormat.Alignment = 0
                    $sel.TypeText([string]$text)
                    $sel.TypeParagraph()
                }
            }
            # раздел, закрытый разрывом «на текущей странице», Word выравнивает по колонкам — иначе весь текст
            # уходит в первую колонку
            $sel.InsertBreak(3)
            $doc.Sections.Item($sections.Count + 1).PageSetup.TextColumns.SetCount(1)
        } | Out-Null
        Export-Word $word 'word-table.pdf' {
            param($doc, $sel)
            $rows = @($spec.'word-table.pdf'.table)
            Set-Font $sel 'Arial' 11 0
            $tbl = $doc.Tables.Add($sel.Range, $rows.Count, 3)
            $tbl.Borders.Enable = 1
            for ($r = 0; $r -lt $rows.Count; $r++) {
                $cells = @($rows[$r])
                for ($c = 0; $c -lt 3; $c++) { $tbl.Cell($r + 1, $c + 1).Range.Text = [string]$cells[$c] }
            }
        } | Out-Null
        Export-Word $word 'word-rotated.pdf' {
            param($doc, $sel)
            Add-Paragraphs $sel @($spec.'word-rotated.pdf'.paragraphs)
            # надпись 60 × 260 pt слева, текст снизу вверх (msoTextOrientationUpward = 2)
            $box = $doc.Shapes.AddTextbox(1, 80, 160, 60, 260)
            $box.TextFrame.Orientation = 2
            $box.TextFrame.TextRange.Text = [string]$spec.'word-rotated.pdf'.rotatedText
            $box.TextFrame.TextRange.Font.Name = 'Arial'
            $box.TextFrame.TextRange.Font.Size = 14
        } | Out-Null
    }
    finally {
        # Close/Quit с аргументом требуют [ref] — документы закрыты в Export-Word
        if ($word) { $word.Quit(); [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($word) }
        [GC]::Collect()
        [GC]::WaitForPendingFinalizers()
    }
}

# LibreOffice — при наличии (до 7.3 его ставит человек)
$soffice = @(
    (Join-Path $env:ProgramFiles 'LibreOffice\program\soffice.com'),
    (Join-Path ${env:ProgramFiles(x86)} 'LibreOffice\program\soffice.com')
) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
if ($soffice -and $paragraphsDocx -and (Test-Path $paragraphsDocx)) {
    $loOut = Join-Path $work 'lo'
    New-Item -ItemType Directory -Force -Path $loOut | Out-Null
    & $soffice --headless --norestore --convert-to pdf --outdir $loOut $paragraphsDocx | Out-Null
    $loRaw = Join-Path $loOut 'word-paragraphs.pdf'
    if (Test-Path $loRaw) {
        & $Python -X utf8 -I $norm pdf $loRaw (Join-Path $dst 'lo-paragraphs.pdf') --title 'LibreOffice: абзацы' --subject 'tests/fixtures/edit/lo-paragraphs.pdf'
        if ($LASTEXITCODE -ne 0) { throw "normalize_word.py pdf lo-paragraphs.pdf: код $LASTEXITCODE" }
        Step 'lo-paragraphs.pdf'
    }
}
else {
    Write-Output 'edit: LibreOffice (soffice.com) не найден — lo-paragraphs.pdf пропущен'
}

& $Python -X utf8 -I $gen --out $Out
if ($LASTEXITCODE -ne 0) { throw "make_edit_fixtures.py: код $LASTEXITCODE" }
Write-Output ("edit: готово за {0:N1} с" -f ((Get-Date) - $t0).TotalSeconds)
exit 0
