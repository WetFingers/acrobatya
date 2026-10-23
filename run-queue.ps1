<#
.SYNOPSIS
    Автоматическая очередь «АкроБати»: выполняет docs/plan.md сессию за сессией через Claude Code.

.DESCRIPTION
    Скрипт находит первую сессию плана с невыполненными пунктами (или непройденный рубеж), пишет задание
    в .queue/current.md и запускает `claude -p "/next-task"` в новой сессии. После каждого запуска считает
    отметки в плане, делает страховочный коммит и идёт дальше. Перенесён из bny-studio («Мастерская»)
    по эталону 10.6: без Docker/Postgres; этап 0 делается интерактивно с человеком, и пока он не закрыт
    («[x]» у всех пунктов и "verified": ["0"] в .queue/state.json), очередь не стартует.

    Лимит использования: основная модель → та же сессия на запасной (-FallbackModel, усилие -FallbackEffort)
    → лимит и у запасной — ждать до «resets …» плюс 2 минуты → снова основная модель на той же сессии.

    Функции разбора плана, состояния и лимита — в tools/queue-lib.ps1 (их же проверяет
    tools/test_reset_parse.ps1). Подробности — QUEUE.md.

.EXAMPLE
    .\run-queue.ps1 -Status       # прогресс по этапам
    .\run-queue.ps1 -Setup        # проверка окружения; "verified": ["0"] — только если все пункты этапа 0 уже [x]
    .\run-queue.ps1 -DryRun       # следующее задание в .queue\current.md, Claude не запускается
    .\run-queue.ps1               # очередь до конца плана (Opus 5.5 xhigh, запасная Fable 5.1 xhigh)
    .\run-queue.ps1 -MaxTasks 3   # не больше трёх заданий
#>
[CmdletBinding()]
param(
    [switch]$Setup,
    [switch]$Status,
    [switch]$DryRun,
    [switch]$Fast,
    # -Setup без tools/verify.py (только проверки окружения и этапа 0).
    [switch]$SkipVerify,
    # Брать усилие из заголовка сессии («усилие xhigh»), если -Effort/-VerifyEffort не заданы явно.
    # По умолчанию выключено: решение человека 23.10.2026 — Opus 5.5 xhigh во всех сессиях, флаги главнее заголовков.
    [switch]$UsePlanEffort,
    # Тест: вместо claude — заглушка (первые два вызова отвечают текстом лимита, третий — без лимита),
    # Wait-Until не спит, а печатает и пишет в .queue\simulate-limit.jsonl, до какого времени ждал бы.
    [switch]$SimulateLimit,
    [int]$MaxTasks = 0,
    [int]$MaxTurns = 300,
    [int]$VerifyMaxTurns = 300,
    [int]$Retries = 2,
    [int]$LimitWaitMinutes = 20,
    [string]$Model = 'claude-opus-5-5',
    # Запасная модель: на неё очередь переходит сама, когда у основной кончился лимит использования.
    [string]$FallbackModel = 'claude-fable-5-1',
    [ValidateSet('low', 'medium', 'high', 'xhigh', 'max', 'ultracode')]
    [string]$Effort = 'xhigh',
    [ValidateSet('low', 'medium', 'high', 'xhigh', 'max', 'ultracode')]
    [string]$VerifyEffort = 'xhigh',
    # Усилие запасной модели: ultracode для Fable CLI может отвергнуть — прогон упал бы как обычная неудача.
    [ValidateSet('low', 'medium', 'high', 'xhigh', 'max', 'ultracode')]
    [string]$FallbackEffort = 'xhigh',
    # Корень репозитория (по умолчанию — папка скрипта). Тест подставляет временную копию.
    [string]$Root = ''
)

$ErrorActionPreference = 'Stop'
$Utf8 = New-Object System.Text.UTF8Encoding $false
try { [Console]::OutputEncoding = $Utf8 } catch { }
$OutputEncoding = $Utf8
# Python внутри сессий печатает кириллицу: консоль Windows по умолчанию cp1252.
$env:PYTHONUTF8 = '1'
$env:PYTHONIOENCODING = 'utf-8'

$EffortLevels = @('low', 'medium', 'high', 'xhigh', 'max', 'ultracode')
$ScriptDir = $PSScriptRoot
if (-not $Root) { $Root = $ScriptDir }
$Root = (Resolve-Path -LiteralPath $Root).Path
Set-Location -LiteralPath $Root
. (Join-Path $ScriptDir 'tools\queue-lib.ps1')

$QueueDir    = Join-Path $Root '.queue'
$LogDir      = Join-Path $QueueDir 'log'
$StateFile   = Join-Path $QueueDir 'state.json'
$CurrentFile = Join-Path $QueueDir 'current.md'
$StopFile    = Join-Path $QueueDir 'STOP'
$SummaryLog  = Join-Path $LogDir 'summary.log'
$SimLog      = Join-Path $QueueDir 'simulate-limit.jsonl'
$ToolsRoot   = if ($env:OTTISK_TOOLS) { $env:OTTISK_TOOLS } else { 'C:\Tools' }
$DefaultAcroBatyaPython = Join-Path $ToolsRoot 'venv-test\Scripts\python.exe'
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$PrimaryModel = $Model
$SimCalls = 0
$SimExitCode = 0

# ---------- вывод ----------
function Say([string]$m, [string]$c = 'Cyan') { Write-Host "[$(Get-Date -Format 'HH:mm:ss')] $m" -ForegroundColor $c }
function Log-Summary([string]$m) { [IO.File]::AppendAllText($SummaryLog, "$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')  $m`r`n", $Utf8) }

function Add-SimRecord($rec) {
    [IO.File]::AppendAllText($SimLog, (($rec | ConvertTo-Json -Compress -Depth 4) + "`n"), $Utf8)
}

# Ждать по часам, а не по таймеру: сон ноутбука не растягивает ожидание.
function Wait-Until([datetime]$until) {
    if ($SimulateLimit) {
        Say ("SimulateLimit: ждал бы до {0} — не сплю" -f $until.ToString('dd.MM HH:mm:ss')) 'DarkYellow'
        Add-SimRecord ([ordered]@{ event = 'wait'; until = $until.ToString('o') })
        return
    }
    while ((Get-Date) -lt $until) {
        $left = ($until - (Get-Date)).TotalSeconds
        Start-Sleep -Seconds ([int][math]::Max(1, [math]::Min(60, [math]::Ceiling($left))))
    }
}

# ---------- файлы, состояние, план ----------
function Get-PlanPath {
    $f = Join-Path $Root 'docs\plan.md'
    if (Test-Path -LiteralPath $f) { return $f }
    throw "Не найден $f"
}
function Get-Plan { return , (Read-QueuePlan (Get-PlanPath)) }
function Get-State { return Read-QueueState $StateFile }
function Save-State($s) { Save-QueueState $StateFile $s }
function Find-Block($stages, [int]$stageNum, [string]$id) { return Find-QueueBlock $stages $stageNum $id }

function Update-PathFromRegistry {
    $parts = @(
        [Environment]::GetEnvironmentVariable('Path', 'Machine'),
        [Environment]::GetEnvironmentVariable('Path', 'User'),
        (Join-Path $env:USERPROFILE '.local\bin'),
        (Join-Path $env:APPDATA 'npm')
    ) | Where-Object { $_ }
    $env:Path = ($parts -join ';')
}

function Test-Cmd([string]$name) { [bool](Get-Command $name -ErrorAction SilentlyContinue) }

function Show-Status {
    $stages = Get-Plan
    $state = Get-State
    $verified = @($state.verified | ForEach-Object { "$_" })
    $width = [math]::Max(20, (@($stages | ForEach-Object { $_.Title.Length }) | Measure-Object -Maximum).Maximum)
    $all = 0; $done = 0; $sAll = 0; $sDone = 0
    foreach ($s in $stages) {
        $n = $s.Items.Count
        $d = @($s.Items | Where-Object { $_.State -ne ' ' }).Count
        $man = @($s.Items | Where-Object { $_.State -eq '~' }).Count
        $all += $n; $done += $d
        $sessions = @($s.Blocks | Where-Object { $_.Kind -eq 'task' })
        $ready = @($sessions | Where-Object { (Get-Pending $_) -eq 0 })
        $sAll += $sessions.Count; $sDone += $ready.Count
        $checks = @($s.Blocks | Where-Object { $_.Kind -eq 'verify' })
        $left = @($checks | Where-Object { $verified -notcontains $_.Id })
        $v = if ($checks.Count -eq 0) { '' }
             elseif ($left.Count -eq 0) { 'проверен' }
             elseif ($d -eq $n) { 'ждёт рубежа' } else { '' }
        if ($man -gt 0) { $v = ("$v (ручных: $man)").Trim() }
        $color = if ($d -eq $n -and ($left.Count -eq 0)) { 'Green' } elseif ($d -gt 0) { 'Yellow' } else { 'Gray' }
        Write-Host ("  Этап {0,2}. {1,-$width} {2,3}/{3,-3} сессий {4}/{5}  {6}" -f `
            $s.Num, $s.Title, $d, $n, $ready.Count, $sessions.Count, $v) -ForegroundColor $color
    }
    $manual = @($stages | ForEach-Object { $_.Items } | Where-Object { $_.State -eq '~' }).Count
    $checksAll = @($stages | ForEach-Object { $_.Blocks } | Where-Object { $_.Kind -eq 'verify' }).Count
    Write-Host ''
    Write-Host "  Всего: $done из $all пунктов · сессий $sDone из $sAll · рубежей и проверок $checksAll · ручных: $manual" -ForegroundColor Cyan
    $gate = Get-Stage0Gate $stages $state.verified
    if (-not $gate.Ok) {
        Write-Host "  Очередь не стартует: этап 0 не закрыт (осталось пунктов: $($gate.Pending.Count), verified 0: $(if ($gate.Verified) { 'есть' } else { 'нет' }))." -ForegroundColor Yellow
    } else {
        $u = Get-NextUnit $stages $state
        if ($u) {
            $what = if ($u.Kind -eq 'task') { "Сессия $($u.Block.Id) · $($u.Block.Title)" } else { "Рубеж $($u.Block.Id) · $($u.Block.Title)" }
            Write-Host "  Следующее: этап $($u.Stage.Num) · $what" -ForegroundColor Cyan
        } else {
            Write-Host '  Следующего задания нет: план выполнен (или остались только [~]).' -ForegroundColor Green
        }
    }
}

function Show-Stage0Gate($gate) {
    if ($gate.Pending.Count -gt 0) {
        Say ("Этап 0 не закрыт: не отмечено [x] пунктов — {0} из {1}:" -f $gate.Pending.Count, $gate.Total) 'Red'
        foreach ($it in $gate.Pending) { Write-Host ("    - [{0}] {1}" -f $it.State, (Get-ItemTitle $it.Text)) -ForegroundColor Yellow }
    }
    if (-not $gate.Verified) { Say 'В .queue\state.json нет "verified": ["0"].' 'Red' }
    Write-Host '  Этап 0 делает Claude в интерактивной сессии с человеком (эталон 8.1), не очередь.' -ForegroundColor Yellow
    Write-Host '  Что сделать: закончить пункты этапа 0 и отметить их [x] вручную в docs\plan.md,' -ForegroundColor Yellow
    Write-Host '  затем .\run-queue.ps1 -Setup (проверит окружение и запишет "verified": ["0"]),' -ForegroundColor Yellow
    Write-Host '  затем .\run-queue.ps1 -DryRun — должно показать «Сессия 1.1».' -ForegroundColor Yellow
}

# ---------- задание для Claude ----------
function Write-CurrentTask($u, $stages) {
    $s = $u.Stage
    $b = $u.Block
    $sb = New-Object System.Text.StringBuilder
    if ($u.Kind -eq 'task') {
        $pending = @($b.Items | Where-Object { $_.State -eq ' ' })
        $done = @($b.Items | Where-Object { $_.State -ne ' ' })
        [void]$sb.AppendLine("# Сессия $($b.Id) · $($b.Title)")
        [void]$sb.AppendLine('')
        [void]$sb.AppendLine("Этап $($s.Num). $($s.Title)")
        [void]$sb.AppendLine("Пунктов в этой сессии: $($pending.Count). Сделать **все**, по очереди, каждый — свой коммит и своя отметка.")
        [void]$sb.AppendLine('')
        foreach ($n in $b.Notes) { [void]$sb.AppendLine($n); [void]$sb.AppendLine('') }
        [void]$sb.AppendLine('## Пункты')
        [void]$sb.AppendLine('')
        foreach ($it in $pending) { [void]$sb.AppendLine("- [ ] $($it.Text)") }
        if ($done.Count -gt 0) {
            [void]$sb.AppendLine('')
            [void]$sb.AppendLine('Эти пункты сессии сделаны раньше — не переделывать:')
            [void]$sb.AppendLine('')
            foreach ($it in $done) { [void]$sb.AppendLine("- [$($it.State)] $($it.Text)") }
        }
    } else {
        [void]$sb.AppendLine("# Рубеж $($b.Id) · $($b.Title)")
        [void]$sb.AppendLine('')
        [void]$sb.AppendLine('Задача — проверка по разделу 3б навыка /next-task.')
        [void]$sb.AppendLine('')
        foreach ($n in $b.Notes) { [void]$sb.AppendLine($n); [void]$sb.AppendLine('') }
    }
    # Критерии этапов, которых касается задание.
    $nums = @($s.Num)
    if ($b.Covers) { $nums = @([regex]::Matches($b.Covers, '\d+') | ForEach-Object { [int]$_.Value }) }
    [void]$sb.AppendLine('')
    [void]$sb.AppendLine('## Критерии этапов')
    [void]$sb.AppendLine('')
    foreach ($num in $nums) {
        $st = $stages | Where-Object { $_.Num -eq $num } | Select-Object -First 1
        if ($st) { [void]$sb.AppendLine("- Этап $num. $($st.Title) — **Проверка:** $($st.Check)") }
    }
    Write-QueueText $CurrentFile $sb.ToString()
}

function Get-ArgValue([string[]]$list, [string]$name) {
    $i = [array]::IndexOf($list, $name)
    if ($i -ge 0 -and $i + 1 -lt $list.Count) { return $list[$i + 1] }
    return ''
}

# Заглушка claude для -SimulateLimit: записывает фактические аргументы и окружение вызова,
# первые два раза отвечает текстом лимита (без api_error_status 429 — случай из эталона 10.6),
# дальше — обычным ответом без лимита.
function Invoke-ClaudeStub([string[]]$cliArgs) {
    $script:SimCalls++
    $n = $script:SimCalls
    $rec = [ordered]@{
        event = 'call'; n = $n
        model = (Get-ArgValue $cliArgs '--model'); effort = (Get-ArgValue $cliArgs '--effort')
        env_effort = "$env:CLAUDE_CODE_EFFORT_LEVEL"; env_model = "$env:ANTHROPIC_MODEL"
        bg_wait = "$env:CLAUDE_CODE_PRINT_BG_WAIT_CEILING_MS"; no_autoupdate = "$env:DISABLE_AUTOUPDATER"
        args = ($cliArgs -join ' ')
    }
    if ($n -le 2) {
        # Основная «сбрасывается» через 30 минут, запасная — через 20 (время до минуты, как в настоящем ответе).
        $t = (Get-Date).AddMinutes($(if ($n -eq 1) { 30 } else { 20 }))
        $t = $t.Date.AddHours($t.Hour).AddMinutes($t.Minute)
        $rec.reset_at = $t.ToString('o')
        $text = "You've hit your session limit · resets $($t.ToString('H:mm')) (Europe/Berlin)"
        $rec.reply = $text
        Add-SimRecord $rec
        $script:SimExitCode = 1
        return ([ordered]@{ type = 'result'; subtype = 'success'; is_error = $true; result = $text; num_turns = 1; total_cost_usd = 0 } | ConvertTo-Json -Compress)
    }
    Add-SimRecord $rec
    $script:SimExitCode = 0
    $text = "SimulateLimit: заглушка claude, лимита нет.`nQUEUE: PARTIAL заглушка -SimulateLimit"
    return ([ordered]@{ type = 'result'; subtype = 'success'; is_error = $false; result = $text; num_turns = 1; total_cost_usd = 0 } | ConvertTo-Json -Compress)
}

function Invoke-Claude([int]$turns, [string]$effort, [string]$logFile) {
    $cliArgs = @('-p', '/next-task', '--dangerously-skip-permissions', '--output-format', 'json', '--max-turns', "$turns", '--effort', $effort)
    if ($Model) { $cliArgs += @('--model', $Model) }
    if ($Fast) {
        $fastFile = Join-Path $QueueDir 'fast-settings.json'
        Write-QueueText $fastFile '{ "fastMode": true }'
        $cliArgs += @('--settings', $fastFile)
    }
    # Переменная окружения важнее флага --effort, поэтому задаём и её — на время запуска (запасная модель:
    # CLAUDE_CODE_EFFORT_LEVEL=xhigh). Ultracode: workflow и фоновые агенты досчитываются после хода; без
    # CLAUDE_CODE_PRINT_BG_WAIT_CEILING_MS `claude -p` гасит их через 10 минут ожидания. Самообновление
    # Claude Code посреди сессии меняет поведение — выключено.
    $prevBgWait = $env:CLAUDE_CODE_PRINT_BG_WAIT_CEILING_MS
    $prevNoUpdate = $env:DISABLE_AUTOUPDATER
    $prevEffort = $env:CLAUDE_CODE_EFFORT_LEVEL
    $prevModel = $env:ANTHROPIC_MODEL
    $env:CLAUDE_CODE_PRINT_BG_WAIT_CEILING_MS = '7200000'
    $env:DISABLE_AUTOUPDATER = '1'
    if ($effort -eq 'ultracode') { $env:CLAUDE_CODE_EFFORT_LEVEL = $null } else { $env:CLAUDE_CODE_EFFORT_LEVEL = $effort }
    if ($Model) { $env:ANTHROPIC_MODEL = $Model }
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        if ($SimulateLimit) {
            $out = Invoke-ClaudeStub $cliArgs
            $code = $script:SimExitCode
        } else {
            $out = & claude @cliArgs 2>&1
            $code = $LASTEXITCODE
        }
    } finally {
        $ErrorActionPreference = $prev
        $env:CLAUDE_CODE_EFFORT_LEVEL = $prevEffort
        $env:ANTHROPIC_MODEL = $prevModel
        $env:CLAUDE_CODE_PRINT_BG_WAIT_CEILING_MS = $prevBgWait
        $env:DISABLE_AUTOUPDATER = $prevNoUpdate
    }
    $text = (@($out) | ForEach-Object { "$_" }) -join "`n"
    Write-QueueText $logFile $text

    $json = $null
    try { $json = $text | ConvertFrom-Json } catch {
        $line = ($text -split "`n" | Where-Object { $_.TrimStart().StartsWith('{') } | Select-Object -Last 1)
        if ($line) { try { $json = $line | ConvertFrom-Json } catch { } }
    }
    $result = if ($json -and $json.result) { [string]$json.result } else { $text }
    $verdict = 'NONE'; $reason = ''
    $m = [regex]::Matches($result, '(?m)QUEUE:\s*(DONE|PARTIAL|NEEDS_HUMAN|BLOCKED)[ \t]*(.*)$')
    if ($m.Count -gt 0) { $last = $m[$m.Count - 1]; $verdict = $last.Groups[1].Value; $reason = $last.Groups[2].Value.Trim() }
    $failed = ($code -ne 0 -or ($json -and $json.is_error))
    # Лимит использования — ждать долго; перегрузка сервера (529) проходит за минуты — ждать коротко.
    $limited = $failed -and (Test-Limited -Text $result -Json $json)
    $overloaded = $failed -and -not $limited -and ($result -match '(?i)\b529\b|overloaded|API Error: 5\d\d')

    return [pscustomobject]@{
        Code = $code; Verdict = $verdict; Reason = $reason; Limited = $limited; Overloaded = $overloaded
        Result = $result
        Cost = if ($json) { $json.total_cost_usd } else { $null }
        Turns = if ($json) { $json.num_turns } else { $null }
    }
}

function Invoke-SafetyCommit([string]$msg) {
    $ErrorActionPreference = 'Continue'
    if (-not (Test-Path (Join-Path $Root '.git'))) { return }
    $dirty = git status --porcelain 2>$null
    if ($dirty) {
        git add -A 2>$null | Out-Null
        git commit -q -m $msg 2>$null | Out-Null
        Say "Страховочный коммит: $msg" 'DarkGray'
    }
}

# ---------- настройки Claude Code ----------
# claude-setup/ (в Git) → .claude/ (копия, которой пользуется Claude Code).
function Install-ClaudeConfig {
    $src = Join-Path $Root 'claude-setup'
    if (-not (Test-Path $src)) { return }
    $dst = Join-Path $Root '.claude'
    # Относительный путь — от FullName самой папки: корень может прийти коротким именем 8.3 (%TEMP%),
    # а FullName детей — длинным; длина строки $src тогда не совпадает (образец здесь ошибался).
    $base = (Get-Item -LiteralPath $src).FullName.TrimEnd('\')
    Get-ChildItem -LiteralPath $base -Recurse -File | ForEach-Object {
        $rel = $_.FullName.Substring($base.Length).TrimStart('\', '/')
        $target = Join-Path $dst $rel
        if (-not (Test-Path $target) -or (Get-Item $target).LastWriteTime -lt $_.LastWriteTime) {
            New-Item -ItemType Directory -Force -Path (Split-Path $target) | Out-Null
            Copy-Item $_.FullName $target -Force
            Say "  .claude\$rel обновлён" 'DarkGray'
        }
    }
}

# .claude/settings.local.json не в Git (глобальный gitignore человека тоже его исключает), а свежему клону
# он обязателен: без env очереди сборка и тесты идут не в том окружении. Нет файла — создаётся по шаблону
# эталона 10.6; OTTISK_SOFFICE сюда дописывает проба LibreOffice (этап 0), шаблон его не ставит.
function Get-LocalSettingsText {
    $la = $env:LOCALAPPDATA.Replace('\', '\\')
    $tools = $ToolsRoot.Replace('\', '\\')
    return @"
{
  "env": {
    "BASH_DEFAULT_TIMEOUT_MS": "1800000",
    "BASH_MAX_TIMEOUT_MS": "7200000",
    "CLAUDE_CODE_PRINT_BG_WAIT_CEILING_MS": "7200000",
    "VCPKG_MAX_CONCURRENCY": "10",
    "VCPKG_ROOT": "$la\\vcpkg",
    "QT_ROOT": "C:\\Qt\\6.12.0\\msvc2022_64",
    "MUPDF_ROOT": "C:\\src\\mupdf",
    "QT_QPA_PLATFORM": "offscreen",
    "QT_QPA_FONTDIR": "C:/Windows/Fonts",
    "QT_FORCE_STDERR_LOGGING": "1",
    "PYTHONUTF8": "1",
    "PYTHONIOENCODING": "utf-8",
    "TESSDATA_PREFIX": "$tools\\tessdata",
    "OTTISK_TOOLS": "$tools",
    "OTTISK_CORPUS": "$la\\ottisk\\corpus",
    "OTTISK_PYTHON": "$tools\\venv-test\\Scripts\\python.exe",
    "OTTISK_INSTANCE": "test"
  },
  "permissions": {
    "allow": [
      "Read",
      "Edit",
      "Write",
      "Glob",
      "Grep",
      "Bash(cmake:*)",
      "Bash(ctest:*)",
      "Bash(ninja:*)",
      "Bash(py:*)",
      "Bash(git:*)",
      "Bash(clang-format:*)",
      "Bash(node .claude/skills/next-task/done.mjs:*)",
      "Bash($tools\\venv-test\\Scripts\\python.exe:*)",
      "Bash(C:/Tools/venv-test/Scripts/python.exe:*)",
      "Bash(/c/Tools/venv-test/Scripts/python.exe:*)",
      "Bash(\"`$OTTISK_PYTHON\":*)",
      "Bash(cmd /c tools\\vcenv.cmd:*)",
      "Bash(powershell -NoProfile -ExecutionPolicy Bypass -File tools/*)",
      "Bash(powershell -NoProfile -ExecutionPolicy Bypass -File tools\\*)",
      "PowerShell(cmake:*)",
      "PowerShell(ctest:*)",
      "PowerShell(ninja:*)",
      "PowerShell(py:*)",
      "PowerShell(git:*)",
      "PowerShell(clang-format:*)",
      "PowerShell(node .claude/skills/next-task/done.mjs:*)",
      "PowerShell($tools\\venv-test\\Scripts\\python.exe:*)",
      "PowerShell(& \"`$env:OTTISK_PYTHON\":*)",
      "PowerShell(cmd /c tools\\vcenv.cmd:*)",
      "PowerShell(tools\\*)",
      "PowerShell(.\\tools\\*)",
      "PowerShell(powershell -NoProfile -ExecutionPolicy Bypass -File tools\\*)"
    ]
  }
}
"@
}

function Ensure-LocalSettings {
    $f = Join-Path $Root '.claude\settings.local.json'
    if (-not (Test-Path -LiteralPath $f)) {
        New-Item -ItemType Directory -Force -Path (Split-Path $f) | Out-Null
        Write-QueueText $f (Get-LocalSettingsText)
        Say '  .claude\settings.local.json создан по шаблону (env очереди и permissions.allow)' 'Yellow'
        return
    }
    try {
        $j = Read-QueueText $f | ConvertFrom-Json
        $need = @('BASH_DEFAULT_TIMEOUT_MS', 'CLAUDE_CODE_PRINT_BG_WAIT_CEILING_MS', 'VCPKG_ROOT', 'QT_ROOT', 'MUPDF_ROOT',
                  'QT_QPA_PLATFORM', 'TESSDATA_PREFIX', 'OTTISK_PYTHON', 'OTTISK_CORPUS', 'OTTISK_INSTANCE')
        $miss = @($need | Where-Object { -not ($j.env.PSObject.Properties.Name -contains $_) })
        if ($miss.Count -gt 0) { Say ("  ! .claude\settings.local.json: нет env {0}" -f ($miss -join ', ')) 'Yellow' }
        else { Say '  [+] .claude\settings.local.json — env очереди на месте' 'Green' }
        if (-not ($j.env.PSObject.Properties.Name -contains 'OTTISK_SOFFICE')) {
            Say '  [!] OTTISK_SOFFICE не задан — LibreOffice нет; 7.3 пойдёт на FakeOfficeBackend (не BLOCKED)' 'DarkYellow'
        }
    } catch { Say "  [-] .claude\settings.local.json не читается как JSON: $($_.Exception.Message)" 'Red'; $script:SetupOk = $false }
}

# ---------- этап 0: проверка окружения ----------
function Add-Check([string]$name, [bool]$pass, [string]$detail, [switch]$WarnOnly) {
    if ($pass) { Say "  [+] $name — $detail" 'Green' }
    elseif ($WarnOnly) { Say "  [!] $name — $detail" 'Yellow' }
    else { Say "  [-] $name — $detail" 'Red'; $script:SetupOk = $false }
}

function Invoke-Native([scriptblock]$sb) {
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $o = & $sb 2>&1
        return [pscustomobject]@{ Code = $LASTEXITCODE; Text = ((@($o) | ForEach-Object { "$_" }) -join "`n") }
    } catch {
        return [pscustomobject]@{ Code = -1; Text = $_.Exception.Message }
    } finally { $ErrorActionPreference = $prev }
}

# Пункты этапа 0 НЕ отмечает (образец ставил [x] всем оставшимся — так страж старта обходился бы сам):
# "verified": ["0"] пишется только если все пункты этапа 0 уже [x] и окружение в порядке.
function Invoke-Setup {
    $script:SetupOk = $true
    Say 'Этап 0: проверка окружения. Скрипт ничего не ставит и пункты плана не отмечает.'
    Install-ClaudeConfig
    Ensure-LocalSettings
    Update-PathFromRegistry
    if (-not $env:OTTISK_PYTHON) {
        $env:OTTISK_PYTHON = $DefaultAcroBatyaPython
        Say "  OTTISK_PYTHON не задан в окружении — беру $DefaultAcroBatyaPython (так же в settings.local.json)" 'DarkGray'
    }

    # MSVC и clang-format — только через tools\vcenv.cmd (vcvars64 clang-format в PATH не кладёт).
    if (Test-Path -LiteralPath (Join-Path $Root 'tools\vcenv.cmd')) {
        $vc = Invoke-Native { cmd /d /c "call tools\vcenv.cmd && (cl 2>&1 & clang-format --version)" }
        $cl = [regex]::Match($vc.Text, '\b(19\.\d+\.\d+)')
        Add-Check 'cl (через tools\vcenv.cmd)' $cl.Success $(if ($cl.Success) { "версия $($cl.Groups[1].Value)" } else { 'cl не найден после vcvars64' })
        $cf = [regex]::Match($vc.Text, 'clang-format version (\S+)')
        Add-Check 'clang-format' ($cf.Success -and $cf.Groups[1].Value -eq '19.1.5') $(if ($cf.Success) { "версия $($cf.Groups[1].Value) (нужна 19.1.5)" } else { 'нет в PATH после tools\vcenv.cmd' })
    } else { Add-Check 'tools\vcenv.cmd' $false 'файла нет' }

    $qt = if ($env:QT_ROOT) { $env:QT_ROOT } else { 'C:\Qt\6.12.0\msvc2022_64' }
    $qtOk = (Test-Path "$qt\bin\Qt6Core.dll") -and (Test-Path "$qt\plugins\platforms\qoffscreen.dll") -and (Test-Path "$qt\plugins\platforms\qwindows.dll")
    Add-Check 'Qt 6.12.0' $qtOk "$qt (Qt6Core.dll, qoffscreen.dll, qwindows.dll)"

    $mu = if ($env:MUPDF_ROOT) { $env:MUPDF_ROOT } else { 'C:\src\mupdf' }
    $muLibs = @('platform\win32\x64\Debug\libmupdf.lib', 'platform\win32\x64\Release\libmupdf.lib', 'platform\win32\x64\Release\libresources.lib', 'platform\win32\x64\Release\mutool.exe')
    $muMiss = @($muLibs | Where-Object { -not (Test-Path (Join-Path $mu $_)) })
    Add-Check 'MuPDF 1.28.5' ($muMiss.Count -eq 0) $(if ($muMiss.Count -eq 0) { "$mu (Debug и Release libmupdf.lib, Release libresources.lib, mutool.exe)" } else { 'нет: ' + ($muMiss -join ', ') })

    $vcpkg = if ($env:VCPKG_ROOT) { $env:VCPKG_ROOT } else { Join-Path $env:LOCALAPPDATA 'vcpkg' }
    Add-Check 'vcpkg' (Test-Path (Join-Path $vcpkg 'vcpkg.exe')) "$vcpkg\vcpkg.exe"

    $toolFiles = @('qpdf\bin\qpdf.exe', 'pdfcpu\pdfcpu.exe', 'tessdata\rus.traineddata', 'tessdata\eng.traineddata', 'tessdata\deu.traineddata', 'venv-test\Scripts\python.exe')
    $toolMiss = @($toolFiles | Where-Object { -not (Test-Path (Join-Path $ToolsRoot $_)) })
    Add-Check $ToolsRoot ($toolMiss.Count -eq 0) $(if ($toolMiss.Count -eq 0) { 'qpdf, pdfcpu, tessdata (rus, eng, deu), venv-test' } else { 'нет: ' + ($toolMiss -join ', ') })
    Add-Check 'veraPDF' (Test-Path (Join-Path $ToolsRoot 'verapdf\verapdf.bat')) "$ToolsRoot\verapdf\verapdf.bat (нужен compat-тестам)" -WarnOnly
    $logs = Join-Path $ToolsRoot 'logs'
    if (-not (Test-Path $logs)) { New-Item -ItemType Directory -Force -Path $logs | Out-Null }
    Add-Check 'каталог логов' (Test-Path $logs) $logs

    $py = Invoke-Native { py -3.14 -c "import sys; print(sys.version.split()[0])" }
    Add-Check 'py -3.14' ($py.Code -eq 0 -and $py.Text -match '^3\.14') $(if ($py.Code -eq 0) { "Python $($py.Text.Trim())" } else { 'не запускается' })

    $op = $env:OTTISK_PYTHON
    if (Test-Path -LiteralPath $op) {
        $imp = Invoke-Native { & $op -X utf8 -I -c "import pypdfium2, pikepdf, pyhanko, skimage, openpyxl, pptx, odf" }
        Add-Check 'OTTISK_PYTHON' ($imp.Code -eq 0) $(if ($imp.Code -eq 0) { "$op (pypdfium2, pikepdf, pyhanko, skimage, openpyxl, pptx, odf)" } else { "$op — импорт не прошёл: $($imp.Text.Split("`n")[-1])" })
    } else { Add-Check 'OTTISK_PYTHON' $false "$op — файла нет" }

    foreach ($c in @('claude', 'git', 'node')) {
        if (Test-Cmd $c) {
            $v = Invoke-Native ([scriptblock]::Create("$c --version"))
            Add-Check $c ($v.Code -eq 0) ($v.Text.Split("`n")[0].Trim())
        } else { Add-Check $c $false 'не найден в PATH' }
    }
    $iscc = Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'
    Add-Check 'Inno Setup 6' (Test-Path $iscc) "$iscc (нужен tools\build.ps1 и рубежам)" -WarnOnly
    Add-Check 'Git-репозиторий' (Test-Path (Join-Path $Root '.git')) $Root -WarnOnly

    # Этап 0 — только проверка отметок; отмечает их человек вместе с Claude.
    $stages = Get-Plan
    $state = Get-State
    $gate = Get-Stage0Gate $stages $state.verified
    if ($gate.Pending.Count -gt 0) {
        Say ("Этап 0 не закрыт: не отмечено [x] пунктов — {0} из {1}:" -f $gate.Pending.Count, $gate.Total) 'Red'
        foreach ($it in $gate.Pending) { Write-Host ("    - [{0}] {1}" -f $it.State, (Get-ItemTitle $it.Text)) -ForegroundColor Yellow }
        Say '"verified": ["0"] не записан. Закончите пункты с человеком, отметьте их [x] в docs\plan.md и повторите -Setup.' 'Red'
        return $false
    }
    if (-not $script:SetupOk) { Say 'Окружение не готово — "verified": ["0"] не записан.' 'Red'; return $false }
    if ($gate.Verified) {
        Say 'Этап 0 уже проверен ("verified": ["0"] есть).' 'Green'
    } else {
        $verify = Join-Path $Root 'tools\verify.py'
        if (-not $SkipVerify -and (Test-Path $verify)) {
            Say 'tools\verify.py (на переднем плане, может занять до 30 минут)…'
            $prev = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
            & py -3.14 $verify
            $vcode = $LASTEXITCODE
            $ErrorActionPreference = $prev
            if ($vcode -ne 0) { Say "verify.py красный (код $vcode) — разберитесь до запуска очереди" 'Red'; return $false }
        } elseif (-not $SkipVerify) { Say '  [!] tools\verify.py нет — пропускаю' 'Yellow' }
        $state.verified = @($state.verified) + '0'
        Save-State $state
        Say 'Записано "verified": ["0"] в .queue\state.json.' 'Green'
    }
    Say 'Этап 0 готов. Проверьте: .\run-queue.ps1 -DryRun должен показать «Сессия 1.1».' 'Green'
    return $true
}

# ---------- главный цикл ----------
if ($Status) { Show-Status; exit 0 }
if ($Setup)  { if (Invoke-Setup) { exit 0 } else { exit 1 } }

# Страж старта — раньше всего остального, и для -DryRun тоже.
$gateStages = Get-Plan
$gateState = Get-State
$gate = Get-Stage0Gate $gateStages $gateState.verified
if (-not $gate.Ok) {
    Say 'Очередь не запускается: этап 0 не закрыт.' 'Red'
    Show-Stage0Gate $gate
    exit 1
}

Update-PathFromRegistry
if (-not $env:OTTISK_PYTHON) { $env:OTTISK_PYTHON = $DefaultAcroBatyaPython }
$needCmds = @('git', 'node', 'py')
if (-not $SimulateLimit) { $needCmds = @('claude') + $needCmds }
foreach ($c in $needCmds) { if (-not (Test-Cmd $c)) { Say "Не найден $c. Сначала выполните .\run-queue.ps1 -Setup" 'Red'; exit 1 } }
if (-not $DryRun -and -not $SimulateLimit -and -not (Test-Path (Join-Path $Root '.git'))) { Say 'Нет Git-репозитория (пункт этапа 0 «Git и снимок»).' 'Red'; exit 1 }
if (Test-Path $StopFile) { Remove-Item $StopFile -Force }
Install-ClaudeConfig
if (-not (Test-Path (Join-Path $Root '.claude\skills\next-task\SKILL.md'))) { Say 'Нет навыка .claude\skills\next-task — проверьте папку claude-setup' 'Red'; exit 1 }
if ($SimulateLimit -and (Test-Path $SimLog)) { Remove-Item $SimLog -Force }

Say 'Очередь запущена. Остановить после текущего задания: создать файл .queue\STOP'
if ($SimulateLimit) { Say 'SimulateLimit: claude подменён заглушкой, ожидания не спят.' 'DarkYellow' }
Show-Status
$count = 0
$overloadStreak = 0
$onFallback = $false
$primaryResetAt = $null

while ($true) {
    if (Test-Path $StopFile) { Say 'Найден .queue\STOP — остановка.' 'Yellow'; Remove-Item $StopFile -Force; break }
    if ($MaxTasks -gt 0 -and $count -ge $MaxTasks) { Say "Выполнено заданий: $count (лимит -MaxTasks)." 'Yellow'; break }

    # Лимит основной модели сброшен раньше, чем кончился у запасной, — вернуться к основной.
    if ($onFallback -and $primaryResetAt -and (Get-Date) -ge $primaryResetAt) {
        Say "Сброс лимита $PrimaryModel прошёл — возвращаюсь к основной модели." 'Yellow'
        $onFallback = $false; $Model = $PrimaryModel; $primaryResetAt = $null
    }

    Update-PathFromRegistry
    $stages = Get-Plan
    $state = Get-State
    $u = Get-NextUnit $stages $state
    if (-not $u) { Say 'План выполнен полностью (блоки с одними [~] ждут человека).' 'Green'; Show-Status; break }

    $shortTitle = if ($u.Kind -eq 'task') { "Сессия $($u.Block.Id) · $($u.Block.Title) · пунктов $(Get-Pending $u.Block)" }
                  else { "Рубеж $($u.Block.Id) · $($u.Block.Title)" }
    Say ("Этап {0} · {1}" -f $u.Stage.Num, $shortTitle) 'White'
    if ($DryRun) { Write-CurrentTask $u $stages; Say "DryRun: задание записано в $CurrentFile" 'Yellow'; break }

    Write-CurrentTask $u $stages
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $logFile = Join-Path $LogDir ("$stamp-{0}-s{1}.json" -f $u.Kind, $u.Stage.Num)
    # Ходы — из заголовка сессии, если человек не задал своё в командной строке. Усилие — из флагов
    # (по умолчанию xhigh, решение человека 23.10.2026); заголовок «усилие …» берётся только с -UsePlanEffort.
    $forcedTurns  = $PSBoundParameters.ContainsKey($(if ($u.Kind -eq 'verify') { 'VerifyMaxTurns' } else { 'MaxTurns' }))
    $forcedEffort = $PSBoundParameters.ContainsKey($(if ($u.Kind -eq 'verify') { 'VerifyEffort' } else { 'Effort' }))
    $turns = if ($u.Kind -eq 'verify') { $VerifyMaxTurns } else { $MaxTurns }
    $effortNow = if ($u.Kind -eq 'verify') { $VerifyEffort } else { $Effort }
    if (-not $forcedTurns -and $u.Block.Turns -gt 0) { $turns = $u.Block.Turns }
    if ($UsePlanEffort -and -not $forcedEffort -and $u.Block.Effort) {
        if ($EffortLevels -contains $u.Block.Effort) { $effortNow = $u.Block.Effort }
        else { Say "  В плане у сессии неизвестное усилие «$($u.Block.Effort)» — беру $effortNow" 'Yellow' }
    }
    $effortCall = if ($onFallback) { $FallbackEffort } else { $effortNow }
    $started = Get-Date

    Say ("  модель: {0} · усилие: {1} · ходов: {2}{3}{4}" -f $Model, $effortCall, $turns,
        $(if ($onFallback) { ' · запасная' } else { '' }), $(if ($Fast) { ' · быстрый режим' } else { '' })) 'DarkGray'
    $r = Invoke-Claude $turns $effortCall $logFile
    $mins = [math]::Round(((Get-Date) - $started).TotalMinutes, 1)

    if ($SimulateLimit -and -not $r.Limited) {
        Say "SimulateLimit: вызов $SimCalls прошёл без лимита ($Model, усилие $effortCall) — сценарий пройден, выхожу." 'Green'
        exit 0
    }

    # Ошибка API в конце сессии, которая уже отметила пункты, — не повод ждать: сделанное считается как обычно.
    $progressed = $false
    if ($u.Kind -eq 'task' -and ($r.Limited -or $r.Overloaded)) {
        $now = Find-Block (Get-Plan) $u.Stage.Num $u.Block.Id
        $progressed = ($null -ne $now) -and ((Get-Pending $now) -lt (Get-Pending $u.Block))
    }
    if ($r.Overloaded -and $r.Verdict -eq 'NONE' -and -not $progressed) {
        $overloadStreak++
        $wait = [math]::Min(15, [math]::Pow(2, $overloadStreak))
        Say "Сервер перегружен (529). Жду $wait мин и повторяю…" 'Yellow'
        Log-Summary "OVERLOAD $($u.Key) · жду $wait мин"
        Start-Sleep -Seconds ($wait * 60)
        continue
    }
    if ($r.Limited -and $r.Verdict -eq 'NONE' -and -not $progressed) {
        $resetWait = Get-ResetWaitSeconds -Text $r.Result
        # (1) лимит у основной → та же сессия на запасной с её усилием.
        if (-not $onFallback -and $FallbackModel -and $FallbackModel -ne $PrimaryModel) {
            $primaryResetAt = if ($resetWait) { (Get-Date).AddSeconds($resetWait) } else { $null }
            Say "Лимит у $Model — перехожу на $FallbackModel (усилие $FallbackEffort) и повторяю ту же сессию." 'Yellow'
            Log-Summary ("LIMIT    $($u.Key) · $Model -> $FallbackModel/$FallbackEffort{0}" -f $(if ($primaryResetAt) { ' · основная сбросится ' + $primaryResetAt.ToString('dd.MM HH:mm') } else { '' }))
            $onFallback = $true
            $Model = $FallbackModel
            continue
        }
        # (2) лимит и у запасной (или запасной нет) → ждать сброса, затем основная модель на той же сессии.
        if ($resetWait) {
            $until = (Get-Date).AddSeconds($resetWait)
            if ($primaryResetAt -and $primaryResetAt -gt (Get-Date) -and $primaryResetAt -lt $until) { $until = $primaryResetAt }
            Say ("Лимит — жду сброса до {0}" -f $until.ToString('dd.MM HH:mm')) 'Yellow'
            Log-Summary ("LIMIT    $($u.Key) · жду до {0}" -f $until.ToString('dd.MM HH:mm'))
        } else {
            $until = (Get-Date).AddMinutes($LimitWaitMinutes)
            if ($primaryResetAt -and $primaryResetAt -gt (Get-Date) -and $primaryResetAt -lt $until) { $until = $primaryResetAt }
            Say ("Похоже на лимит, время сброса не разобрано — жду до {0} (-LimitWaitMinutes {1})" -f $until.ToString('dd.MM HH:mm'), $LimitWaitMinutes) 'Yellow'
            Log-Summary ("LIMIT    $($u.Key) · не разобрано, жду до {0}" -f $until.ToString('dd.MM HH:mm'))
        }
        $state.limitWaits = [int]$state.limitWaits + 1
        Save-State $state
        Wait-Until $until
        $onFallback = $false
        $Model = $PrimaryModel
        $primaryResetAt = $null
        Say "Возвращаюсь к $PrimaryModel и повторяю ту же сессию." 'Yellow'
        continue
    }

    # Итог: считаем не вердикт, а отмеченные пункты — сделанное не пропадает.
    $ok = $false
    $advanced = 0
    $left = 0
    if ($u.Kind -eq 'task') {
        $before = Get-Pending $u.Block
        if ($before -eq 1 -and $r.Verdict -in @('DONE', 'NEEDS_HUMAN')) {
            $only = @($u.Block.Items | Where-Object { $_.State -eq ' ' })[0]
            $mark = if ($r.Verdict -eq 'NEEDS_HUMAN') { '~' } else { 'x' }
            [void](Set-PlanItemMark (Get-PlanPath) $u.Stage.Num $only.Text $mark)
        }
        $after = Find-Block (Get-Plan) $u.Stage.Num $u.Block.Id
        $left = if ($after) { Get-Pending $after } else { 0 }
        $advanced = $before - $left
        $ok = ($left -eq 0)
    } else {
        $ok = ($r.Verdict -in @('DONE', 'NEEDS_HUMAN'))
        if ($ok) { $state.verified = @($state.verified) + $u.Block.Id }
    }

    $costText = if ($null -ne $r.Cost) { ' · $' + ([math]::Round([double]$r.Cost, 2)) } else { '' }
    $progressText = if ($u.Kind -eq 'task') { " · пунктов $advanced, осталось $left" } else { '' }
    Log-Summary ("{0,-12} {1} · {2}/{3} · {4} мин · ходов {5}{6}{7} · {8}" -f $r.Verdict, $u.Key, $Model, $effortCall, $mins, $r.Turns, $costText, $progressText, $r.Reason)

    if ($r.Verdict -eq 'BLOCKED') {
        Invoke-SafetyCommit "этап $($u.Stage.Num): остановка очереди"
        Say "Claude остановил очередь: $($r.Reason)" 'Red'
        Say "Подробности: $logFile и docs\progress.md" 'Red'
        Save-State $state
        exit 2
    }

    # Часть пунктов сделана: это не неудача — следующая сессия возьмёт остаток.
    if (-not $ok -and $advanced -gt 0) {
        $state.lastKey = ''; $state.failCount = 0; $overloadStreak = 0
        Save-State $state
        Invoke-SafetyCommit "этап $($u.Stage.Num): сессия $($u.Block.Id) — часть пунктов"
        Say ("  Сделано пунктов: {0}, осталось {1} — продолжаю новой сессией{2}" -f $advanced, $left, $costText) 'Yellow'
        $count++
        continue
    }

    if ($ok) {
        $state.lastKey = ''; $state.failCount = 0; $overloadStreak = 0
        Save-State $state
        $label = if ($u.Kind -eq 'task') { "сессия $($u.Block.Id) · $($u.Block.Title)" } else { "рубеж $($u.Block.Id)" }
        Invoke-SafetyCommit "этап $($u.Stage.Num): $label"
        $color = if ($r.Verdict -eq 'DONE') { 'Green' } else { 'Yellow' }
        Say ("  {0} за {1} мин{2}" -f $r.Verdict, $mins, $costText) $color
        $count++
        continue
    }

    # Неудача — повтор в новой сессии
    if ($state.lastKey -eq $u.Key) { $state.failCount++ } else { $state.lastKey = $u.Key; $state.failCount = 1 }
    Save-State $state
    Invoke-SafetyCommit "этап $($u.Stage.Num): незавершённая попытка"
    if ($state.failCount -gt $Retries) {
        Say "Задание не удалось после $($state.failCount) попыток. Очередь остановлена." 'Red'
        Say "Лог: $logFile (код $($r.Code), итог $($r.Verdict))" 'Red'
        exit 3
    }
    Say "  Не завершено (код $($r.Code), итог $($r.Verdict)). Попытка $($state.failCount + 1) из $($Retries + 1)…" 'Yellow'
}
