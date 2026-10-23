<#
.SYNOPSIS
    Снимки сцен «Оттиска» без экрана: Ottisk.exe --shots … под QT_QPA_PLATFORM=offscreen.

.DESCRIPTION
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\shots.ps1 -Only home[,имя…] | -All
        [-Preset msvc-debug] [-Out .queue\screenshots] [-Theme light|dark] [-Size 1440x900] [-TimeoutSeconds 600]

    Запускает build\<каталог пресета>\bin\Ottisk.exe --shots <имена|all> --out <каталог> с окружением:
      PATH += C:\Qt\6.12.0\msvc2022_64\bin (DLL Qt в build не копируются), QT_QPA_PLATFORM=offscreen,
      QT_QPA_FONTDIR=C:/Windows/Fonts, OTTISK_DATA_DIR=.queue\shots-data (данные человека не читаются),
      OTTISK_INSTANCE=test (своё имя QLocalServer — файлы не уходят в установленную копию).
    Печатает пути PNG, их размеры в пикселях и байтах. Код выхода — код Ottisk.exe
    (нет exe — 1). Снимки — только из главной сессии очереди (агенты workflow их не запускают).
    Файл в UTF-8 с BOM: Windows PowerShell 5.1 иначе читает кириллицу неверно.
#>
[CmdletBinding()]
param(
    [string]$Only,
    [switch]$All,
    [string]$Preset = 'msvc-debug',
    [string]$Out = '.queue\screenshots',
    [ValidateSet('', 'light', 'dark')][string]$Theme = '',
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
$exe = Join-Path $root "build\$buildName\bin\Ottisk.exe"
if (-not (Test-Path -LiteralPath $exe)) {
    Write-Host "Нет $exe — сначала соберите цель Ottisk (пресет $Preset)"
    exit 1
}

$outDir = if ([IO.Path]::IsPathRooted($Out)) { $Out } else { Join-Path $root $Out }
$outDir = [IO.Path]::GetFullPath($outDir).TrimEnd('\')  # "C:\a b\" в кавычках съел бы закрывающую кавычку
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$dataDir = Join-Path $root '.queue\shots-data'
New-Item -ItemType Directory -Force -Path $dataDir | Out-Null

$names = if ($All) { 'all' } else { (($Only -split ',') | ForEach-Object { $_.Trim() } | Where-Object { $_ }) -join ',' }
$exeArgs = @('--shots', $names, '--out', $outDir)
if ($Theme) { $exeArgs += @('--theme', $Theme) }
if ($Size) { $exeArgs += @('--size', $Size) }

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
    Write-Host "Ottisk.exe $($exeArgs -join ' ')"
    # Ottisk.exe — GUI-приложение (WIN32): через & PowerShell его не ждёт и кода не даёт,
    # поэтому Start-Process с ожиданием и выводом в файлы
    $logBase = Join-Path $env:TEMP ("ottisk-shots-{0}" -f $PID)
    $argLine = ($exeArgs | ForEach-Object { if ("$_" -match '[\s"]') { '"' + ("$_" -replace '"', '\"') + '"' } else { "$_" } }) -join ' '
    $process = Start-Process -FilePath $exe -ArgumentList $argLine -NoNewWindow -PassThru `
        -RedirectStandardOutput "$logBase.out.txt" -RedirectStandardError "$logBase.err.txt" -WorkingDirectory $root
    $null = $process.Handle
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        & taskkill.exe /T /F /PID $process.Id 2>&1 | Out-Null
        Write-Host "Ottisk.exe не завершился за $TimeoutSeconds с — остановлен"
    }
    $process.WaitForExit()
    $code = $process.ExitCode
    foreach ($stream in 'out', 'err') {
        $file = "$logBase.$stream.txt"
        if (Test-Path -LiteralPath $file) {
            Get-Content -LiteralPath $file -Encoding UTF8 | ForEach-Object { Write-Host "  $_" }
            Remove-Item -LiteralPath $file -Force -ErrorAction SilentlyContinue
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
Write-Host ("Снимков: {0}, код Ottisk.exe {1}, {2:N1} с" -f @($pngs).Count, $code, $watch.Elapsed.TotalSeconds)
if ($code -eq 0 -and @($pngs).Count -eq 0) {
    Write-Host 'Внимание: Ottisk.exe вернул 0, но новых PNG нет'
}
exit $code
