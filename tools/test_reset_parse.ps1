<#
.SYNOPSIS
    Страж очереди: признак лимита, разбор времени сброса, порядок моделей при лимите и страж старта этапа 0.

.DESCRIPTION
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\test_reset_parse.ps1 [-KeepTemp]

    1. Шесть образцов эталона 10.6 («resets 9:30am», «resets 14:05», «resets at 02:10», «resets in 25 min»,
       «resets in 2 hours», «weekly limit · resets Oct 25, 9am») — Test-Limited даёт «лимит» и
       Get-ResetWaitSeconds — секунды до сброса плюс 120 с (±60) при фиксированном «сейчас» 23.10.2026 12:00.
       Функции берутся dot-source'ом из tools/queue-lib.ps1 — того же файла, что подключает run-queue.ps1.
    2. Сценарий run-queue.ps1 -SimulateLimit на временной копии репозитория (docs/plan.md с этапом 0 [x],
       .queue/state.json с "verified": ["0"]): вызовы claude по порядку — claude-opus-5-5 xhigh →
       claude-fable-5-1 xhigh (CLAUDE_CODE_EFFORT_LEVEL=xhigh) → ожидание до сброса + 120 с → снова
       claude-opus-5-5 xhigh на той же сессии 1.1.
    3. -DryRun на той же копии печатает «Сессия 1.1», -Status — этап 0 «проверен».
    4. Отказ старта (и -DryRun) на копии с одним [ ] в этапе 0 и на копии без "verified": ["0"].
    5. Отказ -Setup писать "verified": ["0"] на копии с одним [ ] в этапе 0 (план при этом не меняется).
    6. -Setup -SkipVerify на копии со всеми [x] пишет "verified": ["0"], план не трогает, затем -DryRun даёт 1.1
       (окружение машины не готово — пропуск этой проверки, verified при этом не пишется).
    Код 0 — всё зелёное, 1 — есть расхождения (печатаются). Запускает tools/verify.py.
#>
[CmdletBinding()]
param([switch]$KeepTemp)

$ErrorActionPreference = 'Stop'
$Utf8 = New-Object System.Text.UTF8Encoding $false
try { [Console]::OutputEncoding = $Utf8 } catch { }
$OutputEncoding = $Utf8

$Repo = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'queue-lib.ps1')
$RunQueue = Join-Path $Repo 'run-queue.ps1'
$Fails = New-Object System.Collections.ArrayList
$Started = Get-Date

function Check([string]$name, [bool]$ok, [string]$detail = '') {
    if ($ok) { Write-Host "  ok    $name" -ForegroundColor Green }
    else {
        Write-Host "  FAIL  $name$(if ($detail) { " — $detail" })" -ForegroundColor Red
        [void]$Fails.Add($name)
    }
    return $ok
}

# ---------- 1. образцы ----------
Write-Host 'Образцы эталона 10.6 (сейчас = 23.10.2026 12:00):' -ForegroundColor Cyan
$Now = New-Object DateTime (2026, 10, 23, 12, 0, 0)
$Samples = @(
    @{ Text = 'resets 9:30am';                       Wait = 77520;  Why = 'сегодня 9:30 прошло — завтра, 21 ч 30 мин + 120 с' },
    @{ Text = 'resets 14:05';                        Wait = 7620;   Why = '2 ч 05 мин + 120 с' },
    @{ Text = 'resets at 02:10';                     Wait = 51120;  Why = 'завтра 02:10, 14 ч 10 мин + 120 с' },
    @{ Text = 'resets in 25 min';                    Wait = 1620;   Why = '25 мин + 120 с' },
    @{ Text = 'resets in 2 hours';                   Wait = 7320;   Why = '2 ч + 120 с' },
    @{ Text = 'weekly limit · resets Oct 25, 9am';   Wait = 162120; Why = '25.10 9:00, 45 ч + 120 с' }
)
$limitedOk = 0; $waitOk = 0
foreach ($s in $Samples) {
    $lim = Test-Limited -Text $s.Text
    if (Check "Test-Limited «$($s.Text)»" $lim 'не признан лимитом') { $limitedOk++ }
    $w = Get-ResetWaitSeconds -Text $s.Text -Now $Now
    $good = ($null -ne $w) -and ([math]::Abs([int]$w - $s.Wait) -le 60)
    if (Check "Get-ResetWaitSeconds «$($s.Text)» = $w с ($($s.Why))" $good "ждали $($s.Wait) ± 60") { $waitOk++ }
}

Write-Host 'Дополнительно:' -ForegroundColor Cyan
[void](Check 'Test-Limited: «You''ve hit your session limit · resets 14:05» без 429 в JSON' (Test-Limited -Text "You've hit your session limit · resets 14:05 (Europe/Berlin)"))
[void](Check 'Test-Limited: api_error_status 429 без текста' (Test-Limited -Text 'API Error' -Json ([pscustomobject]@{ api_error_status = 429 })))
[void](Check 'Test-Limited: обычный итог сессии — не лимит' (-not (Test-Limited -Text "Пункты сделаны, verify.py зелёный.`nQUEUE: DONE")))
[void](Check 'Test-Limited: «QUEUE: PARTIAL 2 из 3» — не лимит' (-not (Test-Limited -Text 'QUEUE: PARTIAL 2 из 3, остался пункт про печать')))
[void](Check 'Get-ResetWaitSeconds: «resets 5» без минут и am/pm — не разобрано' ($null -eq (Get-ResetWaitSeconds -Text 'resets 5' -Now $Now)))
[void](Check 'Get-ResetWaitSeconds: текст без времени — не разобрано' ($null -eq (Get-ResetWaitSeconds -Text 'usage limit reached' -Now $Now)))
[void](Check 'Get-ResetWaitSeconds: сброс 10 минут назад — только запас 120 с' ((Get-ResetWaitSeconds -Text 'resets 11:50' -Now $Now) -eq 120))
[void](Check 'Get-ResetWaitSeconds: «resets in 1 hour 30 min» = 5520' ((Get-ResetWaitSeconds -Text 'resets in 1 hour 30 min' -Now $Now) -eq 5520))
[void](Check 'Get-ResetWaitSeconds: «resets 12:10pm» = 720' ((Get-ResetWaitSeconds -Text 'resets 12:10pm' -Now $Now) -eq 720))
$epochIn1h = (New-Object DateTimeOffset ($Now.AddHours(1))).ToUnixTimeSeconds()
[void](Check 'Test-Limited и Get-ResetWaitSeconds: «Claude AI usage limit reached|<Unix через 1 ч>» = 3720' (
    (Test-Limited -Text "Claude AI usage limit reached|$epochIn1h") -and (Get-ResetWaitSeconds -Text "Claude AI usage limit reached|$epochIn1h" -Now $Now) -eq 3720))
$hb = Read-BlockHeader '### Сессия 2.2b · AnnotSelectTool, текст и ссылки · усилие xhigh · ходов 300'
[void](Check 'Read-BlockHeader: суффикс-буква 2.2b — обычный id' ($hb -and $hb.Id -eq '2.2b' -and $hb.Turns -eq 300 -and $hb.Effort -eq 'xhigh' -and $hb.Kind -eq 'task'))
$realPlan = Read-QueuePlan (Join-Path $Repo 'docs\plan.md')
$blocks = @($realPlan | Where-Object { $_.Num -gt 0 } | ForEach-Object { $_.Blocks })
$sessions = @($blocks | Where-Object { $_.Kind -eq 'task' })
$checks = @($blocks | Where-Object { $_.Kind -eq 'verify' })
$keys = @($sessions | ForEach-Object { $_.Id })
$sfx = @('2.2b', '2.4b', '2.5b', '8.1b' | Where-Object { $keys -contains $_ })
$rIds = @($checks | ForEach-Object { $_.Id })
$rOk = @('Р1', 'Р2', 'Р3', 'Р4', 'Р5' | Where-Object { $rIds -contains $_ }).Count -eq 5
$empty = @($sessions | Where-Object { $_.Items.Count -eq 0 } | ForEach-Object { $_.Id })
# Не «ровно 48»: человек может вставить сессию (эталон 12.1 — 7.7 «PubSec»), страж от этого не краснеет.
[void](Check "docs/plan.md: сессий $($sessions.Count) (≥ 48), рубежи Р1–Р5, id уникальны, суффиксы 2.2b/2.4b/2.5b/8.1b на месте, у каждой сессии есть пункты" (
    $sessions.Count -ge 48 -and $rOk -and (@($keys | Select-Object -Unique).Count -eq $keys.Count) -and $sfx.Count -eq 4 -and $empty.Count -eq 0) "рубежи: $($rIds -join ', '); сессии без пунктов: $($empty -join ', ')")

# ---------- копии репозитория ----------
$TmpBase = Join-Path $env:TEMP ('ottisk-queue-test-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Force -Path $TmpBase | Out-Null

# Отметки этапа 0 в тексте плана: 'all' — все [x]; 'one-open' — все [x], кроме последнего пункта.
function Set-Stage0Marks([string]$text, [string]$mode) {
    $eol = if ($text.Contains("`r`n")) { "`r`n" } else { "`n" }
    $lines = $text -split "`r?`n"
    $idx = @()
    $in = $false
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '^## Этап (\d+)\.') { $in = ([int]$Matches[1] -eq 0); continue }
        if ($lines[$i] -match '^## ') { $in = $false; continue }
        if ($in -and $lines[$i] -match '^- \[[ xX~]\] ') { $lines[$i] = '- [x] ' + $lines[$i].Substring(6); $idx += $i }
    }
    if ($mode -eq 'one-open' -and $idx.Count -gt 0) { $j = $idx[-1]; $lines[$j] = '- [ ] ' + $lines[$j].Substring(6) }
    return ($lines -join $eol)
}

function New-RepoCopy([string]$name, [string]$mode, [bool]$verified) {
    $root = Join-Path $TmpBase $name
    New-Item -ItemType Directory -Force -Path (Join-Path $root 'docs'), (Join-Path $root '.queue') | Out-Null
    Write-QueueText (Join-Path $root 'docs\plan.md') (Set-Stage0Marks (Read-QueueText (Join-Path $Repo 'docs\plan.md')) $mode)
    $state = [pscustomobject]@{ verified = @(); lastKey = ''; failCount = 0; limitWaits = 0 }
    if ($verified) { $state.verified = @('0') }
    Save-QueueState (Join-Path $root '.queue\state.json') $state
    Copy-Item -Recurse -Path (Join-Path $Repo 'claude-setup') -Destination (Join-Path $root 'claude-setup')
    return $root
}

function Invoke-RunQueue([string]$root, [string[]]$more) {
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $out = & powershell -NoProfile -ExecutionPolicy Bypass -File $RunQueue -Root $root @more 2>&1
        $code = $LASTEXITCODE
    } finally { $ErrorActionPreference = $prev }
    $text = (@($out) | ForEach-Object { "$_" }) -join "`n"
    if ($VerbosePreference -eq 'Continue') { Write-Host $text -ForegroundColor DarkGray }
    return [pscustomobject]@{ Code = $code; Text = $text }
}

try {
    # ---------- 2. -SimulateLimit ----------
    Write-Host 'Сценарий run-queue.ps1 -SimulateLimit (этап 0 [x], verified 0):' -ForegroundColor Cyan
    $ready = New-RepoCopy 'ready' 'all' $true
    $planBefore = Read-QueueText (Join-Path $ready 'docs\plan.md')
    $r = Invoke-RunQueue $ready @('-SimulateLimit')
    [void](Check "код выхода 0 (получили $($r.Code))" ($r.Code -eq 0) (($r.Text -split "`n" | Select-Object -Last 5) -join ' | '))
    $simLog = Join-Path $ready '.queue\simulate-limit.jsonl'
    $events = @()
    if (Test-Path $simLog) { $events = @(Get-Content -LiteralPath $simLog -Encoding UTF8 | Where-Object { $_.Trim() } | ForEach-Object { $_ | ConvertFrom-Json }) }
    $order = ($events | ForEach-Object { if ($_.event -eq 'call') { "call$($_.n)" } else { $_.event } }) -join ' → '
    [void](Check "порядок: call1 → call2 → wait → call3 (получили: $order)" ($order -eq 'call1 → call2 → wait → call3'))
    $calls = @($events | Where-Object { $_.event -eq 'call' })
    $wait = @($events | Where-Object { $_.event -eq 'wait' }) | Select-Object -First 1
    if ($calls.Count -ge 3) {
        $c1 = $calls[0]; $c2 = $calls[1]; $c3 = $calls[2]
        [void](Check "1-й вызов: основная claude-opus-5-5 --effort xhigh, CLAUDE_CODE_EFFORT_LEVEL=xhigh (получили $($c1.model) $($c1.effort), '$($c1.env_effort)')" ($c1.model -eq 'claude-opus-5-5' -and $c1.effort -eq 'xhigh' -and $c1.env_effort -eq 'xhigh'))
        [void](Check "2-й вызов: запасная claude-fable-5-1 --effort xhigh, CLAUDE_CODE_EFFORT_LEVEL=xhigh (получили $($c2.model) $($c2.effort), '$($c2.env_effort)')" ($c2.model -eq 'claude-fable-5-1' -and $c2.effort -eq 'xhigh' -and $c2.env_effort -eq 'xhigh'))
        [void](Check "3-й вызов: снова claude-opus-5-5 --effort xhigh (получили $($c3.model) $($c3.effort), '$($c3.env_effort)')" ($c3.model -eq 'claude-opus-5-5' -and $c3.effort -eq 'xhigh' -and $c3.env_effort -eq 'xhigh'))
        $envOk = @($calls | Where-Object { $_.bg_wait -eq '7200000' -and $_.no_autoupdate -eq '1' -and $_.args -match '^-p /next-task --dangerously-skip-permissions --output-format json --max-turns 300 ' }).Count -eq $calls.Count
        [void](Check 'окружение вызова: CLAUDE_CODE_PRINT_BG_WAIT_CEILING_MS=7200000, DISABLE_AUTOUPDATER=1, -p /next-task, ходов 300 из заголовка' $envOk)
        if ($wait -and $c2.reset_at) {
            $delta = ([datetime]$wait.until - [datetime]$c2.reset_at).TotalSeconds
            [void](Check ("ожидание до сброса запасной + 120 с (разница {0:N0} с)" -f $delta) ([math]::Abs($delta - 120) -le 60))
        } else { [void](Check 'ожидание до сброса записано' $false 'нет события wait или reset_at') }
    }
    $sum = Join-Path $ready '.queue\log\summary.log'
    $sumText = if (Test-Path $sum) { Read-QueueText $sum } else { '' }
    [void](Check 'та же сессия 1.1 на всех вызовах (summary.log: LIMIT task|1|1.1 ×2)' (([regex]::Matches($sumText, 'LIMIT\s+task\|1\|1\.1')).Count -eq 2))
    [void](Check 'задание в .queue\current.md — «Сессия 1.1»' ((Read-QueueText (Join-Path $ready '.queue\current.md')) -match '^# Сессия 1\.1 ·'))
    [void](Check 'план не изменён' ((Read-QueueText (Join-Path $ready 'docs\plan.md')) -eq $planBefore))
    $st = Read-QueueState (Join-Path $ready '.queue\state.json')
    [void](Check "state.json: verified 0 на месте, failCount 0, limitWaits 1 (получили $($st.failCount), $($st.limitWaits))" (@($st.verified) -contains '0' -and $st.failCount -eq 0 -and $st.limitWaits -eq 1))

    # ---------- 3. -DryRun ----------
    Write-Host 'run-queue.ps1 -DryRun (этап 0 [x], verified 0):' -ForegroundColor Cyan
    $r = Invoke-RunQueue $ready @('-DryRun')
    [void](Check "код 0 и печатает «Сессия 1.1» (код $($r.Code))" ($r.Code -eq 0 -and $r.Text -match 'Этап 1 · Сессия 1\.1 · '))
    $r = Invoke-RunQueue $ready @('-Status')
    [void](Check "-Status: этап 0 «проверен», следующее — сессия 1.1 (код $($r.Code))" (
        $r.Code -eq 0 -and $r.Text -match '(?m)Этап\s+0\..*проверен' -and $r.Text -match 'Следующее: этап 1 · Сессия 1\.1 · '))

    # ---------- 4. отказ старта ----------
    Write-Host 'Отказ старта:' -ForegroundColor Cyan
    $open = New-RepoCopy 'one-open' 'one-open' $true
    $openPlan = Read-QueuePlan (Join-Path $open 'docs\plan.md')
    $openStage0 = @($openPlan | Where-Object { $_.Num -eq 0 })[0]
    $openItem = @($openStage0.Items | Where-Object { $_.State -eq ' ' })[0]
    $openTitle = Get-ItemTitle $openItem.Text
    $r = Invoke-RunQueue $open @('-SimulateLimit')
    [void](Check "один [ ] в этапе 0: старт отказывает (код $($r.Code)), печатает пункт «$openTitle»" ($r.Code -eq 1 -and $r.Text -match 'Этап 0 не закрыт' -and $r.Text.Contains($openTitle)))
    [void](Check 'один [ ] в этапе 0: claude не вызывался' (-not (Test-Path (Join-Path $open '.queue\simulate-limit.jsonl'))))
    $r = Invoke-RunQueue $open @('-DryRun')
    [void](Check "один [ ] в этапе 0: -DryRun тоже отказывает (код $($r.Code))" ($r.Code -eq 1 -and $r.Text -match 'Этап 0 не закрыт' -and $r.Text -notmatch 'Сессия 1\.1 · '))
    $noVer = New-RepoCopy 'no-verified' 'all' $false
    $r = Invoke-RunQueue $noVer @('-SimulateLimit')
    [void](Check "все [x], но нет verified 0: старт отказывает (код $($r.Code)) и велит -Setup" ($r.Code -eq 1 -and $r.Text -match 'нет "verified": \["0"\]' -and $r.Text -match '-Setup'))

    # ---------- 5. отказ -Setup ----------
    Write-Host 'Отказ -Setup писать verified:' -ForegroundColor Cyan
    $setupCopy = New-RepoCopy 'setup-open' 'one-open' $false
    $planBefore = Read-QueueText (Join-Path $setupCopy 'docs\plan.md')
    $r = Invoke-RunQueue $setupCopy @('-Setup', '-SkipVerify')
    $st = Read-QueueState (Join-Path $setupCopy '.queue\state.json')
    [void](Check "-Setup: код 1 (получили $($r.Code)), печатает оставшийся пункт «$openTitle»" ($r.Code -eq 1 -and $r.Text -match 'Этап 0 не закрыт' -and $r.Text.Contains($openTitle)))
    [void](Check '-Setup: "verified": ["0"] не записан' (-not (@($st.verified) -contains '0')))
    [void](Check '-Setup: пункты этапа 0 не отмечены (план не изменён)' ((Read-QueueText (Join-Path $setupCopy 'docs\plan.md')) -eq $planBefore))

    # ---------- 6. -Setup при всех [x] пишет verified ----------
    # Обратная сторона отказа: страж не должен закрывать очередь навсегда. Проверки окружения настоящие
    # (cl через tools\vcenv.cmd, Qt, MuPDF, C:\Tools, py, claude); если машина не готова — verified не пишется,
    # и это пропуск, а не красный (среду проверяет сам -Setup и verify.py).
    Write-Host '-Setup при всех [x] (без verify.py):' -ForegroundColor Cyan
    $allCopy = New-RepoCopy 'setup-all' 'all' $false
    New-Item -ItemType Directory -Force -Path (Join-Path $allCopy 'tools') | Out-Null
    Copy-Item -LiteralPath (Join-Path $Repo 'tools\vcenv.cmd') -Destination (Join-Path $allCopy 'tools\vcenv.cmd')
    $planBefore = Read-QueueText (Join-Path $allCopy 'docs\plan.md')
    $r = Invoke-RunQueue $allCopy @('-Setup', '-SkipVerify')
    $st = Read-QueueState (Join-Path $allCopy '.queue\state.json')
    $verifiedNow = @($st.verified) -contains '0'
    if ($r.Code -ne 0 -and $r.Text -match 'Окружение не готово') {
        $bad = @($r.Text -split "`n" | Where-Object { $_ -match '\[-\]' } | ForEach-Object { $_.Trim() })
        Write-Host ("  skip  окружение машины не готово — verified не проверяю: {0}" -f ($bad -join '; ')) -ForegroundColor Yellow
        [void](Check '-Setup при неготовом окружении: "verified": ["0"] не записан' (-not $verifiedNow))
    } else {
        [void](Check "-Setup: код 0 и записан ""verified"": [""0""] (код $($r.Code))" ($r.Code -eq 0 -and $verifiedNow) (($r.Text -split "`n" | Select-Object -Last 3) -join ' | '))
        [void](Check '-Setup: план не изменён' ((Read-QueueText (Join-Path $allCopy 'docs\plan.md')) -eq $planBefore))
        $r = Invoke-RunQueue $allCopy @('-DryRun')
        [void](Check "после -Setup: -DryRun печатает «Сессия 1.1» (код $($r.Code))" ($r.Code -eq 0 -and $r.Text -match 'Этап 1 · Сессия 1\.1 · '))
    }
} finally {
    if ($KeepTemp) { Write-Host "Копии оставлены: $TmpBase" -ForegroundColor DarkGray }
    else { Remove-Item -Recurse -Force -LiteralPath $TmpBase -ErrorAction SilentlyContinue }
}

$secs = [math]::Round(((Get-Date) - $Started).TotalSeconds, 1)
$summary = "test_reset_parse: Test-Limited $limitedOk/$($Samples.Count), Get-ResetWaitSeconds $waitOk/$($Samples.Count), расхождений: $($Fails.Count) · $secs с"
if ($Fails.Count -eq 0) { Write-Host $summary -ForegroundColor Green; exit 0 }
Write-Host $summary -ForegroundColor Red
foreach ($f in $Fails) { Write-Host "  - $f" -ForegroundColor Red }
exit 1
