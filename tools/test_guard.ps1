<#
.SYNOPSIS
    Страж очереди: защитный хук claude-setup/hooks/guard.ps1 запрещает и разрешает то, что велит эталон 10.6.

.DESCRIPTION
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\test_guard.ps1 [-Verbose]

    Каждая команда подаётся в guard.ps1 так же, как это делает Claude Code перед вызовом Bash/PowerShell:
    JSON хука PreToolUse ({tool_name, cwd, tool_input.command}) на stdin; ожидаемый код выхода — 2 (запрещено)
    или 0 (можно). Основной набор — 6 команд эталона (3 запрещённых, 3 разрешённых, включая установщик
    dist\AcroBatya-0.0.1-setup.exe /VERYSILENT); дополнительный — регрессии остальных правил хука, в том числе
    JSON хука с BOM впереди (так пишет в stdin вызывающий с консолью UTF-8 — хук обязан его пережить).
    Ещё проверяется, что .claude\hooks\guard.ps1 совпадает с claude-setup (копию кладёт run-queue.ps1), и
    строка команды хука из claude-setup\settings.json целиком: через cmd.exe, из чужого каталога, с
    CLAUDE_PROJECT_DIR — запрет доходит кодом 2, разрешение — 0 (powershell -Command сам превратил бы 2 в 1).
    Код 0 — всё совпало (зелёный), 1 — есть расхождения (печатаются). Запускает tools/verify.py.
#>
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$Utf8 = New-Object System.Text.UTF8Encoding $false
try { [Console]::OutputEncoding = $Utf8 } catch { }
$OutputEncoding = $Utf8

$Repo = Split-Path $PSScriptRoot -Parent
$Guard = Join-Path $Repo 'claude-setup\hooks\guard.ps1'
if (-not (Test-Path -LiteralPath $Guard)) { Write-Host "Нет $Guard" -ForegroundColor Red; exit 1 }

# Запуск хука как это делает Claude Code: JSON на stdin (байты UTF-8), код выхода и stderr.
# Все случаи стартуют сразу параллельно — один запуск powershell стоит ~0,7 с.
# -HookCommand: запустить не guard.ps1 напрямую, а строку команды хука из settings.json — через cmd.exe,
# из чужого каталога и с CLAUDE_PROJECT_DIR, как это делает Claude Code.
# -Bom: JSON с BOM впереди — так пишет в stdin процесс с консолью UTF-8 (кодовая страница 65001): .NET
# Framework создаёт StandardInput в Console.InputEncoding и сразу пишет его преамбулу. Хук обязан это пережить,
# иначе ConvertFrom-Json падает и хук пропускает всё (код 0).
function Start-Guard([string]$tool, [string]$command, [string]$HookCommand = '', [bool]$Bom = $false) {
    $json = [ordered]@{
        session_id = 'test-guard'; hook_event_name = 'PreToolUse'; tool_name = $tool; cwd = $Repo
        tool_input = [ordered]@{ command = $command; description = 'test_guard.ps1' }
    } | ConvertTo-Json -Compress -Depth 4
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    if ($HookCommand) {
        $psi.FileName = 'cmd.exe'
        $psi.Arguments = '/d /s /c "' + $HookCommand + '"'
        $psi.EnvironmentVariables['CLAUDE_PROJECT_DIR'] = $Repo
    } else {
        $psi.FileName = 'powershell.exe'
        $psi.Arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$Guard`""
    }
    $psi.UseShellExecute = $false
    $psi.CreateNoWindow = $true
    $psi.RedirectStandardInput = $true
    $psi.RedirectStandardError = $true
    $psi.RedirectStandardOutput = $true
    $psi.StandardErrorEncoding = $Utf8
    $psi.StandardOutputEncoding = $Utf8
    $psi.WorkingDirectory = if ($HookCommand) { $env:TEMP } else { $Repo }
    $p = [System.Diagnostics.Process]::Start($psi)
    $bytes = $Utf8.GetBytes($json)
    if ($Bom) { $bytes = [byte[]](0xEF, 0xBB, 0xBF) + $bytes }
    $p.StandardInput.BaseStream.Write($bytes, 0, $bytes.Length)
    $p.StandardInput.Close()
    return [pscustomobject]@{ Proc = $p; Err = $p.StandardError.ReadToEndAsync(); Out = $p.StandardOutput.ReadToEndAsync() }
}

function Complete-Guard($h) {
    if (-not $h.Proc.WaitForExit(60000)) { try { $h.Proc.Kill() } catch { }; return [pscustomobject]@{ Code = -1; Text = 'таймаут 60 с' } }
    $h.Proc.WaitForExit()
    return [pscustomobject]@{ Code = $h.Proc.ExitCode; Text = (($h.Err.Result + ' ' + $h.Out.Result) -replace '\s+', ' ').Trim() }
}

# Основной набор (эталон 10.6): 3 запрещённых, 3 разрешённых.
$Main = @(
    @{ Name = 'VS Installer setup.exe (правка Build Tools)'; Tool = 'PowerShell'; Expect = 2
       Cmd = '& "C:\Program Files (x86)\Microsoft Visual Studio\Installer\setup.exe" modify --installPath "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools" --add Microsoft.VisualStudio.Component.VC.ATL --quiet' },
    @{ Name = 'msiexec /i (установка MSI)'; Tool = 'PowerShell'; Expect = 2
       Cmd = 'msiexec /i C:\Tools\dl\lo\LibreOffice_26.8.1_Win_x86-64.msi /qn' },
    @{ Name = 'запись в C:\Qt'; Tool = 'PowerShell'; Expect = 2
       Cmd = 'Set-Content -Path C:\Qt\6.12.0\msvc2022_64\bin\qt.conf -Value "[Paths]"' },
    @{ Name = 'тихая установка dist\AcroBatya-0.0.1-setup.exe /VERYSILENT'; Tool = 'PowerShell'; Expect = 0
       Cmd = '& dist\AcroBatya-0.0.1-setup.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /DIR="$env:TEMP\ottisk-install-test" /LOG="C:\Tools\logs\install-0.0.1.log"' },
    @{ Name = 'msiexec /a (распаковка LibreOffice в C:\Tools\libreoffice)'; Tool = 'PowerShell'; Expect = 0
       Cmd = 'msiexec /a C:\Tools\dl\lo\LibreOffice_26.8.1_Win_x86-64.msi /qn TARGETDIR=C:\Tools\libreoffice' },
    @{ Name = 'tools\build_lock.ps1 -Target'; Tool = 'PowerShell'; Expect = 0
       Cmd = 'powershell -NoProfile -ExecutionPolicy Bypass -File tools\build_lock.ps1 -Target unit_tests' }
)

# Дополнительные регрессии остальных правил.
$Extra = @(
    @{ Name = 'vs_buildtools.exe'; Tool = 'PowerShell'; Expect = 2; Cmd = 'C:\Tools\dl\vs\vs_buildtools.exe --add Microsoft.VisualStudio.Workload.VCTools --quiet --norestart' },
    @{ Name = 'msiexec /x'; Tool = 'PowerShell'; Expect = 2; Cmd = 'msiexec /x {11111111-2222-3333-4444-555555555555} /qn' },
    @{ Name = 'Start-Process msiexec /i'; Tool = 'PowerShell'; Expect = 2; Cmd = "Start-Process msiexec -ArgumentList '/i','C:\Tools\dl\lo\x.msi','/qn' -Wait" },
    @{ Name = 'winget --scope machine'; Tool = 'PowerShell'; Expect = 2; Cmd = 'winget install TheDocumentFoundation.LibreOffice -e --scope machine' },
    @{ Name = 'vc_redist /install'; Tool = 'PowerShell'; Expect = 2; Cmd = 'C:\Tools\dl\vc\vc_redist.x64.exe /install /quiet /norestart' },
    @{ Name = 'git push'; Tool = 'Bash'; Expect = 2; Cmd = 'git push origin main' },
    @{ Name = 'git push, JSON хука с BOM (консоль UTF-8)'; Tool = 'Bash'; Expect = 2; Cmd = 'git push origin main'; Bom = $true },
    @{ Name = 'git reset --hard'; Tool = 'Bash'; Expect = 2; Cmd = 'git reset --hard HEAD~1' },
    @{ Name = 'git clean -fdx'; Tool = 'Bash'; Expect = 2; Cmd = 'git clean -fdx' },
    @{ Name = 'pip install в venv-test'; Tool = 'PowerShell'; Expect = 2; Cmd = 'C:\Tools\venv-test\Scripts\python.exe -m pip install requests' },
    @{ Name = 'robocopy в C:\src\mupdf'; Tool = 'PowerShell'; Expect = 2; Cmd = 'robocopy tools\mupdf-patches C:\src\mupdf\source /E' },
    @{ Name = 'перенаправление в C:\Tools'; Tool = 'PowerShell'; Expect = 2; Cmd = 'cmake --version > C:\Tools\qpdf\cmake.txt' },
    @{ Name = 'cp в /c/Qt (Bash)'; Tool = 'Bash'; Expect = 2; Cmd = 'cp build/debug/bin/AcroBatya.exe /c/Qt/6.12.0/' },
    @{ Name = 'запись в scratchpad'; Tool = 'PowerShell'; Expect = 2; Cmd = 'Copy-Item build\x.png "$env:TEMP\claude\abc\scratchpad\x.png"' },
    @{ Name = 'удаление вне проекта'; Tool = 'PowerShell'; Expect = 2; Cmd = 'Remove-Item -Recurse -Force C:\Users\Public\Documents\x' },
    @{ Name = 'удаление корня проекта'; Tool = 'Bash'; Expect = 2; Cmd = 'rm -rf .' },
    @{ Name = 'правка эталона через оболочку'; Tool = 'PowerShell'; Expect = 2; Cmd = 'Add-Content docs\design.md "x"' },
    @{ Name = 'эталоны рендера руками'; Tool = 'PowerShell'; Expect = 2; Cmd = 'Copy-Item out.png tests\render\expected\mini\blank-a4.pdf.1.72.png' },
    @{ Name = 'реестр вне AcroBatya'; Tool = 'PowerShell'; Expect = 2; Cmd = 'reg add HKCU\Software\Microsoft\Windows\CurrentVersion\Run /v x /d y /f' },
    @{ Name = 'вложенный cmd /c с записью в C:\Tools'; Tool = 'PowerShell'; Expect = 2; Cmd = 'cmd /c "copy build\debug\bin\AcroBatya.exe C:\Tools\pdfcpu\"' },
    @{ Name = 'вложенный cmd //c (Git Bash) с записью в C:\Qt'; Tool = 'Bash'; Expect = 2; Cmd = 'cmd //c "tools\vcenv.cmd && copy build\debug\bin\AcroBatya.exe C:\Qt\6.12.0\"' },
    @{ Name = 'cmd //c (Git Bash): ctest под vcenv'; Tool = 'Bash'; Expect = 0; Cmd = 'cmd //c "tools\vcenv.cmd && ctest --test-dir build/debug -R unit.mu --output-on-failure"' },
    @{ Name = 'unins000.exe /VERYSILENT'; Tool = 'PowerShell'; Expect = 0; Cmd = '& "$env:TEMP\ottisk-install-test\unins000.exe" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART' },
    @{ Name = 'реестр HKCU\Software\Classes\AcroBatya.pdf'; Tool = 'PowerShell'; Expect = 0; Cmd = 'reg delete HKCU\Software\Classes\AcroBatya.pdf /f' },
    @{ Name = 'RegisteredApplications\AcroBatya'; Tool = 'PowerShell'; Expect = 0; Cmd = 'Remove-ItemProperty -Path HKCU:\Software\RegisteredApplications -Name AcroBatya' },
    @{ Name = 'сборка с логом в C:\Tools\logs'; Tool = 'PowerShell'; Expect = 0; Cmd = 'cmd /c "tools\vcenv.cmd && cmake --build build/debug --target unit_tests" 2>&1 | Tee-Object -FilePath C:\Tools\logs\build.txt' },
    @{ Name = 'OTTISK_PYTHON над PDF'; Tool = 'PowerShell'; Expect = 0; Cmd = '& "$env:OTTISK_PYTHON" -X utf8 -I tools/compat_check.py build/out.pdf --json C:\Tools\logs\compat.json' },
    @{ Name = 'mutool OCR: чтение tessdata, вывод в logs'; Tool = 'PowerShell'; Expect = 0; Cmd = 'C:\src\mupdf\platform\win32\x64\Release\mutool.exe draw -t rus+eng -d C:\Tools\tessdata -r 300 -o C:\Tools\logs\out.ocr.pdf scan.png' },
    @{ Name = 'чтение из C:\Qt в проект'; Tool = 'PowerShell'; Expect = 0; Cmd = 'Copy-Item C:\Qt\6.12.0\msvc2022_64\plugins\platforms\qoffscreen.dll build\deploy\platforms\' },
    @{ Name = 'удаление в build'; Tool = 'Bash'; Expect = 0; Cmd = 'rm -rf build/debug/gtest-xml && ctest --test-dir build/debug -L unit' },
    @{ Name = 'временный каталог'; Tool = 'PowerShell'; Expect = 0; Cmd = 'New-Item -ItemType Directory -Force "$env:TEMP\ottisk-shots" | Out-Null; Remove-Item -Recurse -Force "$env:TEMP\ottisk-shots"' },
    @{ Name = 'коммит и отметка done.mjs'; Tool = 'Bash'; Expect = 0; Cmd = 'node .claude/skills/next-task/done.mjs "этап 1: MuContext" "Контекст и ошибки"' }
)

function Test-Set([string]$title, $cases, [string]$HookCommand = '') {
    $handles = @($cases | ForEach-Object { Start-Guard $_.Tool $_.Cmd $HookCommand ([bool]$_.Bom) })
    $ok = 0
    Write-Host $title -ForegroundColor Cyan
    for ($i = 0; $i -lt $cases.Count; $i++) {
        $c = $cases[$i]
        $r = Complete-Guard $handles[$i]
        $want = if ($c.Expect -eq 2) { 'запрет' } else { 'можно' }
        if ($r.Code -eq $c.Expect) {
            $ok++
            Write-Host ("  ok    {0} — {1}" -f $c.Name, $want) -ForegroundColor Green
            if ($VerbosePreference -eq 'Continue' -and $r.Text) { Write-Host "        $($r.Text)" -ForegroundColor DarkGray }
        } else {
            Write-Host ("  FAIL  {0} — ждали {1} (код {2}), получили код {3}" -f $c.Name, $want, $c.Expect, $r.Code) -ForegroundColor Red
            Write-Host "        команда: $($c.Cmd)" -ForegroundColor Red
            if ($r.Text) { Write-Host "        хук: $($r.Text)" -ForegroundColor Red }
        }
    }
    return $ok
}

$started = Get-Date
$mainOk = Test-Set 'Основной набор (эталон 10.6):' $Main
$extraOk = Test-Set 'Дополнительные регрессии:' $Extra

# Команда хука из settings.json целиком (cmd.exe, чужой каталог, CLAUDE_PROJECT_DIR): код 2 доходит до Claude Code.
$Hook = @(
    @{ Name = 'команда хука: git push'; Tool = 'Bash'; Expect = 2; Cmd = 'git push origin main' },
    @{ Name = 'команда хука: git push, JSON с BOM'; Tool = 'Bash'; Expect = 2; Cmd = 'git push origin main'; Bom = $true },
    @{ Name = 'команда хука: ctest'; Tool = 'Bash'; Expect = 0; Cmd = 'ctest --test-dir build/debug -L unit --output-on-failure' }
)
$hookOk = $Hook.Count
$hookNote = ''
$settingsFile = Join-Path $Repo 'claude-setup\settings.json'
$hookCmd = ''
try { $hookCmd = [string]((Get-Content -Raw -Encoding UTF8 -LiteralPath $settingsFile | ConvertFrom-Json).hooks.PreToolUse[0].hooks[0].command) } catch { }
if (-not $hookCmd) {
    $hookOk = 0; $hookNote = 'нет команды хука в claude-setup\settings.json'
    Write-Host "  FAIL  $hookNote" -ForegroundColor Red
} elseif (-not (Test-Path -LiteralPath (Join-Path $Repo '.claude\hooks\guard.ps1'))) {
    $hookNote = 'пропуск: нет .claude — запустите .\run-queue.ps1 -Setup'
    Write-Host "Команда хука из settings.json: $hookNote" -ForegroundColor Yellow
} else {
    $hookOk = Test-Set 'Команда хука из settings.json (cmd.exe, чужой каталог, CLAUDE_PROJECT_DIR):' $Hook $hookCmd
}

# Копия в .claude должна совпадать с исходником (её кладёт run-queue.ps1 -Setup / при запуске очереди).
$copyOk = $true
$copy = Join-Path $Repo '.claude\hooks\guard.ps1'
if (Test-Path -LiteralPath $copy) {
    $copyOk = ((Get-FileHash $copy).Hash -eq (Get-FileHash $Guard).Hash)
    if (-not $copyOk) { Write-Host '  FAIL  .claude\hooks\guard.ps1 отличается от claude-setup — запустите .\run-queue.ps1 -Setup' -ForegroundColor Red }
}

$secs = [math]::Round(((Get-Date) - $started).TotalSeconds, 1)
$summary = "test_guard: основной набор $mainOk/$($Main.Count), дополнительно $extraOk/$($Extra.Count), команда хука $hookOk/$($Hook.Count)$(if ($hookNote) { " ($hookNote)" }), копия .claude: $(if ($copyOk) { 'совпадает' } else { 'РАСХОДИТСЯ' }) · $secs с"
if ($mainOk -eq $Main.Count -and $extraOk -eq $Extra.Count -and $hookOk -eq $Hook.Count -and $copyOk) {
    Write-Host $summary -ForegroundColor Green
    exit 0
}
Write-Host $summary -ForegroundColor Red
exit 1
