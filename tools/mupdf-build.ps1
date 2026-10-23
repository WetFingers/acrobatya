<#
.SYNOPSIS
    Сборка MuPDF 1.28.5 из C:\src\mupdf\platform\win32\mupdf.sln для «Оттиска».

.DESCRIPTION
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\mupdf-build.ps1 -Config Debug|Release [-NoLtcg] [-Target mutool] [-Rebuild]

    Все проекты получают tools\ottisk-md.props (/MD и /U_DEBUG во всех конфигурациях) через
    /p:ForceImportBeforeCppTargets. -NoLtcg — Release без /GL (ускоряет линковку exe «Оттиска»).
    -Rebuild — сначала удаляет x64\<Config> (выходные .lib/.exe и промежуточные файлы), потом собирает.
    Цель по умолчанию — mutool: она тянет libmupdf, libthirdparty, libharfbuzz, libtesseract, libleptonica,
    libextract, libzxing, libmubarcode, libpkcs7, libmuthreads, libmutool и libresources (Debug|x64 у
    libresources отображён на Release|x64 — её .lib всегда в x64\Release).
    Не собирать mupdf-gl, sodochandler, *Extra. Лог — C:\Tools\logs\mupdf-<config>.txt; код выхода — MSBuild.
    Файл в UTF-8 с BOM: Windows PowerShell 5.1 иначе читает кириллицу неверно.
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')][string]$Config = 'Debug',
    [switch]$NoLtcg,
    [string]$Target = 'mutool',
    [switch]$Rebuild,
    [string]$MupdfRoot = $(if ($env:MUPDF_ROOT) { $env:MUPDF_ROOT } else { 'C:\src\mupdf' })
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding $false

$props = Join-Path $PSScriptRoot 'ottisk-md.props'
if (-not (Test-Path $props)) { throw "Нет $props" }
$props = (Resolve-Path $props).Path
$sln = Join-Path $MupdfRoot 'platform\win32\mupdf.sln'
if (-not (Test-Path $sln)) { throw "Нет $sln" }

$msbuild = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe"
if (-not (Test-Path $msbuild)) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
}
if (-not $msbuild -or -not (Test-Path $msbuild)) { throw 'MSBuild не найден' }

$logDir = 'C:\Tools\logs'
New-Item -ItemType Directory -Force $logDir | Out-Null
$suffix = if ($NoLtcg) { '-noltcg' } else { '' }
$log = Join-Path $logDir ("mupdf-{0}{1}.txt" -f $Config.ToLower(), $suffix)

if ($Rebuild) {
    $out = Join-Path $MupdfRoot "platform\win32\x64\$Config"
    if (Test-Path $out) {
        Write-Host "Удаляю $out (выход прежней сборки)"
        Remove-Item -LiteralPath $out -Recurse -Force
    }
}

$msArgs = @($sln, "/t:$Target", "/p:Configuration=$Config", '/p:Platform=x64', '/p:PlatformToolset=v143',
          "/p:ForceImportBeforeCppTargets=$props", '/m', '/v:m', '/nologo')
if ($NoLtcg) { $msArgs += '/p:OttiskNoLtcg=true' }

$t0 = Get-Date
Write-Host ("MSBuild {0} {1}{2} → {3}" -f $Target, $Config, $(if ($NoLtcg) { ' без LTCG' } else { '' }), $log)
& $msbuild @msArgs *> $log
$rc = $LASTEXITCODE
$sec = [int]((Get-Date) - $t0).TotalSeconds
$errors = @(Select-String -Path $log -Pattern ' error ' -SimpleMatch | Select-Object -First 20)
Write-Host ("Готово за {0} с, код {1}, ошибок в логе: {2}" -f $sec, $rc, $errors.Count)
if ($errors.Count) { $errors | ForEach-Object { Write-Host $_.Line } }
exit $rc
