<#
.SYNOPSIS
    Проверка детерминизма генераторов: два прогона make_all.ps1 дают одинаковые sha256.

.DESCRIPTION
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\gen\check_determinism.ps1 [-SkipWord] [-SkipBig]
        [-NoRepoCompare] [-Keep] [-Python <python.exe>]

    1. Дважды запускает tools\gen\make_all.ps1 во временные корни %TEMP%\ottisk-det\run1 и run2
       (каждый со своим каталогом корпуса run<N>\corpus-gen) — репозиторий и настоящий корпус не трогаются.
    2. Сравнивает sha256 всех файлов run1 и run2 (tests\… и corpus-gen\…), кроме шаблонов из
       tools\gen\nondeterministic.txt (помеченные «недетерминирован: Word»).
    3. Сравнивает run1 с файлами репозитория (tests\data\mini, tests\fixtures\{annots,forms,enc,sig,scans,
       office,cyr,demo,images}) — закоммиченные фикстуры должны воспроизводиться; -NoRepoCompare — без этого.
    -Keep — не удалять %TEMP%\ottisk-det после проверки.
    Печатает различия и итог. Код выхода 0 — всё совпало, 1 — есть различия или упал make_all.
    Файл в UTF-8 с BOM: Windows PowerShell 5.1 иначе читает кириллицу неверно.
#>
[CmdletBinding()]
param(
    [switch]$SkipWord,
    [switch]$SkipBig,
    [switch]$NoRepoCompare,
    [switch]$Keep,
    [string]$Python = ''
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding $false

$gen = $PSScriptRoot
$repo = (Resolve-Path (Join-Path $gen '..\..')).Path
$base = Join-Path (Get-Item -LiteralPath $env:TEMP).FullName 'ottisk-det'
if (Test-Path -LiteralPath $base) { Remove-Item -LiteralPath $base -Recurse -Force }   # каталог создаёт только этот скрипт
New-Item -ItemType Directory -Force -Path $base | Out-Null

$ignore = @()
$listFile = Join-Path $gen 'nondeterministic.txt'
if (Test-Path -LiteralPath $listFile) {
    $ignore = Get-Content -LiteralPath $listFile -Encoding UTF8 | Where-Object { $_.Trim() -and -not $_.TrimStart().StartsWith('#') } | ForEach-Object { $_.Trim() }
}
function Test-Ignored([string]$rel) {
    foreach ($pat in $ignore) { if ($rel -like $pat) { return $true } }
    return $false
}

# Текстовые файлы (.txt .json .pem .md) сравниваются без учёта CRLF/LF: на машине core.autocrlf=true,
# а в каталогах фикстур .gitattributes «* -text» — так сверка устойчива к любому из вариантов.
$textExt = @('.txt', '.json', '.pem', '.md')
$sha = [System.Security.Cryptography.SHA256]::Create()
function Get-NormHash([string]$path) {
    $bytes = [System.IO.File]::ReadAllBytes($path)
    if ($textExt -contains [System.IO.Path]::GetExtension($path).ToLowerInvariant()) {
        $text = [System.Text.Encoding]::UTF8.GetString($bytes).Replace("`r`n", "`n")
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($text)
    }
    return ([System.BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '')
}

function Get-Hashes([string]$root) {
    $map = @{}
    foreach ($sub in @('tests', 'corpus-gen')) {
        $dir = Join-Path $root $sub
        if (-not (Test-Path -LiteralPath $dir)) { continue }
        Get-ChildItem -LiteralPath $dir -Recurse -File | ForEach-Object {
            $rel = $_.FullName.Substring($root.Length + 1).Replace('\', '/')
            $map[$rel] = Get-NormHash $_.FullName
        }
    }
    return $map
}

$t0 = Get-Date
$runs = @()
foreach ($i in 1, 2) {
    $root = Join-Path $base "run$i"
    $corpus = Join-Path $root 'corpus-gen'
    $margs = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $gen 'make_all.ps1'),
        '-Out', $root, '-Corpus', $corpus, '-NoVerify')
    if ($SkipWord) { $margs += '-SkipWord' }
    if ($SkipBig) { $margs += '-SkipBig' }
    if ($Python) { $margs += @('-Python', $Python) }
    $s = Get-Date
    $ErrorActionPreference = 'Continue'
    $log = & powershell @margs 2>&1
    $code = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    $log | ForEach-Object { Write-Verbose ([string]$_) }
    Write-Output ('прогон {0}: make_all код {1}, {2:N1} с' -f $i, $code, ((Get-Date) - $s).TotalSeconds)
    if ($code -ne 0) {
        $log | Select-Object -Last 25 | ForEach-Object { Write-Output ('   ' + [string]$_) }
        exit 1
    }
    $runs += , (Get-Hashes $root)
}

$diff = New-Object System.Collections.Generic.List[string]
$skipped = 0
$a = $runs[0]
$b = $runs[1]
foreach ($k in ($a.Keys + $b.Keys | Sort-Object -Unique)) {
    if (Test-Ignored $k) { $skipped++; continue }
    if (-not $a.ContainsKey($k)) { $diff.Add("только во 2-м прогоне: $k"); continue }
    if (-not $b.ContainsKey($k)) { $diff.Add("только в 1-м прогоне: $k"); continue }
    if ($a[$k] -ne $b[$k]) { $diff.Add("разные sha256: $k") }
}
Write-Output ('сравнено файлов: {0}, пропущено по nondeterministic.txt: {1}, различий: {2}' -f $a.Count, $skipped, $diff.Count)

$repoDiff = New-Object System.Collections.Generic.List[string]
if (-not $NoRepoCompare) {
    foreach ($k in ($a.Keys | Sort-Object)) {
        if (-not $k.StartsWith('tests/')) { continue }
        if (Test-Ignored $k) { continue }
        $p = Join-Path $repo ($k.Replace('/', '\'))
        if (-not (Test-Path -LiteralPath $p)) { $repoDiff.Add("нет в репозитории: $k"); continue }
        if ((Get-NormHash $p) -ne $a[$k]) { $repoDiff.Add("репозиторий отличается: $k") }
    }
    Write-Output ('сверка с репозиторием: различий {0}' -f $repoDiff.Count)
}
$diff + $repoDiff | ForEach-Object { Write-Output ('  ' + $_) }
if (-not $Keep) { Remove-Item -LiteralPath $base -Recurse -Force }
Write-Output ('check_determinism: {0:N1} с' -f ((Get-Date) - $t0).TotalSeconds)
if ($diff.Count -gt 0 -or $repoDiff.Count -gt 0) { exit 1 }
exit 0
