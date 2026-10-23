<#
.SYNOPSIS
    Запасной путь установки Qt для «АкроБати» через aqtinstall (без прав администратора).

.DESCRIPTION
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\install-qt.ps1 [-Fallback] [-WhatIf] [-Force]
        [-OutputDir C:\Qt] [-AqtVenv C:\Tools\aqt]

    Основной путь (Qt 6.12.0 LTS):
      1. py -3.14 -m venv C:\Tools\aqt                          (если venv ещё нет)
      2. C:\Tools\aqt\Scripts\python.exe -m pip install "git+https://github.com/miurahr/aqtinstall.git@<sha>"
         (пропускается, если pip freeze уже показывает этот коммит)
      3. C:\Tools\aqt\Scripts\python.exe -m aqt install-qt windows desktop 6.12.0 win64_msvc2022_64
             -m qtimageformats --outputdir C:\Qt
    -Fallback — aqtinstall==3.3.0 с PyPI и Qt 6.10.3 (если 6.12.0 недоступна в репозитории Qt).
    Вызов только через python.exe -m aqt / -m pip: лаунчеры Scripts\aqt.exe и pip.exe перенесённого
    venv хранят старый путь к python.exe и не запускаются. QtPdf/QtWebEngine не ставятся.
    Каталог <OutputDir>\<версия>\msvc2022_64 уже есть — установка отказывается без -Force.
    -WhatIf — только напечатать команды. После установки — проверка qoffscreen.dll и qtdiag.exe.
    SHA закреплён по `C:\Tools\aqt\Scripts\python.exe -m pip show aqtinstall` / `-m aqt version`
    перенесённого venv (aqtinstall v3.3.1.dev166).
    Файл в UTF-8 с BOM: Windows PowerShell 5.1 иначе читает кириллицу неверно.
#>
[CmdletBinding()]
param(
    [switch]$Fallback,
    [switch]$WhatIf,
    [switch]$Force,
    [string]$OutputDir = 'C:\Qt',
    [string]$AqtVenv = 'C:\Tools\aqt'
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding $false

$aqtCommit = '076e1659807d0b362a3ed684d54c2e9c775eb9c7'
$aqtGit = "git+https://github.com/miurahr/aqtinstall.git@$aqtCommit"
if ($Fallback) {
    $qtVersion = '6.10.3'
    $aqtSpec = 'aqtinstall==3.3.0'
}
else {
    $qtVersion = '6.12.0'
    $aqtSpec = $aqtGit
}
$arch = 'win64_msvc2022_64'
$python = Join-Path $AqtVenv 'Scripts\python.exe'
$qtDir = Join-Path $OutputDir "$qtVersion\msvc2022_64"

$plan = @(
    @{ Title = "venv aqtinstall ($AqtVenv)"; Exe = 'py'; Args = @('-3.14', '-m', 'venv', $AqtVenv); Skip = (Test-Path -LiteralPath $python) },
    @{ Title = "aqtinstall ($aqtSpec)"; Exe = $python; Args = @('-m', 'pip', 'install', '--disable-pip-version-check', $aqtSpec); Skip = $false },
    @{ Title = "Qt $qtVersion $arch + qtimageformats"; Exe = $python; Args = @('-m', 'aqt', 'install-qt', 'windows', 'desktop', $qtVersion, $arch, '-m', 'qtimageformats', '--outputdir', $OutputDir); Skip = $false }
)

function Format-Command($step) {
    $parts = @($step.Exe) + $step.Args | ForEach-Object { if ("$_" -match '\s') { '"' + $_ + '"' } else { "$_" } }
    return $parts -join ' '
}

Write-Host "Qt $qtVersion ($arch) → $qtDir"
for ($i = 0; $i -lt $plan.Count; $i++) {
    $note = if ($plan[$i].Skip) { '  (есть — пропуск)' } else { '' }
    Write-Host ("  [{0}/{1}] {2}: {3}{4}" -f ($i + 1), $plan.Count, $plan[$i].Title, (Format-Command $plan[$i]), $note)
}
Write-Host ("  проверка: Test-Path {0}\plugins\platforms\qoffscreen.dll; {0}\bin\qtdiag.exe" -f $qtDir)
if ($WhatIf) {
    Write-Host '-WhatIf: ничего не запускалось.'
    exit 0
}

if ((Test-Path -LiteralPath $qtDir) -and -not $Force) {
    Write-Host "Qt уже установлен: $qtDir. Переустановка — с -Force (поверх; aqt перезапишет файлы)."
    exit 0
}

$watch = [Diagnostics.Stopwatch]::StartNew()
for ($i = 0; $i -lt $plan.Count; $i++) {
    $step = $plan[$i]
    if ($step.Skip) { continue }
    if ($i -eq 1 -and -not $Fallback) {
        # тот же коммит уже стоит — pip не трогаем
        $ErrorActionPreference = 'Continue'
        $frozen = & $python -m pip freeze --disable-pip-version-check 2>$null
        $ErrorActionPreference = 'Stop'
        if ($frozen | Where-Object { $_ -match "aqtinstall.*$aqtCommit" }) {
            Write-Host "[2/3] aqtinstall @ $aqtCommit уже установлен"
            continue
        }
    }
    Write-Host ("[{0}/{1}] {2}" -f ($i + 1), $plan.Count, (Format-Command $step))
    $stepArgs = $step.Args
    & $step.Exe @stepArgs
    if ($LASTEXITCODE -ne 0) {
        Write-Host "ОШИБКА: код $LASTEXITCODE"
        exit 1
    }
}

& $python -m aqt version
$checks = @(
    (Join-Path $qtDir 'plugins\platforms\qoffscreen.dll'),
    (Join-Path $qtDir 'plugins\platforms\qwindows.dll'),
    (Join-Path $qtDir 'bin\qtdiag.exe'),
    (Join-Path $qtDir 'bin\windeployqt.exe')
)
$missing = $checks | Where-Object { -not (Test-Path -LiteralPath $_) }
if ($missing) {
    $missing | ForEach-Object { Write-Host "Нет $_" }
    exit 1
}
Write-Host ("Qt {0} установлен: {1} ({2:N0} с)" -f $qtVersion, $qtDir, $watch.Elapsed.TotalSeconds)
exit 0
