<#
.SYNOPSIS
    Фаззинг pdf_open_fuzzer (эталон 9.5, план 1.1): 60 с в сессии, 600 — на рубеже.

.DESCRIPTION
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\fuzz.ps1 [-Seconds 60] [-NoBuild]
        [-RssLimitMb 4096] [-Timeout 20]

    1. Сборка pdf_open_fuzzer в build\fuzz под замком (tools\build_lock.ps1 -Preset msvc-fuzz). Пресет
       сконфигурирован главной сессией (cmake --preset msvc-fuzz); нет CMakeCache.txt — код 2.
    2. Семена: tests\fixtures\pick\safedocs, tests\fixtures\pick\pdf-differences, свои мутанты
       (build\fuzz\gen\mutants — tools\gen\make_mutants.py, seed 1; делаются при первом запуске) и
       tests\fixtures\regress (прошлые падения).
    3. Рабочий корпус — build\fuzz\corpus (сюда libFuzzer пишет новые входы), падения —
       build\fuzz\artifacts\crash-*, timeout-*, oom-*; лог — build\fuzz\fuzz.log.
    4. Запуск через tools\vcenv.cmd (PATH к clang_rt.asan_dynamic-x86_64.dll):
       pdf_open_fuzzer.exe <корпус> <семена…> -max_total_time=<с> -rss_limit_mb=… -timeout=…
    Код: 0 — падений нет; 1 — есть артефакты (переложить в tests\fixtures\regress\ с понятным именем —
    их открывает unit.regress.load); 2 — нет конфигурации, сборка упала или нечего запускать.
    Файл в UTF-8 с BOM: Windows PowerShell 5.1 иначе читает кириллицу неверно.
#>
[CmdletBinding()]
param(
    [int]$Seconds = 60,
    [switch]$NoBuild,
    [int]$RssLimitMb = 4096,
    [int]$Timeout = 20
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding $false

$root = Split-Path -Parent $PSScriptRoot
$build = Join-Path $root 'build\fuzz'
if (-not (Test-Path -LiteralPath (Join-Path $build 'CMakeCache.txt'))) {
    Write-Host "Нет конфигурации $build — её делает главная сессия: cmake --preset msvc-fuzz"
    exit 2
}
if (-not $NoBuild) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'build_lock.ps1') -Preset msvc-fuzz -Target pdf_open_fuzzer
    if ($LASTEXITCODE -ne 0) { exit 2 }
}
$exe = Join-Path $build 'bin\pdf_open_fuzzer.exe'
if (-not (Test-Path -LiteralPath $exe)) {
    Write-Host "Нет $exe"
    exit 2
}

$python = if ($env:OTTISK_PYTHON) { $env:OTTISK_PYTHON } else { 'C:\Tools\venv-test\Scripts\python.exe' }
$mutants = Join-Path $build 'gen\mutants'
if (-not (Test-Path -LiteralPath (Join-Path $mutants 'index.json'))) {
    & $python -X utf8 -I (Join-Path $root 'tools\gen\make_mutants.py') --corpus (Join-Path $build 'gen') --tmp (Join-Path $build 'gen-tmp') | Out-Null
    if ($LASTEXITCODE -ne 0) { Write-Host 'make_mutants.py упал'; exit 2 }
}

$corpus = Join-Path $build 'corpus'
$artifacts = Join-Path $build 'artifacts'
New-Item -ItemType Directory -Force -Path $corpus, $artifacts | Out-Null
$seeds = @(
    (Join-Path $root 'tests\fixtures\pick\safedocs'),
    (Join-Path $root 'tests\fixtures\pick\pdf-differences'),
    $mutants,
    (Join-Path $root 'tests\fixtures\regress')
) | Where-Object { Test-Path -LiteralPath $_ }

$log = Join-Path $build 'fuzz.log'
$before = @(Get-ChildItem -LiteralPath $artifacts -File).Count
$args = @("`"$corpus`"") + ($seeds | ForEach-Object { "`"$_`"" }) + @(
    "-max_total_time=$Seconds", "-rss_limit_mb=$RssLimitMb", "-timeout=$Timeout",
    # Обратная косая перед закрывающей кавычкой удвоена: «\"» в разборе командной строки MSVCRT — кавычка внутри
    # аргумента, и путь съедал следующий ключ (рубеж Р2: артефакт timeout-* уходил мимо каталога).
    "-artifact_prefix=`"$artifacts\\`"", '-print_final_stats=1')
$cmdLine = "`"$(Join-Path $root 'tools\vcenv.cmd')`" && `"$exe`" " + ($args -join ' ') + " > `"$log`" 2>&1"
$t0 = Get-Date
& cmd /c $cmdLine
$code = $LASTEXITCODE
$elapsed = [int]((Get-Date) - $t0).TotalSeconds
$found = @(Get-ChildItem -LiteralPath $artifacts -File | Where-Object { $_.Name -match '^(crash|timeout|oom|leak)-' })
$stats = Select-String -LiteralPath $log -Pattern 'stat::number_of_executed_units|stat::new_units_added|stat::peak_rss_mb|cov: \d+' |
    Select-Object -Last 4 | ForEach-Object { $_.Line.Trim() }
Write-Host "fuzz: $elapsed с, код libFuzzer $code, артефактов $($found.Count) (было $before), лог $log"
$stats | ForEach-Object { Write-Host "  $_" }
if ($found.Count -gt 0) {
    $found | ForEach-Object { Write-Host "  падение: $($_.FullName)" }
    exit 1
}
exit 0
