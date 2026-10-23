<#
.SYNOPSIS
    clang-tidy по исходникам «АкроБати»: только изменённые (-Changed) или все (-All).

.DESCRIPTION
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\lint.ps1 -Changed | -All [-Preset msvc-debug]

    -Changed — файлы из `git diff --name-only HEAD` (изменённые и проиндексированные) плюс
               неотслеживаемые *.cpp/*.h в src (`git ls-files --others --exclude-standard src`);
               до первого коммита — все исходники из индекса и неотслеживаемые в src.
    -All     — все *.cpp/*.h в src, tests, tools\imgcmp и fuzz (рубеж).

    База компиляции: build\<пресет>\compile_commands.json (CMAKE_EXPORT_COMPILE_COMMANDS=ON) в виде,
    понятном clang-tidy, собирается во %TEMP%\ottisk-lint\<пресет>\compile_commands.json:
      * unity-файлы (CMakeFiles\<цель>.dir\Unity\unity_N_cxx.cxx) раскрываются в записи по каждому
        включённому .cpp — с той же командой (иначе отдельных .cpp в базе нет вовсе);
      * /Yc, /Yu, /Fp убираются (clang-cl не читает .pch от cl), /FI<cmake_pch.hxx> остаётся —
        PCH-заголовок включается текстом; /WX убирается (предупреждения компилятора — не ошибки tidy);
      * cmake_pch.cxx, mocs_compilation.cpp и .rc пропускаются.
    clang-tidy получает --extra-arg=-D_CRT_USE_BUILTIN_OFFSETOF: offsetof из vcruntime.h для clang не
    constexpr, и qplugin.h Qt 6.12 иначе даёт clang-diagnostic-error. Заголовок проверяется через
    одноимённый .cpp из базы, если он есть, иначе сам (флаги clang-tidy подбирает по соседнему файлу).
    Запуск через tools\vcenv.cmd (clang-tidy 19 из VC\Tools\Llvm и заголовки MSVC), пачками по 25 файлов.
    Печатает только предупреждения и ошибки («файл:строка:столбец: warning|error: …») и итог.
    Код выхода: 0 — замечаний нет, 1 — есть замечания, файл не разобрался или clang-tidy не запустился.
    Файл в UTF-8 с BOM: Windows PowerShell 5.1 иначе читает кириллицу неверно.
#>
[CmdletBinding()]
param(
    [switch]$Changed,
    [switch]$All,
    [string]$Preset = 'msvc-debug'
)

$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding $false

$root = Split-Path -Parent $PSScriptRoot
if ($Changed.IsPresent -eq $All.IsPresent) {
    Write-Host 'Укажите ровно одно: -Changed или -All'
    exit 1
}

$buildName = $Preset -replace '^msvc-', ''
$compileDb = Join-Path $root "build\$buildName\compile_commands.json"
if (-not (Test-Path -LiteralPath $compileDb)) {
    Write-Host "Нет $compileDb — сначала cmake --preset $Preset"
    exit 1
}
$sourcePattern = '\.(cpp|cc|cxx|h|hpp)$'
$lintRoots = '^(src|tests|tools/imgcmp|fuzz)/'

function Invoke-Git([string[]]$gitArgs) {
    $ErrorActionPreference = 'Continue'
    # core.quotepath=off — кириллица в путях без восьмеричных \320\236 и кавычек
    $output = & git -C $root -c core.quotepath=off @gitArgs 2>$null
    return @{ Code = $LASTEXITCODE; Lines = @($output) }
}

function Get-CleanCommand([string]$command) {
    # без PCH-ключей cl (/Yc /Yu /Fp) и без /WX; /FI<cmake_pch.hxx> остаётся
    $clean = [regex]::Replace($command, '\s[/-](Yc|Yu|Fp)("[^"]*"|\S*)', '')
    $clean = [regex]::Replace($clean, '\s[/-]WX(?=\s|$)', '')
    return $clean
}

# ---------------------------------------------------------------- база компиляции для clang-tidy

$lintDbDir = Join-Path $env:TEMP "ottisk-lint\$buildName"
New-Item -ItemType Directory -Force -Path $lintDbDir | Out-Null
$entries = New-Object System.Collections.Generic.List[object]
$known = @{}
foreach ($entry in (Get-Content -LiteralPath $compileDb -Raw -Encoding UTF8 | ConvertFrom-Json)) {
    $file = [string]$entry.file
    if ($file -match '(cmake_pch\.cxx|mocs_compilation\.cpp|\.rc)$') { continue }
    if (-not $entry.command) { continue }
    $command = Get-CleanCommand ([string]$entry.command)
    if ($file -match '\\Unity\\unity_\d+_cxx\.cxx$' -and (Test-Path -LiteralPath $file)) {
        foreach ($line in [IO.File]::ReadAllLines($file)) {
            if ($line -match '^\s*#\s*include\s+"([^"]+)"') {
                $source = [IO.Path]::GetFullPath($Matches[1])
                $entries.Add([pscustomobject]@{ directory = $entry.directory; command = $command.Replace($file, $source); file = $source })
                $known[$source.ToLowerInvariant()] = $true
            }
        }
    }
    else {
        $source = [IO.Path]::GetFullPath($file)
        $entries.Add([pscustomobject]@{ directory = $entry.directory; command = $command; file = $source })
        $known[$source.ToLowerInvariant()] = $true
    }
}
$json = ConvertTo-Json -InputObject ([object[]]$entries.ToArray()) -Depth 3  # List[object] напрямую 5.1 не сериализует
[IO.File]::WriteAllText((Join-Path $lintDbDir 'compile_commands.json'), $json, (New-Object System.Text.UTF8Encoding $false))

# ---------------------------------------------------------------- какие файлы

$files = @()
if ($All) {
    foreach ($dir in 'src', 'tests', 'tools\imgcmp', 'fuzz') {
        $path = Join-Path $root $dir
        if (Test-Path -LiteralPath $path) {
            $files += Get-ChildItem -LiteralPath $path -Recurse -File |
                Where-Object { $_.Name -match $sourcePattern } |
                ForEach-Object { $_.FullName }
        }
    }
}
else {
    $diff = Invoke-Git @('diff', '--name-only', 'HEAD')
    $names = @()
    if ($diff.Code -eq 0) {
        $names += $diff.Lines
    }
    else {
        Write-Host 'git diff HEAD недоступен (коммитов ещё нет) — берутся все исходники из индекса'
        $names += (Invoke-Git @('ls-files')).Lines
    }
    $names += (Invoke-Git @('ls-files', '--others', '--exclude-standard', '--', 'src')).Lines
    $files = $names | Where-Object { $_ -match $sourcePattern -and $_ -match $lintRoots } |
        ForEach-Object { [IO.Path]::GetFullPath((Join-Path $root $_)) }
}
# заголовок — через одноимённый .cpp из базы, если он есть
$files = @($files | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | ForEach-Object {
        if ($_ -match '\.(h|hpp)$') {
            $sibling = [IO.Path]::ChangeExtension($_, '.cpp')
            if ($known.ContainsKey($sibling.ToLowerInvariant())) { $sibling } else { $_ }
        }
        else { $_ }
    } | Sort-Object -Unique)

if ($files.Count -eq 0) {
    Write-Host 'clang-tidy: нечего проверять'
    exit 0
}
Write-Host "clang-tidy: $($files.Count) файлов (база $lintDbDir)"

$configArg = if (Test-Path -LiteralPath (Join-Path $root '.clang-tidy')) { '--config-file=.clang-tidy' } else { '' }
$batchSize = 25
$findings = New-Object System.Collections.Generic.List[string]
$notParsed = New-Object System.Collections.Generic.List[string]
$failed = $false
$watch = [Diagnostics.Stopwatch]::StartNew()
Push-Location $root
try {
    for ($i = 0; $i -lt $files.Count; $i += $batchSize) {
        $batch = $files[$i..([Math]::Min($i + $batchSize, $files.Count) - 1)]
        $quoted = ($batch | ForEach-Object { '"' + $_ + '"' }) -join ' '
        $command = "call tools\vcenv.cmd && clang-tidy -p ""$lintDbDir"" $configArg --quiet --extra-arg=-D_CRT_USE_BUILTIN_OFFSETOF $quoted 2>&1"
        $ErrorActionPreference = 'Continue'
        $output = @(& $env:ComSpec /d /s /c """$command""")
        $code = $LASTEXITCODE
        $ErrorActionPreference = 'Stop'
        $batchFindings = 0
        foreach ($line in $output) {
            $text = "$line"
            if ($text -match ':\d+:\d+: (warning|error): ') {
                $findings.Add($text)
                $batchFindings++
            }
            elseif ($text -match '^Error while processing (.+)\.$') {
                $notParsed.Add($Matches[1])
            }
        }
        if ($code -ne 0 -and $batchFindings -eq 0) {
            # clang-tidy упал не из-за замечаний (нет конфигурации, сломан запуск…) — показать хвост
            $failed = $true
            $output | Select-Object -Last 15 | ForEach-Object { Write-Host "  $_" }
        }
    }
}
finally {
    Pop-Location
}

$unique = @($findings | Select-Object -Unique)
foreach ($line in $unique) { Write-Host $line }
if ($notParsed.Count -gt 0) {
    Write-Host "Не разобраны (ошибки компиляции clang выше): $($notParsed.Count) файлов"
}
Write-Host ("clang-tidy: замечаний {0}, {1:N0} с" -f $unique.Count, $watch.Elapsed.TotalSeconds)
if ($failed -or $unique.Count -gt 0 -or $notParsed.Count -gt 0) { exit 1 }
exit 0
