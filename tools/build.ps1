<#
.SYNOPSIS
    Сборка выпуска «АкроБати»: Release → build\deploy → dist\AcroBatya-<версия>-setup.exe (эталон 10.3).

.DESCRIPTION
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\build.ps1 [-SkipInstaller] [-Version x.y.z] [-NoBuild] [-WhatIf]

    Шаги (каждый печатает путь, размер и время):
      1. cmake --preset msvc-release                         (через tools\vcenv.cmd; лог build\release\configure.log)
      2. cmake --build --preset msvc-release --target AcroBatya render_refs render_tests bench
         (лог build\release\build.log; LTCG-линковка с /GL-библиотеками MuPDF долгая — раз в минуту
         печатается, что сборка идёт; таймаут -BuildTimeoutMinutes, по умолчанию 30)
      3. ctest --test-dir build\release -L render             (лог build\release\ctest-render.log)
      4. cmake --install build\release --prefix build\deploy  (cmake\deploy.cmake: AcroBatya.exe + windeployqt)
      5. докладка в build\deploy: platforms\qoffscreen.dll (рядом с qwindows.dll), DLL из
         Microsoft.VC143.CRT ($env:VCToolsRedistDir\x64 после vcvars) рядом с AcroBatya.exe, qpdf.exe и
         его DLL, tessdata\{rus,eng,deu}.traineddata, licenses\ (installer\licenses + THIRD-PARTY-NOTICES.md).
         CRT — одна копия на {app} для AcroBatya.exe и qpdf.exe: из двух источников (VC Redist и
         C:\Tools\qpdf\bin) берётся более новая версия каждой DLL — новая CRT совместима со старыми
         сборками, наоборот — нет (qpdf 12.4.2 собран MSVC 14.51 и несёт CRT 14.51, наш — 14.44)
      6. build\deploy\AcroBatya.exe --self-check — с PATH только из системных папок (доказывает, что
         развёртывание самодостаточно), данные — в build\self-check; код 0
      7. ISCC /Qp "/DAppVer=<версия>" "/DDeployDir=<абс>\build\deploy" installer\ottisk.iss
         → dist\AcroBatya-<версия>-setup.exe

    -NoBuild       — без шагов 1–3: развёртывание из уже собранного build\release.
    -SkipInstaller — без шага 7.
    -WhatIf        — только напечатать план шагов и команды; ничего не запускать.
    -Version       — версия установщика; по умолчанию из cmake\product.cmake (set(OTTISK_VERSION "x.y.z")),
                     если файла нет — обязателен.
    -BuildDir / -DeployDir / -DistDir — другие каталоги (только с -NoBuild; для проверки самого скрипта).

    Код выхода 0 — всё собрано и проверено, 1 — ошибка (печатается шаг и хвост лога).
    Файл в UTF-8 с BOM: Windows PowerShell 5.1 иначе читает кириллицу неверно.
#>
[CmdletBinding()]
param(
    [switch]$SkipInstaller,
    [string]$Version,
    [switch]$NoBuild,
    [switch]$WhatIf,
    [int]$BuildTimeoutMinutes = 30,
    [string]$BuildDir,
    [string]$DeployDir,
    [string]$DistDir
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding $false

$root = Split-Path -Parent $PSScriptRoot
$toolsRoot = if ($env:OTTISK_TOOLS) { $env:OTTISK_TOOLS } else { 'C:\Tools' }
$qtRoot = if ($env:QT_ROOT) { $env:QT_ROOT } else { 'C:\Qt\6.12.0\msvc2022_64' }
$preset = 'msvc-release'
$targets = 'AcroBatya render_refs render_tests bench'
$iss = Join-Path $root 'installer\ottisk.iss'
$tessLanguages = 'rus', 'eng', 'deu'

function Resolve-Dir([string]$value, [string]$default) {
    if (-not $value) { $value = $default }
    if (-not [IO.Path]::IsPathRooted($value)) { $value = Join-Path $root $value }
    return [IO.Path]::GetFullPath($value)
}

$buildDirAbs = Resolve-Dir $BuildDir 'build\release'
$deployDirAbs = Resolve-Dir $DeployDir 'build\deploy'
$distDirAbs = Resolve-Dir $DistDir 'dist'
$selfCheckDir = Join-Path (Split-Path -Parent $deployDirAbs) 'self-check'

function Get-ProductVersion {
    # cmake\product.cmake: set(OTTISK_VERSION "0.0.1") — кавычки необязательны
    $file = Join-Path $root 'cmake\product.cmake'
    if (-not (Test-Path -LiteralPath $file)) { return $null }
    $text = Get-Content -LiteralPath $file -Raw -Encoding UTF8
    if ($text -match 'set\s*\(\s*OTTISK_VERSION\s+"?(\d+\.\d+\.\d+)"?\s*\)') { return $Matches[1] }
    throw "В $file нет строки set(OTTISK_VERSION ""x.y.z"")"
}

function Format-Size([long]$bytes) {
    if ($bytes -ge 1MB) { return '{0:N1} МБ' -f ($bytes / 1MB) }
    return '{0:N0} КБ' -f ($bytes / 1KB)
}

function Get-DirSize([string]$path) {
    $sum = (Get-ChildItem -LiteralPath $path -Recurse -File -Force | Measure-Object Length -Sum).Sum
    if (-not $sum) { return 0 }
    return [long]$sum
}

function Format-Elapsed([Diagnostics.Stopwatch]$watch) {
    $t = $watch.Elapsed
    if ($t.TotalMinutes -ge 1) { return '{0} мин {1:D2} с' -f [int][Math]::Floor($t.TotalMinutes), $t.Seconds }
    return '{0:N1} с' -f $t.TotalSeconds
}

function Find-Iscc {
    $candidates = @()
    if ($env:LOCALAPPDATA) { $candidates += Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe' }
    if (${env:ProgramFiles(x86)}) { $candidates += Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe' }
    if ($env:ProgramFiles) { $candidates += Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe' }
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) { return $candidate }
    }
    $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    throw 'Не найден Inno Setup 6 (ISCC.exe): ожидается %LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe'
}

function Get-LastProgressLine([string]$log) {
    try {
        $lines = Get-Content -LiteralPath $log -Tail 40 -ErrorAction Stop
    }
    catch {
        return 'лог ещё пуст'
    }
    $progress = $lines | Where-Object { $_ -match '^\[\d+/\d+\]' } | Select-Object -Last 1
    if (-not $progress) { $progress = $lines | Select-Object -Last 1 }
    if (-not $progress) { return 'лог ещё пуст' }
    $progress = "$progress".Trim()
    if ($progress.Length -gt 110) { $progress = $progress.Substring(0, 107) + '...' }
    return $progress
}

function Show-LogErrors([string]$log, [int]$tail = 40) {
    if (-not (Test-Path -LiteralPath $log)) { return }
    $errors = Select-String -LiteralPath $log -Pattern 'error C\d{4}|error LNK\d{4}|fatal error|FAILED:|CMake Error|\*\*\*Failed|Errors while running CTest' |
        Select-Object -First 40
    if ($errors) {
        foreach ($line in $errors) { Write-Host "      $($line.Line.Trim())" }
    }
    else {
        Get-Content -LiteralPath $log -Tail $tail | ForEach-Object { Write-Host "      $_" }
    }
    Write-Host "      полный лог: $log"
}

function Invoke-VcLogged {
    # cmd /c "tools\vcenv.cmd && <команда> > <лог> 2>&1"; раз в минуту — строка «идёт …» (если -Progress)
    param([string]$Command, [string]$Log, [int]$TimeoutMinutes = 0, [switch]$Progress)
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Log) | Out-Null
    $cmdLine = '/d /s /c "call tools\vcenv.cmd && ' + $Command + ' > "' + $Log + '" 2>&1"'
    $process = Start-Process -FilePath $env:ComSpec -ArgumentList $cmdLine -WorkingDirectory $root `
        -NoNewWindow -PassThru
    $null = $process.Handle  # без этого ExitCode у Start-Process -PassThru бывает пустым
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $nextReport = 60
    while (-not $process.WaitForExit(5000)) {
        if ($TimeoutMinutes -gt 0 -and $watch.Elapsed.TotalMinutes -ge $TimeoutMinutes) {
            & taskkill.exe /T /F /PID $process.Id 2>&1 | Out-Null
            throw "Таймаут $TimeoutMinutes мин: $Command (лог $Log)"
        }
        if ($Progress -and $watch.Elapsed.TotalSeconds -ge $nextReport) {
            $nextReport += 60
            Write-Host ("      … идёт {0}: {1}" -f (Format-Elapsed $watch), (Get-LastProgressLine $Log))
        }
    }
    $process.WaitForExit()
    return $process.ExitCode
}

function Get-VcRedistCrtDir {
    # VCToolsRedistDir задаёт vcvars64.bat; запасной путь — самая новая версия в BuildTools\VC\Redist\MSVC
    Push-Location $root
    try {
        $ErrorActionPreference = 'Continue'  # PowerShell 5.1: stderr native-команды при 'Stop' стал бы исключением
        $output = & $env:ComSpec /d /c "call tools\vcenv.cmd && set VCToolsRedistDir" 2>$null
    }
    finally {
        Pop-Location
    }
    foreach ($line in $output) {
        if ($line -match '^VCToolsRedistDir=(.+)$') {
            $dir = Join-Path $Matches[1].Trim() 'x64\Microsoft.VC143.CRT'
            if (Test-Path -LiteralPath $dir) { return $dir }
        }
    }
    $base = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\2022\BuildTools\VC\Redist\MSVC'
    $found = Get-ChildItem -LiteralPath $base -Directory -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match '^\d+\.\d+\.\d+$' } |
        Sort-Object { [version]$_.Name } -Descending |
        ForEach-Object { Join-Path $_.FullName 'x64\Microsoft.VC143.CRT' } |
        Where-Object { Test-Path -LiteralPath $_ } |
        Select-Object -First 1
    if ($found) { return $found }
    throw 'Не найдена папка Microsoft.VC143.CRT (ни $env:VCToolsRedistDir после vcvars64, ни BuildTools\VC\Redist\MSVC\*)'
}

function Get-FileVersion([IO.FileInfo]$file) {
    # FileVersion-строка бывает с хвостом («… built by: …») — версия из числовых полей
    $info = $file.VersionInfo
    return New-Object Version $info.FileMajorPart, $info.FileMinorPart, $info.FileBuildPart, $info.FilePrivatePart
}

function Copy-Deploy([string]$source, [string]$destinationDir) {
    New-Item -ItemType Directory -Force -Path $destinationDir | Out-Null
    Copy-Item -LiteralPath $source -Destination $destinationDir -Force
}

function Invoke-SelfCheck([string]$program) {
    # Проверка развёртывания, а не машины: системный PATH без Qt/C:\Tools, свои папки данных.
    New-Item -ItemType Directory -Force -Path $selfCheckDir | Out-Null
    $names = 'PATH', 'OTTISK_DATA_DIR', 'OTTISK_INSTANCE', 'QT_QPA_PLATFORM', 'QT_PLUGIN_PATH', 'QT_FORCE_STDERR_LOGGING'
    $saved = @{}
    foreach ($name in $names) { $saved[$name] = [Environment]::GetEnvironmentVariable($name) }
    $out = Join-Path $selfCheckDir 'self-check.txt'
    $err = Join-Path $selfCheckDir 'self-check-errors.txt'
    try {
        $sys = $env:SystemRoot
        $env:PATH = "$sys\System32;$sys;$sys\System32\Wbem;$sys\System32\WindowsPowerShell\v1.0"
        $env:OTTISK_DATA_DIR = Join-Path $selfCheckDir 'data'
        $env:OTTISK_INSTANCE = 'selfcheck'
        $env:QT_QPA_PLATFORM = $null
        $env:QT_PLUGIN_PATH = $null
        $env:QT_FORCE_STDERR_LOGGING = '1'
        $process = Start-Process -FilePath $program -ArgumentList '--self-check' -NoNewWindow -PassThru `
            -RedirectStandardOutput $out -RedirectStandardError $err -WorkingDirectory $selfCheckDir
        $null = $process.Handle
        if (-not $process.WaitForExit(120000)) {
            & taskkill.exe /T /F /PID $process.Id 2>&1 | Out-Null
            throw 'AcroBatya.exe --self-check не завершился за 120 с'
        }
        $process.WaitForExit()
        $code = $process.ExitCode
    }
    finally {
        foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $saved[$name]) }
    }
    $lines = @(Get-Content -LiteralPath $out -Encoding UTF8 -ErrorAction SilentlyContinue)
    $lines | ForEach-Object { Write-Host "      $_" }
    if ($code -ne 0) {
        Get-Content -LiteralPath $err -Encoding UTF8 -ErrorAction SilentlyContinue | ForEach-Object { Write-Host "      $_" }
        throw "AcroBatya.exe --self-check завершился с кодом $code"
    }
    return $lines
}

# ---------------------------------------------------------------- план

$productVersion = Get-ProductVersion
if ($Version) {
    if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw "-Version ""$Version"": ожидается x.y.z" }
    if ($productVersion -and $productVersion -ne $Version) {
        Write-Warning "-Version $Version не совпадает с cmake\product.cmake ($productVersion): AcroBatya.exe покажет $productVersion"
    }
    $appVersion = $Version
}
else {
    $appVersion = $productVersion
}
$versionText = if ($appVersion) { $appVersion } else { '<версия: нет cmake\product.cmake — укажите -Version>' }

if (($BuildDir -or $DeployDir -or $DistDir) -and -not $NoBuild -and -not $WhatIf) {
    throw '-BuildDir/-DeployDir/-DistDir — только вместе с -NoBuild (пресет msvc-release всегда собирает в build\release)'
}

$steps = New-Object System.Collections.Generic.List[object]
if (-not $NoBuild) {
    $steps.Add(@{ Key = 'configure'; Title = 'Конфигурация Release'; Command = "cmake --preset $preset" })
    $steps.Add(@{ Key = 'build'; Title = 'Сборка Release'; Command = "cmake --build --preset $preset --target $targets" })
    $steps.Add(@{ Key = 'ctest'; Title = 'Рендер-тесты'; Command = "ctest --test-dir build\release -L render --output-on-failure --no-tests=error" })
}
$steps.Add(@{ Key = 'install'; Title = 'Установка в deploy'; Command = "cmake --install ""$buildDirAbs"" --prefix ""$deployDirAbs""" })
$steps.Add(@{ Key = 'extras'; Title = 'Докладка'; Command = "qoffscreen.dll → к qwindows.dll; Microsoft.VC143.CRT\*.dll → корень (CRT-DLL новее — из qpdf\bin); $toolsRoot\qpdf\bin\qpdf.exe + qpdf*.dll; $toolsRoot\tessdata\{$($tessLanguages -join ',')}.traineddata → tessdata\; installer\licenses + THIRD-PARTY-NOTICES.md → licenses\" })
$steps.Add(@{ Key = 'selfcheck'; Title = 'Самопроверка'; Command = "$deployDirAbs\AcroBatya.exe --self-check" })
if (-not $SkipInstaller) {
    $steps.Add(@{ Key = 'iscc'; Title = 'Установщик'; Command = "ISCC /Qp ""/DAppVer=$versionText"" ""/DDeployDir=$deployDirAbs"" installer\ottisk.iss → $distDirAbs\AcroBatya-$versionText-setup.exe" })
}

Write-Host "АкроБатя $versionText — сборка выпуска"
Write-Host 'План:'
for ($i = 0; $i -lt $steps.Count; $i++) {
    Write-Host ("  [{0}/{1}] {2}: {3}" -f ($i + 1), $steps.Count, $steps[$i].Title, $steps[$i].Command)
}
if ($NoBuild) { Write-Host '  (-NoBuild: конфигурация, сборка и рендер-тесты пропущены)' }
if ($SkipInstaller) { Write-Host '  (-SkipInstaller: установщик не собирается)' }
if ($WhatIf) {
    Write-Host '-WhatIf: ничего не запускалось.'
    exit 0
}

# ---------------------------------------------------------------- выполнение

if (-not $appVersion) {
    Write-Host 'ОШИБКА: нет cmake\product.cmake с set(OTTISK_VERSION "x.y.z") — укажите -Version x.y.z'
    exit 1
}
$iscc = if ($SkipInstaller) { $null } else { Find-Iscc }  # нет компилятора — узнать до долгой сборки
$total = [Diagnostics.Stopwatch]::StartNew()
$stepNo = 0
$currentLog = $null

try {
    foreach ($step in $steps) {
        $stepNo++
        Write-Host ("[{0}/{1}] {2}…" -f $stepNo, $steps.Count, $step.Title)
        $watch = [Diagnostics.Stopwatch]::StartNew()
        switch ($step.Key) {
            'configure' {
                $currentLog = Join-Path $buildDirAbs 'configure.log'
                $code = Invoke-VcLogged -Command $step.Command -Log $currentLog -TimeoutMinutes 120 -Progress
                if ($code -ne 0) { throw "cmake --preset ${preset}: код $code" }
                Write-Host "      $buildDirAbs — $(Format-Elapsed $watch); лог $currentLog"
            }
            'build' {
                $currentLog = Join-Path $buildDirAbs 'build.log'
                Write-Host "      цели: $targets; лог $currentLog (LTCG-линковка долгая, таймаут $BuildTimeoutMinutes мин)"
                $code = Invoke-VcLogged -Command $step.Command -Log $currentLog -TimeoutMinutes $BuildTimeoutMinutes -Progress
                if ($code -ne 0) { throw "cmake --build: код $code" }
                $exe = Join-Path $buildDirAbs 'bin\AcroBatya.exe'
                if (-not (Test-Path -LiteralPath $exe)) { throw "Сборка не создала $exe" }
                Write-Host "      $exe — $(Format-Size (Get-Item -LiteralPath $exe).Length), $(Format-Elapsed $watch)"
            }
            'ctest' {
                $currentLog = Join-Path $buildDirAbs 'ctest-render.log'
                $savedPath = $env:PATH
                $env:PATH = "$qtRoot\bin;$env:PATH"
                try {
                    $code = Invoke-VcLogged -Command $step.Command -Log $currentLog -TimeoutMinutes 60 -Progress
                }
                finally {
                    $env:PATH = $savedPath
                }
                $summary = Select-String -LiteralPath $currentLog -Pattern 'tests passed|tests failed' | Select-Object -Last 1
                if ($summary) { Write-Host "      $($summary.Line.Trim())" }
                if ($code -ne 0) { throw "ctest -L render: код $code" }
                Write-Host "      $(Format-Elapsed $watch); лог $currentLog"
            }
            'install' {
                if (-not (Test-Path -LiteralPath (Join-Path $buildDirAbs 'CMakeCache.txt'))) {
                    throw "Нет сборки $buildDirAbs (CMakeCache.txt) — запустите без -NoBuild"
                }
                if (Test-Path -LiteralPath $deployDirAbs) { Remove-Item -LiteralPath $deployDirAbs -Recurse -Force }
                $currentLog = Join-Path $buildDirAbs 'install.log'
                $code = Invoke-VcLogged -Command $step.Command -Log $currentLog -TimeoutMinutes 15
                if ($code -ne 0) { throw "cmake --install: код $code" }
                $exe = Join-Path $deployDirAbs 'AcroBatya.exe'
                if (-not (Test-Path -LiteralPath $exe)) { throw "cmake --install не положил $exe" }
                Write-Host "      $deployDirAbs — $(Format-Size (Get-DirSize $deployDirAbs)), $(Format-Elapsed $watch)"
            }
            'extras' {
                $currentLog = $null
                # qoffscreen.dll — туда же, где qwindows.dll (windeployqt: plugins\platforms)
                $qwindows = Get-ChildItem -LiteralPath $deployDirAbs -Recurse -Filter 'qwindows.dll' | Select-Object -First 1
                if (-not $qwindows) { throw "В $deployDirAbs нет qwindows.dll — windeployqt не отработал (см. install.log)" }
                $qoffscreen = Join-Path $qtRoot 'plugins\platforms\qoffscreen.dll'
                if (-not (Test-Path -LiteralPath $qoffscreen)) { throw "Нет $qoffscreen" }
                Copy-Deploy $qoffscreen $qwindows.DirectoryName
                Write-Host "      qoffscreen.dll → $($qwindows.DirectoryName)"
                # CRT: все DLL папки Microsoft.VC143.CRT — рядом с AcroBatya.exe (app-local)
                $crtDir = Get-VcRedistCrtDir
                $crt = Get-ChildItem -LiteralPath $crtDir -Filter '*.dll'
                foreach ($dll in $crt) { Copy-Deploy $dll.FullName $deployDirAbs }
                Write-Host "      Microsoft.VC143.CRT: $($crt.Count) DLL из $crtDir"
                # qpdf.exe и его DLL; CRT-DLL из папки qpdf заменяют наши, только если они новее
                $qpdfBin = Join-Path $toolsRoot 'qpdf\bin'
                $qpdfExe = Join-Path $qpdfBin 'qpdf.exe'
                if (-not (Test-Path -LiteralPath $qpdfExe)) { throw "Нет $qpdfExe" }
                Copy-Deploy $qpdfExe $deployDirAbs
                $crtNames = $crt | ForEach-Object { $_.Name.ToLowerInvariant() }
                $allQpdfDlls = @(Get-ChildItem -LiteralPath $qpdfBin -Filter '*.dll')
                $qpdfDlls = @($allQpdfDlls | Where-Object { $crtNames -notcontains $_.Name.ToLowerInvariant() })
                foreach ($dll in $qpdfDlls) { Copy-Deploy $dll.FullName $deployDirAbs }
                Write-Host "      qpdf.exe + $(($qpdfDlls | ForEach-Object Name) -join ', ')"
                $newer = @()
                foreach ($dll in ($allQpdfDlls | Where-Object { $crtNames -contains $_.Name.ToLowerInvariant() })) {
                    $ours = Get-Item -LiteralPath (Join-Path $deployDirAbs $dll.Name)
                    if ((Get-FileVersion $dll) -gt (Get-FileVersion $ours)) {
                        Copy-Deploy $dll.FullName $deployDirAbs
                        $newer += "$($dll.Name) $(Get-FileVersion $dll)"
                    }
                }
                if ($newer) { Write-Host "      CRT новее из папки qpdf: $($newer -join ', ')" }
                # tessdata
                $tessDir = Join-Path $deployDirAbs 'tessdata'
                foreach ($lang in $tessLanguages) {
                    $file = Join-Path $toolsRoot "tessdata\$lang.traineddata"
                    if (-not (Test-Path -LiteralPath $file)) { throw "Нет $file — tools\get-tessdata.ps1" }
                    Copy-Deploy $file $tessDir
                }
                Write-Host "      tessdata: $($tessLanguages -join ', ') — $(Format-Size (Get-DirSize $tessDir))"
                # лицензии
                $licDir = Join-Path $deployDirAbs 'licenses'
                New-Item -ItemType Directory -Force -Path $licDir | Out-Null
                Copy-Item -Path (Join-Path $root 'installer\licenses\*') -Destination $licDir -Recurse -Force
                Copy-Deploy (Join-Path $root 'THIRD-PARTY-NOTICES.md') $licDir
                $licCount = (Get-ChildItem -LiteralPath $licDir -Recurse -File).Count
                Write-Host "      licenses: $licCount файлов"
                Write-Host "      $deployDirAbs — $(Format-Size (Get-DirSize $deployDirAbs)), $(Format-Elapsed $watch)"
            }
            'selfcheck' {
                $currentLog = Join-Path $selfCheckDir 'self-check-errors.txt'
                $selfCheck = Invoke-SelfCheck (Join-Path $deployDirAbs 'AcroBatya.exe')
                # -NoBuild берёт готовый build\release: после смены версии в cmake\product.cmake он устарел
                $reported = $selfCheck | Where-Object { $_ -match '^version:\s*(\S+)' } | Select-Object -First 1
                if ($productVersion -and $reported -and $reported -notmatch ('^version:\s*' + [regex]::Escape($productVersion) + '\b')) {
                    throw "AcroBatya.exe сообщает «$reported», а cmake\product.cmake — ${productVersion}: сборка $buildDirAbs устарела (запустите без -NoBuild)"
                }
                Write-Host "      код 0, $(Format-Elapsed $watch)"
            }
            'iscc' {
                $currentLog = $null
                $setup = Join-Path $distDirAbs "AcroBatya-$appVersion-setup.exe"
                New-Item -ItemType Directory -Force -Path $distDirAbs | Out-Null
                if (Test-Path -LiteralPath $setup) { Remove-Item -LiteralPath $setup -Force }
                & $iscc /Qp "/DAppVer=$appVersion" "/DDeployDir=$deployDirAbs" "/O$distDirAbs" $iss
                $isccCode = $LASTEXITCODE
                Write-Host ''  # /Qp печатает прогресс без перевода строки
                if ($isccCode -ne 0) { throw "ISCC завершился с кодом $isccCode" }
                if (-not (Test-Path -LiteralPath $setup)) { throw "ISCC не создал $setup" }
                Write-Host "      $setup — $(Format-Size (Get-Item -LiteralPath $setup).Length), $(Format-Elapsed $watch)"
            }
        }
    }
}
catch {
    Write-Host "ОШИБКА на шаге [$stepNo/$($steps.Count)]: $($_.Exception.Message)"
    if ($currentLog) { Show-LogErrors $currentLog }
    exit 1
}

$result = if ($SkipInstaller) { $deployDirAbs } else { Join-Path $distDirAbs "AcroBatya-$appVersion-setup.exe" }
Write-Host "Готово за $(Format-Elapsed $total): $result"
exit 0
