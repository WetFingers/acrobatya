<#
.SYNOPSIS
    Все генераторы своих фикстур «Оттиска» по порядку, затем приёмка verify_fixtures.py.

.DESCRIPTION
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\gen\make_all.ps1 [-Out <корень>] [-Corpus <каталог>]
        [-Python <python.exe>] [-SkipWord] [-SkipBig] [-NoVerify]

    Порядок (каждый следующий может читать результат предыдущих):
      make_images.py → make_mini.py → make_annots.py → make_forms.py → make_enc.py → make_sig.py →
      make_cyr_fixtures.ps1 (Word) → make_office_fixtures.ps1 (Word) → make_office_fixtures.py →
      make_scans.py → make_demo.py → make_imgcmp.py → make_text.py → stext_expected.py → make_view.py →
      make_big.py (корпус) → make_mutants.py (корпус) → verify_fixtures.py.
    -Out     — корень, под которым создаются tests\data\mini и tests\fixtures\… (по умолчанию — репозиторий;
               check_determinism.ps1 передаёт сюда временные каталоги).
    -Corpus  — каталог больших файлов и мутантов (по умолчанию %OTTISK_CORPUS%\gen или
               C:\Tools\ottisk\corpus\gen; в Git не попадают).
    -SkipWord — без шагов Word (если Word не зарегистрирован в COM, пропускаются сами, с предупреждением).
    -SkipBig  — без big-1200/images-200 и мутантов.
    -WordTimeout — предел одного шага Word в секундах (по умолчанию 300): скрытый Word может повиснуть
               на невидимом диалоге; шаг тогда падает, а не ждёт вечно (WINWORD.EXE остаётся — закрыть вручную).
    Python — $env:OTTISK_PYTHON или C:\Tools\venv-test\Scripts\python.exe, всегда с -X utf8 -I.
    Печатает время каждого шага. Код выхода 0 — все шаги и приёмка прошли, 1 — нет.
    Файл в UTF-8 с BOM: Windows PowerShell 5.1 иначе читает кириллицу неверно.
#>
[CmdletBinding()]
param(
    [string]$Out = '',
    [string]$Corpus = '',
    [string]$Python = '',
    [switch]$SkipWord,
    [switch]$SkipBig,
    [switch]$NoVerify,
    [int]$WordTimeout = 300
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding $false

$gen = $PSScriptRoot
$repo = (Resolve-Path (Join-Path $gen '..\..')).Path
if (-not $Out) { $Out = $repo }
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$Out = (Get-Item -LiteralPath $Out).FullName
if (-not $Corpus) {
    if ($env:OTTISK_CORPUS) { $Corpus = Join-Path $env:OTTISK_CORPUS 'gen' }
    else { $Corpus = 'C:\Tools\ottisk\corpus\gen' }
}
New-Item -ItemType Directory -Force -Path $Corpus | Out-Null
$Corpus = (Get-Item -LiteralPath $Corpus).FullName
if (-not $Python) { $Python = $env:OTTISK_PYTHON }
if (-not $Python) { $Python = 'C:\Tools\venv-test\Scripts\python.exe' }
$tmp = Join-Path (Get-Item -LiteralPath $env:TEMP).FullName 'ottisk-gen'
New-Item -ItemType Directory -Force -Path $tmp | Out-Null

if (-not $SkipWord -and -not [type]::GetTypeFromProgID('Word.Application')) {
    Write-Warning 'Word не зарегистрирован в COM — шаги Word пропущены (cyr/word-truetype.pdf, office/проба.docx, заметка.rtf)'
    $SkipWord = $true
}

$failed = New-Object System.Collections.Generic.List[string]
$total = Get-Date

function Invoke-Step([string]$name, [scriptblock]$body) {
    $t0 = Get-Date
    try {
        & $body
        Write-Output ('[ok]   {0,-28} {1,6:N1} с' -f $name, ((Get-Date) - $t0).TotalSeconds)
    }
    catch {
        Write-Output ('[FAIL] {0,-28} {1,6:N1} с  {2}' -f $name, ((Get-Date) - $t0).TotalSeconds, $_.Exception.Message)
        $failed.Add($name)
    }
}

function Invoke-Gen([string]$script, [string[]]$extra = @()) {
    $path = Join-Path $gen $script
    $ErrorActionPreference = 'Continue'      # stderr программы под 2>&1 в PS 5.1 иначе бросает исключение
    $out = & $Python -X utf8 -I $path --out $Out --tmp $tmp @extra 2>&1
    $code = $LASTEXITCODE
    $out | ForEach-Object { Write-Verbose ([string]$_) }
    if ($code -ne 0) {
        $tail = ($out | Select-Object -Last 15) -join "`n"
        throw "${script}: код $code`n$tail"
    }
}

function Invoke-Ps([string]$script) {
    # Отдельный процесс с пределом времени: COM-вызов Word нельзя прервать изнутри скрипта.
    $path = Join-Path $gen $script
    $stdout = Join-Path $tmp ($script + '.out.log')
    $stderr = Join-Path $tmp ($script + '.err.log')
    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', ('"' + $path + '"'),
        '-Out', ('"' + $Out + '"'), '-Python', ('"' + $Python + '"'))
    $p = Start-Process -FilePath 'powershell.exe' -ArgumentList $argList -NoNewWindow -PassThru `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $null = $p.Handle                         # PS 5.1: без этого ExitCode после выхода пуст
    if (-not $p.WaitForExit($WordTimeout * 1000)) {
        try { $p.Kill() } catch { }
        throw "${script}: не закончился за $WordTimeout с — Word, вероятно, ждёт невидимый диалог; скрытый WINWORD.EXE закрыть в диспетчере задач"
    }
    $p.WaitForExit()
    $code = $p.ExitCode
    $out = @(Get-Content -LiteralPath $stdout -Encoding UTF8) + @(Get-Content -LiteralPath $stderr -Encoding UTF8)
    $out | ForEach-Object { Write-Verbose ([string]$_) }
    if ($code -ne 0) {
        $tail = ($out | Select-Object -Last 15) -join "`n"
        throw "${script}: код $code`n$tail"
    }
}

Write-Output "make_all: корень $Out, корпус $Corpus"
Invoke-Step 'make_images.py' { Invoke-Gen 'make_images.py' }
Invoke-Step 'make_mini.py' { Invoke-Gen 'make_mini.py' }
Invoke-Step 'make_annots.py' { Invoke-Gen 'make_annots.py' }
Invoke-Step 'make_forms.py' { Invoke-Gen 'make_forms.py' }
Invoke-Step 'make_enc.py' { Invoke-Gen 'make_enc.py' }
Invoke-Step 'make_sig.py' { Invoke-Gen 'make_sig.py' }
if (-not $SkipWord) {
    Invoke-Step 'make_cyr_fixtures.ps1' { Invoke-Ps 'make_cyr_fixtures.ps1' }
    Invoke-Step 'make_office_fixtures.ps1' { Invoke-Ps 'make_office_fixtures.ps1' }
}
Invoke-Step 'make_office_fixtures.py' { Invoke-Gen 'make_office_fixtures.py' }
Invoke-Step 'make_scans.py' { Invoke-Gen 'make_scans.py' }
Invoke-Step 'make_demo.py' { Invoke-Gen 'make_demo.py' }
Invoke-Step 'make_imgcmp.py' { Invoke-Gen 'make_imgcmp.py' }
Invoke-Step 'make_text.py' { Invoke-Gen 'make_text.py' }
Invoke-Step 'stext_expected.py' { Invoke-Gen 'stext_expected.py' }
Invoke-Step 'make_view.py' { Invoke-Gen 'make_view.py' }
if (-not $SkipBig) {
    Invoke-Step 'make_big.py' { Invoke-Gen 'make_big.py' @('--corpus', $Corpus) }
    Invoke-Step 'make_mutants.py' { Invoke-Gen 'make_mutants.py' @('--corpus', $Corpus) }
}
if (-not $NoVerify) {
    Invoke-Step 'verify_fixtures.py' {
        $vargs = @('--out', $Out, '--corpus', $Corpus)
        if ($SkipWord) { $vargs += '--no-word' }
        $ErrorActionPreference = 'Continue'
        $out = & $Python -X utf8 -I (Join-Path $gen 'verify_fixtures.py') @vargs 2>&1
        $code = $LASTEXITCODE
        $out | ForEach-Object { Write-Output ('       ' + [string]$_) }
        if ($code -ne 0) { throw "verify_fixtures.py: код $code" }
    }
}
$secs = ((Get-Date) - $total).TotalSeconds
if ($failed.Count -gt 0) {
    Write-Output ('make_all: упало {0} шаг(ов): {1}; всего {2:N1} с' -f $failed.Count, ($failed -join ', '), $secs)
    exit 1
}
Write-Output ('make_all: всё готово за {0:N1} с' -f $secs)
exit 0
