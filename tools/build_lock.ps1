<#
.SYNOPSIS
    Сборка одной цели под замком .queue\build.lock — один Ninja на build\<пресет> в каждый момент.

.DESCRIPTION
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_lock.ps1 -Target <цель>[,<цель>…]
        [-Preset msvc-debug] [-TimeoutMinutes 30]

    Замок — файл .queue\build.lock (создаётся атомарно, CreateNew) с PID, временем старта процесса,
    временем взятия, целью и пресетом. Чужой живой PID — ждём (раз в 15 с строка «жду…») до
    -TimeoutMinutes, затем код 3; мёртвый PID (или PID переиспользован другим процессом) — замок
    забирается. Затем tools\vcenv.cmd → cmake --build --preset <пресет> --target <цели>, лог
    build\<каталог пресета>\build.log (перезаписывается). При неудаче печатает строки с «error C»,
    «error LNK», «fatal error», «FAILED:» (или хвост лога). Замок снимается в finally — только свой.
    Код выхода: код cmake --build; 2 — нет конфигурации пресета; 3 — не дождались замка.
    Им собирает свою цель исполнитель, если он единственный в сессии (SKILL.md, раздел «Ultracode»).
    Файл в UTF-8 с BOM: Windows PowerShell 5.1 иначе читает кириллицу неверно.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string[]]$Target,
    [string]$Preset = 'msvc-debug',
    [int]$TimeoutMinutes = 30
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding $false

$root = Split-Path -Parent $PSScriptRoot
$targets = @($Target | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
if ($targets.Count -eq 0) {
    Write-Host 'Пустой -Target'
    exit 2
}
foreach ($name in $targets) {
    if ($name -notmatch '^[A-Za-z0-9_.+-]+$') {
        Write-Host "Недопустимое имя цели: $name"
        exit 2
    }
}
$buildDir = Join-Path $root ('build\' + ($Preset -replace '^msvc-', ''))
if (-not (Test-Path -LiteralPath (Join-Path $buildDir 'CMakeCache.txt'))) {
    Write-Host "Нет конфигурации $buildDir (CMakeCache.txt) — её делает главная сессия: cmake --preset $Preset"
    exit 2
}
$log = Join-Path $buildDir 'build.log'
$lockDir = Join-Path $root '.queue'
$lock = Join-Path $lockDir 'build.lock'
New-Item -ItemType Directory -Force -Path $lockDir | Out-Null

function Get-ProcessStartTicks([int]$id) {
    try {
        return (Get-Process -Id $id -ErrorAction Stop).StartTime.ToUniversalTime().Ticks
    }
    catch {
        return $null
    }
}

function Read-Lock {
    try {
        $info = @{}
        foreach ($line in [IO.File]::ReadAllLines($lock)) {
            if ($line -match '^(\w+)=(.*)$') { $info[$Matches[1]] = $Matches[2] }
        }
        return $info
    }
    catch {
        return $null
    }
}

function Test-LockAlive($info) {
    # живой — процесс с этим PID есть и (если удаётся узнать) запущен в то же время, что записано
    if (-not $info -or -not $info['pid']) { return $false }
    $id = 0
    if (-not [int]::TryParse($info['pid'], [ref]$id)) { return $false }
    if (-not (Get-Process -Id $id -ErrorAction SilentlyContinue)) { return $false }
    $ticks = Get-ProcessStartTicks $id
    if ($null -ne $ticks -and $info['pidStart'] -and "$ticks" -ne $info['pidStart']) { return $false }
    return $true
}

function Enter-Lock {
    $myStart = Get-ProcessStartTicks $PID
    $content = "pid=$PID`r`npidStart=$myStart`r`ntaken=$((Get-Date).ToString('s'))`r`ntarget=$($targets -join ',')`r`npreset=$Preset`r`n"
    $bytes = [Text.Encoding]::UTF8.GetBytes($content)
    $watch = [Diagnostics.Stopwatch]::StartNew()
    $nextReport = 0
    while ($true) {
        try {
            $stream = [IO.File]::Open($lock, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::Read)
            try { $stream.Write($bytes, 0, $bytes.Length) } finally { $stream.Dispose() }
            return $true
        }
        catch {
            # CreateNew не прошёл: если файла замка нет — это другая ошибка (права, путь), её не глотаем
            if (-not (Test-Path -LiteralPath $lock)) { throw }
        }
        $info = Read-Lock
        if ($null -eq $info) {
            Start-Sleep -Milliseconds 500  # файл только что создан и ещё пишется
            $info = Read-Lock
        }
        if (-not (Test-LockAlive $info)) {
            $owner = if ($info -and $info['pid']) { "PID $($info['pid'])" } else { 'неизвестного процесса' }
            Write-Host "Замок от $owner устарел (процесса нет) — забираю"
            Remove-Item -LiteralPath $lock -Force -ErrorAction SilentlyContinue
            continue
        }
        if ($watch.Elapsed.TotalMinutes -ge $TimeoutMinutes) {
            Write-Host "Не дождался замка за $TimeoutMinutes мин: PID $($info['pid']) собирает $($info['target']) с $($info['taken'])"
            return $false
        }
        if ($watch.Elapsed.TotalSeconds -ge $nextReport) {
            $nextReport += 15
            Write-Host "Жду замок: PID $($info['pid']) собирает $($info['target']) ($($info['preset'])) с $($info['taken'])"
        }
        Start-Sleep -Seconds 5
    }
}

function Exit-Lock {
    $info = Read-Lock
    if ($info -and $info['pid'] -eq "$PID") {
        Remove-Item -LiteralPath $lock -Force -ErrorAction SilentlyContinue
    }
}

if (-not (Enter-Lock)) { exit 3 }
$code = 1
try {
    $watch = [Diagnostics.Stopwatch]::StartNew()
    Write-Host "Сборка $($targets -join ' ') ($Preset), лог $log"
    $command = "call tools\vcenv.cmd && cmake --build --preset $Preset --target $($targets -join ' ') > ""$log"" 2>&1"
    $process = Start-Process -FilePath $env:ComSpec -ArgumentList "/d /s /c ""$command""" -WorkingDirectory $root `
        -NoNewWindow -PassThru
    $null = $process.Handle
    $process.WaitForExit()
    $code = $process.ExitCode
    if ($code -eq 0) {
        Write-Host ("Собрано: {0}, {1:N0} с" -f ($targets -join ' '), $watch.Elapsed.TotalSeconds)
    }
    else {
        Write-Host ("Сборка упала (код {0}, {1:N0} с):" -f $code, $watch.Elapsed.TotalSeconds)
        $errors = @()
        if (Test-Path -LiteralPath $log) {
            $errors = Select-String -LiteralPath $log -Pattern 'error C\d{4}|error LNK\d{4}|fatal error|FAILED:|CMake Error|ninja: error' |
                Select-Object -First 60
        }
        if ($errors) {
            foreach ($line in $errors) { Write-Host "  $($line.Line.Trim())" }
        }
        elseif (Test-Path -LiteralPath $log) {
            Get-Content -LiteralPath $log -Tail 40 | ForEach-Object { Write-Host "  $_" }
        }
        Write-Host "  полный лог: $log"
    }
}
finally {
    Exit-Lock
}
exit $code
