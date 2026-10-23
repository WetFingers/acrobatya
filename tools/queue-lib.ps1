# Общие функции очереди «АкроБати»: разбор docs/plan.md, состояние .queue/state.json,
# признак лимита и разбор времени его сброса.
#
# Подключается dot-source'ом:  . (Join-Path <корень> 'tools\queue-lib.ps1')
#   - run-queue.ps1 — рабочий код очереди;
#   - tools/test_reset_parse.ps1 — тест проверяет эти же функции, а не их копию.
# Файл без побочных эффектов: ничего не печатает, не меняет $ErrorActionPreference и окружение.
# UTF-8 с BOM: Windows PowerShell 5.1 без BOM читает кириллицу как cp1252 и падает на разборе.

# ---------- файлы ----------
function Read-QueueText([string]$Path) { [IO.File]::ReadAllText($Path, [Text.Encoding]::UTF8) }
function Write-QueueText([string]$Path, [string]$Text) {
    [IO.File]::WriteAllText($Path, $Text, (New-Object System.Text.UTF8Encoding $false))
}

# ---------- признак лимита ----------
# Ответ `claude -p` считается лимитом использования, если в JSON api_error_status = 429 или в тексте есть
# признак. Образец «Мастерской» ловил только «usage limit|usage credits|out of usage|rate limit|429|
# limit reached|resets at|switch to another model» — «You've hit your session limit · resets 14:05» без 429
# в JSON он считал обычной неудачей, и после -Retries очередь вставала с кодом 3 (эталон 10.6, правка 0).
function Test-Limited {
    param([string]$Text, $Json = $null)
    if ($null -ne $Json -and "$($Json.api_error_status)" -eq '429') { return $true }
    if (-not $Text) { return $false }
    $pattern = '(?i)usage limit|usage credits|out of usage|rate.?limit|\b429\b|limit reached|resets at|switch to another model' +
               '|session limit|weekly limit|hit your [^\n]{0,20}limit|resets\s+(?:at\s+|in\s+)?\d'
    return [regex]::IsMatch($Text, $pattern)
}

# ---------- время сброса лимита ----------
# Сколько секунд ждать до сброса лимита по тексту ответа, с запасом $MarginSeconds (120 с — «плюс 2 минуты»).
# Форматы:  «resets 9:30am», «resets 9am», «resets 14:05», «resets at 02:10»,
#           «resets Oct 25, 9am», «resets Oct 25 at 14:00» (недельный лимит с датой — не дальше 8 суток),
#           «resets in 25 min», «resets in 2 hours», «resets in 1 hour 30 min»,
#           «Claude AI usage limit reached|<секунды Unix>» (старый Claude Code).
# Время — часы этой машины. Прошедшее сегодня время — завтра; сброс прошёл меньше 30 минут назад — ждать
# только запас. Не разобрать — $null (очередь тогда ждёт -LimitWaitMinutes).
# -Now — «сейчас» для теста (tools/test_reset_parse.ps1 подставляет фиксированное время).
function Get-ResetWaitSeconds {
    param([string]$Text, [datetime]$Now = (Get-Date), [int]$MarginSeconds = 120)
    if (-not $Text) { return $null }

    # Старый формат Claude Code: «Claude AI usage limit reached|1792915200» — время сброса в секундах Unix.
    $epoch = [regex]::Match($Text, '(?i)limit reached\|(\d{10})\b')
    if ($epoch.Success) {
        $at = [DateTimeOffset]::FromUnixTimeSeconds([long]$epoch.Groups[1].Value).LocalDateTime
        $wait = ($at - $Now).TotalSeconds
        if ($wait -le 0) { return [int]$MarginSeconds }
        if ($wait -gt 8 * 86400) { return $null }
        return [int][math]::Ceiling($wait + $MarginSeconds)
    }

    # Относительный формат.
    $rel = [regex]::Match($Text, '(?i)resets\s+in\s+(?<spec>\d+\s*[a-z]+(?:\s*(?:,|and)?\s*\d+\s*[a-z]+)*)')
    if ($rel.Success) {
        $sec = 0
        foreach ($p in [regex]::Matches($rel.Groups['spec'].Value, '(?i)(\d+)\s*([a-z]+)')) {
            $n = [int]$p.Groups[1].Value
            $unit = $p.Groups[2].Value.ToLowerInvariant()
            if ($unit -match '^(h|hr|hrs|hour|hours)$') { $sec += $n * 3600 }
            elseif ($unit -match '^(m|min|mins|minute|minutes)$') { $sec += $n * 60 }
            elseif ($unit -match '^(s|sec|secs|second|seconds)$') { $sec += $n }
            elseif ($unit -match '^(d|day|days)$') { $sec += $n * 86400 }
            else { return $null }
        }
        if ($sec -le 0 -or $sec -gt 8 * 86400) { return $null }
        return [int]($sec + $MarginSeconds)
    }

    # Абсолютный формат: am/pm или 24 часа, с датой или без.
    $months = @{ jan = 1; feb = 2; mar = 3; apr = 4; may = 5; jun = 6; jul = 7; aug = 8; sep = 9; oct = 10; nov = 11; dec = 12 }
    $re = '(?i)resets\s+(?:at\s+)?(?:(?<mon>jan|feb|mar|apr|may|jun|jul|aug|sep|oct|nov|dec)[a-z]*\.?\s+(?<day>\d{1,2}),?\s+(?:at\s+)?)?' +
          '(?<h>\d{1,2})(?::(?<mi>\d{2}))?\s*(?<ap>am|pm|a\.m\.|p\.m\.)?(?![\w:])'
    foreach ($m in [regex]::Matches($Text, $re)) {
        $h = [int]$m.Groups['h'].Value
        $mi = if ($m.Groups['mi'].Success) { [int]$m.Groups['mi'].Value } else { 0 }
        if ($mi -gt 59) { continue }
        if ($m.Groups['ap'].Success) {
            if ($h -lt 1 -or $h -gt 12) { continue }
            $h = $h % 12
            if ($m.Groups['ap'].Value -match '^(?i)p') { $h += 12 }
        } else {
            # «resets 14:05» — 24 часа; голое «resets 5» без минут и am/pm не разбирается.
            if (-not $m.Groups['mi'].Success -or $h -gt 23) { continue }
        }
        $dated = $m.Groups['mon'].Success
        if ($dated) {
            $mon = $months[$m.Groups['mon'].Value.Substring(0, 3).ToLowerInvariant()]
            try { $at = New-Object DateTime ($Now.Year, $mon, [int]$m.Groups['day'].Value, $h, $mi, 0) } catch { continue }
            if ($at -lt $Now.AddDays(-30)) { $at = $at.AddYears(1) }
        } else {
            $at = $Now.Date.AddHours($h).AddMinutes($mi)
        }
        if ($at -le $Now) {
            if (($Now - $at).TotalMinutes -lt 30) { return [int]$MarginSeconds }
            if ($dated) { return $null }
            $at = $at.AddDays(1)
        }
        $wait = ($at - $Now).TotalSeconds + $MarginSeconds
        if ($wait -gt 8 * 86400) { return $null }
        return [int][math]::Ceiling($wait)
    }
    return $null
}

# ---------- состояние ----------
function Read-QueueState([string]$Path) {
    $s = [pscustomobject]@{ verified = @(); lastKey = ''; failCount = 0; limitWaits = 0 }
    if (Test-Path -LiteralPath $Path) {
        try {
            $j = Read-QueueText $Path | ConvertFrom-Json
            if ($null -ne $j.verified) { $s.verified = @($j.verified | ForEach-Object { "$_" }) }
            if ($j.lastKey)    { $s.lastKey    = [string]$j.lastKey }
            if ($j.failCount)  { $s.failCount  = [int]$j.failCount }
            if ($j.limitWaits) { $s.limitWaits = [int]$j.limitWaits }
        } catch { Write-Warning "state.json повреждён ($Path) — читаю как пустое состояние" }
    }
    return $s
}
function Save-QueueState([string]$Path, $State) {
    $State.verified = @($State.verified | ForEach-Object { "$_" })
    Write-QueueText $Path ($State | ConvertTo-Json -Depth 5)
}

# ---------- разбор плана ----------
# План читается блоками. Блок — «Сессия N.M» (пачка пунктов, один запуск Claude Code) или «Рубеж РN»
# (проверка одного или нескольких этапов). Этап без блоков (этап 0) — пункт = задание, в конце проверка «N».
# Id блока — любое \S+: суффикс-буквы 2.2b, 2.4b, 2.5b, 8.1b — обычные id, ключи task|2|2.4b уникальны.
function New-QueueBlock([string]$Kind, [string]$Id, [string]$Title) {
    return [pscustomobject]@{
        Kind = $Kind; Id = $Id; Title = $Title; Effort = ''; Turns = 0; Covers = ''
        Items = (New-Object System.Collections.ArrayList)
        Notes = (New-Object System.Collections.ArrayList)
    }
}

# «### Сессия 2.2b · AnnotSelectTool, … · усилие xhigh · ходов 300»
# «### Рубеж Р1 · Ядро и читалка с печатью · этапы 1, 2 · усилие xhigh · ходов 300»
function Read-BlockHeader([string]$Line) {
    if ($Line -notmatch '^###\s+(Сессия|Рубеж)\s+(\S+)\s*(?:·\s*(.*))?$') { return $null }
    $kind = if ($Matches[1] -eq 'Рубеж') { 'verify' } else { 'task' }
    $id = $Matches[2]
    $rest = $Matches[3]
    $b = New-QueueBlock $kind $id ''
    foreach ($p in @($rest -split '·' | ForEach-Object { $_.Trim() } | Where-Object { $_ })) {
        if     ($p -match '^усилие\s+(\S+)$') { $b.Effort = $Matches[1] }
        elseif ($p -match '^ходов\s+(\d+)$')  { $b.Turns  = [int]$Matches[1] }
        elseif ($p -match '^этапы?\s+(.+)$')  { $b.Covers = $Matches[1] }
        elseif (-not $b.Title)                { $b.Title  = $p }
    }
    if (-not $b.Title) { $b.Title = $b.Id }
    return $b
}

function Read-QueuePlan([string]$Path) {
    $lines = (Read-QueueText $Path) -split "`r?`n"
    $stages = New-Object System.Collections.ArrayList
    $cur = $null
    $block = $null
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $l = $lines[$i]
        if ($l -match '^## Этап (\d+)\.\s*(.+)$') {
            $cur = [pscustomobject]@{
                Num = [int]$Matches[1]; Title = $Matches[2].Trim(); Check = ''
                Items  = (New-Object System.Collections.ArrayList)
                Blocks = (New-Object System.Collections.ArrayList)
            }
            [void]$stages.Add($cur); $block = $null
            continue
        }
        if ($l -match '^## ') { $cur = $null; $block = $null; continue }
        if ($null -eq $cur) { continue }
        if ($l -match '^###\s') {
            $b = Read-BlockHeader $l
            if ($b) { [void]$cur.Blocks.Add($b) }
            $block = $b
            continue
        }
        if ($l -match '^- \[([ xX~])\] (.+)$') {
            $item = [pscustomobject]@{ Line = $i; State = $Matches[1].ToLower(); Text = $Matches[2].TrimEnd() }
            [void]$cur.Items.Add($item)
            if ($block -and $block.Kind -eq 'task') { [void]$block.Items.Add($item) }
            continue
        }
        if ($l -match '^\*\*Проверка:\*\*\s*(.+)$') { $cur.Check = $Matches[1].Trim(); continue }
        if ($block -and $l.Trim() -and $l -notmatch '^-{3,}$') { [void]$block.Notes.Add($l.Trim()) }
    }
    foreach ($s in $stages) {
        if ($s.Blocks.Count -gt 0) { continue }
        $n = 0
        foreach ($it in $s.Items) {
            $n++
            $b = New-QueueBlock 'task' "$($s.Num).$n" $it.Text
            [void]$b.Items.Add($it)
            [void]$s.Blocks.Add($b)
        }
        [void]$s.Blocks.Add((New-QueueBlock 'verify' "$($s.Num)" "проверка этапа «$($s.Title)»"))
    }
    return , $stages
}

function Get-Pending($Block) {
    if (-not $Block) { return 0 }
    return @($Block.Items | Where-Object { $_.State -eq ' ' }).Count
}

# Первая сессия с «- [ ]» или первый непройденный рубеж. Блок, все пункты которого «[~]» (10.1 до пилота),
# пропускается — поэтому после этапа 10 рубежа нет (QUEUE.md).
function Get-NextUnit($Stages, $State) {
    $verified = @($State.verified | ForEach-Object { "$_" })
    foreach ($s in $Stages) {
        foreach ($b in $s.Blocks) {
            if ($b.Kind -eq 'task') {
                if ((Get-Pending $b) -gt 0) {
                    return [pscustomobject]@{ Kind = 'task'; Stage = $s; Block = $b; Key = "task|$($s.Num)|$($b.Id)" }
                }
            } elseif ($verified -notcontains $b.Id) {
                return [pscustomobject]@{ Kind = 'verify'; Stage = $s; Block = $b; Key = "verify|$($b.Id)" }
            }
        }
    }
    return $null
}

function Find-QueueBlock($Stages, [int]$StageNum, [string]$Id) {
    $s = $Stages | Where-Object { $_.Num -eq $StageNum } | Select-Object -First 1
    if (-not $s) { return $null }
    return $s.Blocks | Where-Object { $_.Id -eq $Id } | Select-Object -First 1
}

# «**Qt и MuPDF из scratchpad:** …» → «Qt и MuPDF из scratchpad».
function Get-ItemTitle([string]$Text) {
    if ($Text -match '^\*\*(.+?)\*\*') { return $Matches[1].Trim().TrimEnd(':') }
    if ($Text.Length -gt 90) { return $Text.Substring(0, 90) + '…' }
    return $Text
}

# Страж старта (эталон 10.6 (д)): этап 0 делается интерактивно с человеком. Пока в нём есть пункт не «[x]»
# или в state.json нет "verified": ["0"], Get-NextUnit принял бы пункты этапа 0 за задания очереди.
function Get-Stage0Gate($Stages, $Verified) {
    $s0 = $Stages | Where-Object { $_.Num -eq 0 } | Select-Object -First 1
    $items = if ($s0) { @($s0.Items) } else { @() }
    $pending = @($items | Where-Object { $_.State -ne 'x' })
    $isVerified = @($Verified | ForEach-Object { "$_" }) -contains '0'
    return [pscustomobject]@{
        HasStage0 = [bool]$s0
        Total     = $items.Count
        Pending   = $pending
        Verified  = $isVerified
        Ok        = (-not $s0) -or ($pending.Count -eq 0 -and $isVerified)
    }
}

# Отметка одного пункта этапа (когда в сессии остался один пункт и Claude ответил DONE/NEEDS_HUMAN).
function Set-PlanItemMark([string]$Path, [int]$StageNum, [string]$Text, [string]$Mark) {
    $raw = Read-QueueText $Path
    $eol = if ($raw.Contains("`r`n")) { "`r`n" } else { "`n" }
    $lines = [System.Collections.Generic.List[string]]($raw -split "`r?`n")
    $inStage = $false
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i] -match '^## Этап (\d+)\.') { $inStage = ([int]$Matches[1] -eq $StageNum); continue }
        if ($lines[$i] -match '^## ') { $inStage = $false; continue }
        if ($inStage -and $lines[$i] -eq "- [ ] $Text") {
            $lines[$i] = "- [$Mark] $Text"
            Write-QueueText $Path ($lines -join $eol)
            return $true
        }
    }
    return $false
}
