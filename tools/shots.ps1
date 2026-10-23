<#
.SYNOPSIS
    Снимки сцен «АкроБати» без экрана: AcroBatya.exe --shots … под QT_QPA_PLATFORM=offscreen.

.DESCRIPTION
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\shots.ps1 -Only home[,имя…] | -All
        [-Preset msvc-debug] [-Out .queue\screenshots] [-Theme light|dark|contrast | -Dark] [-Large] [-Lang ru|en]
        [-Size 1440x900] [-TimeoutSeconds 600]

    -Only — названные сцены в одном варианте (тема -Theme/-Dark, «Крупные элементы» -Large, язык -Lang);
    -All — все сцены во всех вариантах: светлая, тёмная, высокая контрастность, крупные элементы, английский
    (файлы <сцена>[-dark|-contrast][-large][-en].png; сцены с закреплённой темой или масштабом — один раз).
    Список сцен — AcroBatya.exe --shots-list; 30 главных для рубежа — tools/main_scenes.txt.

    Запускает build\<каталог пресета>\bin\AcroBatya.exe --shots <имена|all> --out <каталог> с окружением:
      PATH += C:\Qt\6.12.0\msvc2022_64\bin (DLL Qt в build не копируются), QT_QPA_PLATFORM=offscreen,
      QT_QPA_FONTDIR=C:/Windows/Fonts, OTTISK_DATA_DIR=.queue\shots-data (данные человека не читаются),
      OTTISK_INSTANCE=test (своё имя QLocalServer — файлы не уходят в установленную копию).
    Печатает пути PNG, их размеры в пикселях и байтах. Код выхода — код AcroBatya.exe
    (нет exe — 1). Снимки — только из главной сессии очереди (агенты workflow их не запускают).
    Файл в UTF-8 с BOM: Windows PowerShell 5.1 иначе читает кириллицу неверно.
#>
[CmdletBinding()]
param(
    [string]$Only,
    [switch]$All,
    [string]$Preset = 'msvc-debug',
    [string]$Out = '.queue\screenshots',
    [ValidateSet('', 'light', 'dark', 'contrast')][string]$Theme = '',
    [switch]$Dark,
    [switch]$Large,
    [ValidateSet('', 'ru', 'en')][string]$Lang = '',
    [string]$Size = '',
    [int]$TimeoutSeconds = 600
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding $false

$root = Split-Path -Parent $PSScriptRoot
$qtRoot = if ($env:QT_ROOT) { $env:QT_ROOT } else { 'C:\Qt\6.12.0\msvc2022_64' }

if ([bool]$Only -eq [bool]$All) {
    Write-Host 'Укажите ровно одно: -Only <имя[,имя…]> или -All'
    exit 1
}
if ($Size -and $Size -notmatch '^\d+x\d+$') {
    Write-Host "-Size ""$Size"": ожидается ШxВ, например 1440x900"
    exit 1
}

# msvc-debug → build\debug, msvc-release → build\release, msvc-asan → build\asan …
$buildName = $Preset -replace '^msvc-', ''
$exe = Join-Path $root "build\$buildName\bin\AcroBatya.exe"
if (-not (Test-Path -LiteralPath $exe)) {
    Write-Host "Нет $exe — сначала соберите цель AcroBatya (пресет $Preset)"
    exit 1
}

$outDir = if ([IO.Path]::IsPathRooted($Out)) { $Out } else { Join-Path $root $Out }
$outDir = [IO.Path]::GetFullPath($outDir).TrimEnd('\')  # "C:\a b\" в кавычках съел бы закрывающую кавычку
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$dataDir = Join-Path $root '.queue\shots-data'
New-Item -ItemType Directory -Force -Path $dataDir | Out-Null

if ($Dark) { $Theme = 'dark' }
$names = if ($All) { 'all' } else { (($Only -split ',') | ForEach-Object { $_.Trim() } | Where-Object { $_ }) -join ',' }
$baseArgs = @('--shots', $names, '--out', $outDir)
if ($Size) { $baseArgs += @('--size', $Size) }
# Варианты запуска: -All — все темы, масштаб и язык; иначе один, из ключей.
$variants = @()
if ($All) {
    $variants += , @()
    $variants += , @('--theme', 'dark')
    $variants += , @('--theme', 'contrast')
    $variants += , @('--scale', '1.25')
    $variants += , @('--lang', 'en')
}
else {
    $one = @()
    if ($Theme) { $one += @('--theme', $Theme) }
    if ($Large) { $one += @('--scale', '1.25') }
    if ($Lang) { $one += @('--lang', $Lang) }
    $variants += , $one
}

function Get-PngSize([string]$path) {
    # ширина и высота — big-endian в IHDR (байты 16–23)
    $stream = [IO.File]::OpenRead($path)
    try {
        $header = New-Object byte[] 24
        if ($stream.Read($header, 0, 24) -lt 24) { return '?' }
    }
    finally {
        $stream.Dispose()
    }
    $width = ([int]$header[16] -shl 24) -bor ([int]$header[17] -shl 16) -bor ([int]$header[18] -shl 8) -bor [int]$header[19]
    $height = ([int]$header[20] -shl 24) -bor ([int]$header[21] -shl 16) -bor ([int]$header[22] -shl 8) -bor [int]$header[23]
    return "${width}x${height}"
}

$envNames = 'PATH', 'QT_QPA_PLATFORM', 'QT_QPA_FONTDIR', 'OTTISK_DATA_DIR', 'OTTISK_INSTANCE'
$saved = @{}
foreach ($name in $envNames) { $saved[$name] = [Environment]::GetEnvironmentVariable($name) }
$started = Get-Date
$watch = [Diagnostics.Stopwatch]::StartNew()
try {
    $env:PATH = "$qtRoot\bin;$env:PATH"
    $env:QT_QPA_PLATFORM = 'offscreen'
    $env:QT_QPA_FONTDIR = 'C:/Windows/Fonts'
    $env:OTTISK_DATA_DIR = $dataDir
    $env:OTTISK_INSTANCE = 'test'
    $code = 0
    foreach ($variant in $variants) {
        $exeArgs = $baseArgs + $variant
        Write-Host "AcroBatya.exe $($exeArgs -join ' ')"
        # AcroBatya.exe — GUI-приложение (WIN32): через & PowerShell его не ждёт и кода не даёт,
        # поэтому Start-Process с ожиданием и выводом в файлы
        $logBase = Join-Path $env:TEMP ("ottisk-shots-{0}" -f $PID)
        $argLine = ($exeArgs | ForEach-Object { if ("$_" -match '[\s"]') { '"' + ("$_" -replace '"', '\"') + '"' } else { "$_" } }) -join ' '
        $process = Start-Process -FilePath $exe -ArgumentList $argLine -NoNewWindow -PassThru `
            -RedirectStandardOutput "$logBase.out.txt" -RedirectStandardError "$logBase.err.txt" -WorkingDirectory $root
        $null = $process.Handle
        if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
            & taskkill.exe /T /F /PID $process.Id 2>&1 | Out-Null
            Write-Host "AcroBatya.exe не завершился за $TimeoutSeconds с — остановлен"
        }
        $process.WaitForExit()
        if ($process.ExitCode -ne 0) { $code = $process.ExitCode }
        foreach ($stream in 'out', 'err') {
            $file = "$logBase.$stream.txt"
            if (Test-Path -LiteralPath $file) {
                Get-Content -LiteralPath $file -Encoding UTF8 | ForEach-Object { Write-Host "  $_" }
                Remove-Item -LiteralPath $file -Force -ErrorAction SilentlyContinue
            }
        }
    }
}
finally {
    foreach ($name in $envNames) { [Environment]::SetEnvironmentVariable($name, $saved[$name]) }
}

$pngs = Get-ChildItem -LiteralPath $outDir -Filter '*.png' -File |
    Where-Object { $_.LastWriteTime -ge $started } |
    Sort-Object Name
foreach ($png in $pngs) {
    Write-Host ("{0}  {1}  {2:N0} КБ" -f $png.FullName, (Get-PngSize $png.FullName), ($png.Length / 1KB))
}
Write-Host ("Снимков: {0}, код AcroBatya.exe {1}, {2:N1} с" -f @($pngs).Count, $code, $watch.Elapsed.TotalSeconds)
if ($code -eq 0 -and @($pngs).Count -eq 0) {
    Write-Host 'Внимание: AcroBatya.exe вернул 0, но новых PNG нет'
}
exit $code
