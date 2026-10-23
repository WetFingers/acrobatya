# Защитный хук Claude Code (PreToolUse для Bash и PowerShell) — очередь «АкроБати».
# Блокирует опасные команды даже в режиме --dangerously-skip-permissions.
# Вход — JSON хука на stdin ({tool_name, tool_input.command, cwd}); код выхода 2 = команда запрещена
# (текст из stderr получает Claude), 0 = можно. Ошибка внутри хука — пропуск (код 0), как в образце.
#
# Правила — эталон 10.6:
#   * git push / remote add / reset --hard / clean -f / config --global;
#   * установщики — по ТОЧНЫМ образцам (подстрока «setup.exe» перекрыла бы наш dist\AcroBatya-0.N.0-setup.exe
#     и unins000.exe на рубежах): VS Installer\setup.exe, vs_*.exe, msiexec /i|/x, winget install … --scope
#     machine, vc_redist*.exe /install; разрешены dist\AcroBatya-*-setup.exe /VERYSILENT, unins000.exe /VERYSILENT,
#     msiexec /a (административная распаковка LibreOffice в C:\Tools\libreoffice), tools\build_lock.ps1 -Target;
#   * запись и удаление — только в проекте, C:\Tools\logs и %TEMP% (кроме %TEMP%\claude — scratchpad человека);
#     C:\Qt, C:\src\mupdf, C:\Tools (с vcpkg и корпусом ottisk\corpus), профиль Acrobat — только чтение; в проекте только
#     чтение у .git, эталона, справочников и tests/render/expected (их правит лишь render_refs);
#   * реестр — только HKCU\Software\Classes\AcroBatya* и значение AcroBatya в HKCU\Software\RegisteredApplications;
#   * новые зависимости (pip/winget/choco/scoop/npm -g install) — только через QUEUE: BLOCKED.
# Запись определяется эвристикой по командной строке (перенаправления, командлеты и утилиты записи, флаги
# вывода); то, что программа пишет сама внутри себя, хук не видит. Проверка — tools/test_guard.ps1.
# Файл в UTF-8 с BOM: Windows PowerShell 5.1 иначе читает кириллицу неверно.

$ErrorActionPreference = 'Stop'
try { [Console]::InputEncoding = New-Object System.Text.UTF8Encoding $false } catch { }
try { [Console]::OutputEncoding = New-Object System.Text.UTF8Encoding $false } catch { }

function Block([string]$reason) {
    [Console]::Error.WriteLine("Команда заблокирована защитным хуком: $reason. Найди безопасный способ или заверши задание строкой QUEUE: BLOCKED.")
    exit 2
}

# ---------- корни ----------
$ProjectRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path.TrimEnd('\')
$ToolsRoot = if ($env:OTTISK_TOOLS) { $env:OTTISK_TOOLS.TrimEnd('\') } else { 'C:\Tools' }
$LogsRoots = @("$ToolsRoot\logs", 'C:\Tools\logs')
$TempRoots = @($env:TEMP, $env:TMP, (Join-Path $env:LOCALAPPDATA 'Temp'), (Join-Path $env:USERPROFILE 'AppData\Local\Temp')) |
    Where-Object { $_ } | ForEach-Object { $_.TrimEnd('\') } | Select-Object -Unique
$ScratchRoots = @($TempRoots | ForEach-Object { "$_\claude" })
$ReadOnlyRoots = @(
    @{ P = 'C:\Qt'; R = 'C:\Qt — только чтение (эталон 8.3)' },
    @{ P = 'C:\src\mupdf'; R = 'C:\src\mupdf — только чтение (патчи MuPDF только через tools/mupdf-patches и человека)' },
    @{ P = $ToolsRoot; R = 'C:\Tools — только чтение, писать можно только в C:\Tools\logs' },
    @{ P = 'C:\Tools'; R = 'C:\Tools — только чтение, писать можно только в C:\Tools\logs' },
    @{ P = (Join-Path $env:APPDATA 'Adobe'); R = 'профиль Acrobat человека' },
    @{ P = (Join-Path $env:LOCALAPPDATA 'Adobe'); R = 'профиль Acrobat человека' },
    @{ P = (Join-Path $env:USERPROFILE 'AppData\LocalLow\Adobe'); R = 'профиль Acrobat человека' }
)
$ProtectedInProject = @('.git', 'docs\design.md', 'docs\sessions.md', 'docs\tech-facts.md', 'docs\features.json',
                        'docs\features.md', 'tests\render\expected')
# Явно разрешённая цель вне общих правил: административная распаковка LibreOffice (msiexec /a, проба этапа 0).
$MsiAdminTargets = @("$ToolsRoot\libreoffice", 'C:\Tools\libreoffice')

function Test-InRoot([string]$full, [string]$root) {
    if (-not $full -or -not $root) { return $false }
    $f = $full.ToLowerInvariant().TrimEnd('\')
    $r = $root.ToLowerInvariant().TrimEnd('\')
    return ($f -eq $r -or $f.StartsWith("$r\"))
}

# Почему запись/удаление по этому пути запрещены; $null — можно.
function Get-PathVerdict([string]$full, [string]$op) {
    foreach ($r in $LogsRoots) { if (Test-InRoot $full $r) { return $null } }
    foreach ($r in $ScratchRoots) { if (Test-InRoot $full $r) { return 'scratchpad человека (%TEMP%\claude) — только чтение' } }
    foreach ($r in $TempRoots) {
        if (Test-InRoot $full $r) {
            if ($op -eq 'delete' -and $full.TrimEnd('\') -ieq $r) { return 'удаление всего %TEMP%' }
            return $null
        }
    }
    if (Test-InRoot $full $ProjectRoot) {
        if ($op -eq 'delete' -and $full.TrimEnd('\') -ieq $ProjectRoot) { return 'удаление корня проекта' }
        foreach ($rel in $ProtectedInProject) {
            if (Test-InRoot $full (Join-Path $ProjectRoot $rel)) { return "$($rel -replace '\\', '/') — только чтение (эталон, справочники, эталоны рендера, история Git)" }
        }
        return $null
    }
    foreach ($ro in $ReadOnlyRoots) { if (Test-InRoot $full $ro.P) { return $ro.R } }
    return 'путь вне проекта, C:\Tools\logs и %TEMP%'
}

# ---------- разбор командной строки ----------
function ConvertTo-Plain([string]$t) { return (($t -replace '"', '') -replace "'", '').Trim() }

# Слово → полный путь; $null, если это не путь или его нельзя вычислить (переменная PowerShell, URL, реестр).
function Resolve-TokenPath([string]$tok) {
    $p = ConvertTo-Plain $tok
    if (-not $p) { return $null }
    if ($p -match '^(?i)(?:\$null|nul|con|/dev/(?:null|stdout|stderr|tty))$') { return $null }
    if ($p -match '^(?i)[a-z][a-z0-9+.-]+://') { return $null }
    if ($p -match '^(?i)(?:HK(?:LM|CU|CR|U|CC)(?::|\\|$)|HKEY_|Registry::)') { return $null }
    if ($p -match '^-') { return $null }
    $p = [regex]::Replace($p, '(?i)\$\{?env:(\w+)\}?|%(\w+)%|\$\{(\w+)\}|\$([a-z_][a-z0-9_]*)', [System.Text.RegularExpressions.MatchEvaluator]{
        param($m)
        $name = @($m.Groups[1].Value, $m.Groups[2].Value, $m.Groups[3].Value, $m.Groups[4].Value) | Where-Object { $_ } | Select-Object -First 1
        if ($name -ieq 'HOME') { return $env:USERPROFILE }
        $v = [Environment]::GetEnvironmentVariable($name)
        if ($v) { return $v }
        return '<UNRESOLVED>'
    })
    if ($p -match '<UNRESOLVED>|\$|[()]') { return $null }
    if ($p -match '^~(?=[\\/]|$)') { $p = $env:USERPROFILE + $p.Substring(1) }
    if ($p -match '^/tmp(?=/|$)') { $p = $env:TEMP + $p.Substring(4) }
    if ($p -match '^/([a-zA-Z])(?=/|$)') { $p = $Matches[1] + ':' + $p.Substring(2) }
    if ($p -match '^[a-zA-Z]:$') { $p += '\' }
    $p = ($p -replace '/', '\') -replace '[*?]', '_'
    if ($p -match '^\\(?!\\)') { return '\\unix' + $p }
    if ($p -notmatch '^[a-zA-Z]:\\' -and $p -notmatch '^\\\\') { $p = Join-Path $script:Cwd $p }
    try { $p = [IO.Path]::GetFullPath($p) } catch { return $null }
    return $p.TrimEnd('\')
}

# Флаг: -x, --long, /s /q /E /l*v (короткий ключ cmd без второго слеша — не путь /c/Users).
function Test-IsFlag([string]$w) {
    $p = ConvertTo-Plain $w
    if ($p -match '^-') { return $true }
    if ($p -match '^/[A-Za-z?][A-Za-z0-9?*:+-]*$') { return $true }
    return $false
}

# Флаги, которые забирают следующее слово как значение (остальные — переключатели).
$ValueFlags = @('-path', '-literalpath', '-filepath', '-destination', '-destinationpath', '-itemtype', '-value', '-encoding',
    '-filter', '-include', '-exclude', '-name', '-target', '-type', '-outfile', '-t', '--target-directory', '-inputobject',
    '-delimiter', '-newname', '-argumentlist', '-workingdirectory', '-redirectstandardoutput', '-redirectstandarderror',
    '-redirectstandardinput', '-o', '--output', '--out', '--outdir', '--out-dir', '--output-dir', '--outputdir', '-outputpath',
    '-outdir', '--prefix', '--install-dir', '--install-prefix', '-b', '-c', '-j', '--parallel', '--config', '--target',
    '/v', '/d', '/t')
# Флаги вывода — их значение всегда цель записи, у любой программы.
$OutputFlags = @('-o', '--output', '--out', '--outdir', '--out-dir', '--output-dir', '--outputdir', '-outfile', '-outputpath',
    '-outdir', '-destinationpath', '--prefix', '--install-dir', '--install-prefix', '/dir', '/log', 'targetdir',
    '-redirectstandardoutput', '-redirectstandarderror')

function Get-ArgsInfo($words) {
    $pos = New-Object System.Collections.ArrayList
    $named = @{}
    for ($i = 0; $i -lt $words.Count; $i++) {
        $w = $words[$i]
        $p = ConvertTo-Plain $w
        $name = $null; $value = $null
        if ($p -match '^(-{1,2}[A-Za-z][\w-]*)[=:](.+)$') { $name = $Matches[1]; $value = $Matches[2] }
        elseif ($p -match '^(/[A-Za-z]+)=(.*)$') { $name = $Matches[1]; $value = $Matches[2] }
        elseif ($p -match '^([A-Za-z_]+)=(.+)$') { $name = $Matches[1]; $value = $Matches[2] }
        if ($name) {
            $k = $name.ToLowerInvariant()
            if (-not $named.ContainsKey($k)) { $named[$k] = New-Object System.Collections.ArrayList }
            [void]$named[$k].Add($value)
            continue
        }
        if (Test-IsFlag $w) {
            $k = $p.ToLowerInvariant()
            if ($ValueFlags -contains $k -and $i + 1 -lt $words.Count) {
                if (-not $named.ContainsKey($k)) { $named[$k] = New-Object System.Collections.ArrayList }
                [void]$named[$k].Add($words[$i + 1])
                $i++
            }
            continue
        }
        [void]$pos.Add($w)
    }
    return [pscustomobject]@{ Pos = $pos; Named = $named }
}

function Get-Named($info, [string[]]$names) {
    $out = @()
    foreach ($n in $names) { if ($info.Named.ContainsKey($n)) { $out += @($info.Named[$n]) } }
    return $out
}

function Get-VerbName([string]$w) {
    $p = ConvertTo-Plain $w
    $leaf = ($p -split '[\\/]')[-1]
    return ($leaf -replace '(?i)\.(exe|cmd|bat|com)$', '').ToLowerInvariant()
}

# Сегменты команды: разделители ; & && || | перевод строки и скобки; перенаправления > >> 2> *> — цели записи.
function Get-Segments([string]$text) {
    $re = '(?<redir>\d?>>?&\d|[*\d]?>>?)|(?<sep>&&|\|\||[;&|\r\n{}()])|(?<word>(?:\$\{[^}]*\}|\$\([^)]*\)|"[^"]*"|''[^'']*''|[^\s;&|<>"''{}()])+)'
    $segs = New-Object System.Collections.ArrayList
    $cur = [pscustomobject]@{ Words = (New-Object System.Collections.ArrayList); Redirs = (New-Object System.Collections.ArrayList) }
    $pending = $false
    foreach ($m in [regex]::Matches($text, $re)) {
        if ($m.Groups['sep'].Success) {
            if ($cur.Words.Count -gt 0 -or $cur.Redirs.Count -gt 0) { [void]$segs.Add($cur) }
            $cur = [pscustomobject]@{ Words = (New-Object System.Collections.ArrayList); Redirs = (New-Object System.Collections.ArrayList) }
            $pending = $false
            continue
        }
        if ($m.Groups['redir'].Success) { $pending = ($m.Value -notmatch '&\d$'); continue }
        if ($pending) { [void]$cur.Redirs.Add($m.Groups['word'].Value); $pending = $false; continue }
        [void]$cur.Words.Add($m.Groups['word'].Value)
    }
    if ($cur.Words.Count -gt 0 -or $cur.Redirs.Count -gt 0) { [void]$segs.Add($cur) }
    return , $segs
}

# ---------- реестр ----------
function Test-RegistryToken([string]$w) { return ((ConvertTo-Plain $w) -match '^(?i)(?:HK(?:LM|CU|CR|U|CC)(?::|\\|$)|HKEY_|Registry::)') }
function Test-RegAllowed([string]$key, [string]$valueName) {
    $k = ConvertTo-Plain $key
    $k = $k -replace '^(?i)Registry::', '' -replace '^(?i)HKEY_CURRENT_USER', 'HKCU' -replace '^(?i)HKCU:', 'HKCU'
    $k = $k -replace '/', '\'
    if ($k -match '^(?i)HKCU\\Software\\Classes\\AcroBatya[^\\]*(\\|$)') { return $true }
    if ($k -match '^(?i)HKCU\\Software\\RegisteredApplications\\?$' -and (ConvertTo-Plain $valueName) -ieq 'AcroBatya') { return $true }
    return $false
}
$RegCmdlets = @('new-item', 'ni', 'set-item', 'si', 'remove-item', 'ri', 'rm', 'del', 'rmdir', 'rd', 'erase', 'clear-item', 'cli',
    'rename-item', 'rni', 'copy-item', 'cpi', 'cp', 'copy', 'move-item', 'mi', 'mv', 'move', 'new-itemproperty', 'set-itemproperty',
    'sp', 'remove-itemproperty', 'rp', 'rename-itemproperty', 'rnp', 'clear-itemproperty', 'clp', 'copy-itemproperty', 'cpp',
    'move-itemproperty', 'mp')

# ---------- правила по всей строке ----------
$Rules = @(
    @{ P = '(?i)\bgit(?:\.exe)?\s+(?:-[cC]\s+\S+\s+)*push\b'; R = 'git push запрещён: репозиторий только локальный' },
    @{ P = '(?i)\bgit(?:\.exe)?\s+(?:-[cC]\s+\S+\s+)*remote\s+(?:add|set-url)\b'; R = 'удалённые репозитории не используются' },
    @{ P = '(?i)\bgit(?:\.exe)?\s+(?:-[cC]\s+\S+\s+)*reset\b[^;&|\n]*--hard\b'; R = 'git reset --hard может уничтожить работу' },
    @{ P = '(?i)\bgit(?:\.exe)?\s+(?:-[cC]\s+\S+\s+)*clean\b[^;&|\n]*\s-\w*f'; R = 'git clean -f удаляет неотслеживаемые файлы' },
    @{ P = '(?i)\bgit(?:\.exe)?\s+config\b[^;&|\n]*--(?:global|system)\b'; R = 'глобальные настройки Git менять нельзя' },
    @{ P = '(?i)(?:^|[;&|(]\s*)(?:format|diskpart|bcdedit|shutdown|vssadmin)(?:\.exe|\.com)?(?:\s|$)|\bcipher(?:\.exe)?\s+/w'; R = 'системная команда' },
    @{ P = '(?i)\bSet-ExecutionPolicy\b'; R = 'смена политики выполнения' },
    @{ P = '(?i)\b(?:Stop-Computer|Restart-Computer)\b'; R = 'выключение компьютера' },
    @{ P = '(?i)\b(?:curl|wget|iwr|Invoke-WebRequest|irm|Invoke-RestMethod)\b[^\n]*\|\s*(?:sh|bash|iex|Invoke-Expression|powershell|pwsh)\b'; R = 'выполнение скачанного скрипта' },
    @{ P = '(?i)\brm\s+-\w*r\w*\s+(?:/|~|\$HOME|/[a-z]/?|[a-z]:[\\/]?)(?:\s|$)'; R = 'рекурсивное удаление корня диска или домашней папки' },
    @{ P = '(?i)\b(?:npm|pnpm|yarn)\s+(?:publish|login)\b|\bcargo\s+(?:publish|login)\b|\btwine\s+upload\b'; R = 'публикация пакетов' },
    # Установщики — точные образцы.
    @{ P = '(?i)Microsoft Visual Studio[\\/]+Installer[\\/]+(?:setup|vs_installer(?:shell)?)\.exe'; R = 'Visual Studio Installer — состав Build Tools меняет только человек' },
    @{ P = '(?i)(?:^|[\s"''\\/&;(=])vs_\w+\.exe\b'; R = 'установщик Visual Studio (vs_*.exe) — только человек' },
    @{ P = '(?i)\bmsiexec(?:\.exe)?\b[^\n]*?[\s"'',][/-](?:i|x|package|uninstall|update|f[a-z]*|j[mu])(?=[\s"'',]|$)'; R = 'msiexec /i, /x — установка и удаление MSI (без прав администратора разрешена только распаковка msiexec /a)' },
    @{ P = '(?i)\bwinget(?:\.exe)?\s+(?:install|upgrade|add)\b[^\n]*--scope\s+machine\b'; R = 'winget install --scope machine — установка для всех пользователей (UAC) только человеком' },
    @{ P = '(?i)\bvc_redist[\w.-]*\.exe\b[^\n]*?[\s"''][/-]install\b'; R = 'vc_redist /install — CRT кладётся рядом с exe (Microsoft.VC143.CRT), в систему не ставится' },
    # Новые зависимости — только QUEUE: BLOCKED.
    @{ P = '(?i)(?:^|[\s;&|("''/\\])(?:pip3?(?:\.exe)?|-m\s+pip)\s+(?:install|uninstall)\b'; R = 'установка пакетов Python — новые зависимости только через QUEUE: BLOCKED' },
    @{ P = '(?i)\bwinget(?:\.exe)?\s+(?:install|upgrade|uninstall|import)\b|\b(?:choco|scoop)(?:\.exe)?\s+(?:install|upgrade|uninstall)\b|\bnpm(?:\.cmd)?\s+(?:install|i|add)\b[^;&|\n]*\s(?:-g|--global)\b'; R = 'установка программ — новые зависимости только через QUEUE: BLOCKED; LibreOffice и прочее ставит человек' },
    # Реестр через .NET и regedit.
    @{ P = '(?i)\[Microsoft\.Win32\.Registry(?:Key)?\]::[^\n]*\.(?:SetValue|CreateSubKey|DeleteSubKey\w*|DeleteValue)\b|(?:^|[\s;&|("''\\/])regedit(?:\.exe)?\b'; R = 'изменение реестра' }
)

# ---------- проверка сегмента ----------
$DeleteVerbs = @('rm', 'rmdir', 'rd', 'del', 'erase', 'remove-item', 'ri', 'unlink', 'shred', 'rimraf')
$WriteFirstVerbs = @('set-content', 'sc', 'add-content', 'ac', 'out-file', 'new-item', 'ni', 'clear-content', 'clc', 'export-csv',
    'export-clixml', 'tee-object')
$WriteAllVerbs = @('mkdir', 'md', 'touch', 'tee')
$CopyVerbs = @('cp', 'copy', 'cpi', 'copy-item', 'mv', 'move', 'mi', 'move-item')
$CopySecondVerbs = @('robocopy', 'xcopy')
$RenameVerbs = @('rename-item', 'ren', 'rni', 'rename')
$Keywords = @('if', 'elseif', 'else', 'foreach', 'for', 'while', 'do', 'try', 'catch', 'finally', 'switch', 'then', 'fi', 'done',
    'time', 'exec', 'nohup', 'call', 'start', 'start-process', '.')

function Assert-Target([string]$tok, [string]$op, [string[]]$extraAllowed = @()) {
    $full = Resolve-TokenPath $tok
    if (-not $full) { return }
    foreach ($a in $extraAllowed) { if (Test-InRoot $full $a) { return } }
    $why = Get-PathVerdict $full $op
    if ($why) {
        $what = if ($op -eq 'delete') { 'удаление' } else { 'запись' }
        Block "${what}: $why ($(ConvertTo-Plain $tok))"
    }
}

function Test-Segment($seg, [int]$depth) {
    foreach ($r in $seg.Redirs) { Assert-Target $r 'write' }
    $words = New-Object System.Collections.ArrayList
    foreach ($w in $seg.Words) { [void]$words.Add($w) }
    # Ведущие ключевые слова и присваивания: «if», «$x =», «call», «start».
    while ($words.Count -gt 0) {
        $v0 = Get-VerbName $words[0]
        if ($Keywords -contains $v0 -and $words.Count -gt 1) { $words.RemoveAt(0); continue }
        if ($words.Count -gt 2 -and (ConvertTo-Plain $words[0]) -match '^\$[\w:]+$' -and (ConvertTo-Plain $words[1]) -match '^[-+*/]?=$') { $words.RemoveRange(0, 2); continue }
        break
    }
    if ($words.Count -eq 0) { return }
    $verb = Get-VerbName $words[0]
    $rest = New-Object System.Collections.ArrayList
    for ($i = 1; $i -lt $words.Count; $i++) { [void]$rest.Add($words[$i]) }
    $info = Get-ArgsInfo $rest

    # Вложенные оболочки: cmd /c "…", powershell -Command "…" / -EncodedCommand, bash -c "…".
    if ($depth -lt 3) {
        $inner = $null
        for ($i = 0; $i -lt $rest.Count; $i++) {
            $p = (ConvertTo-Plain $rest[$i]).ToLowerInvariant()
            if ($i + 1 -ge $rest.Count) { break }
            # «//c» — так cmd /c пишут в Git Bash (иначе MSYS превращает /c в путь C:/).
            if ($verb -eq 'cmd' -and $p -match '^//?[ck]$') { $inner = ($rest[($i + 1)..($rest.Count - 1)] -join ' '); break }
            if ($verb -in @('powershell', 'pwsh') -and $p -match '^-(?:c|co|com|comm|comma|comman|command)$') { $inner = ($rest[($i + 1)..($rest.Count - 1)] -join ' '); break }
            if ($verb -in @('powershell', 'pwsh') -and $p -match '^-(?:e|ec|en|enc|enco|encod|encode|encoded|encodedc\w*)$' -and $i + 1 -lt $rest.Count) {
                try { $inner = [Text.Encoding]::Unicode.GetString([Convert]::FromBase64String((ConvertTo-Plain $rest[$i + 1]))) } catch { }
                break
            }
            if ($verb -in @('bash', 'sh') -and $p -eq '-c') { $inner = ($rest[($i + 1)..($rest.Count - 1)] -join ' '); break }
        }
        if ($inner) {
            $inner = $inner.Trim()
            if ($inner.Length -ge 2 -and (($inner[0] -eq '"' -and $inner[-1] -eq '"') -or ($inner[0] -eq "'" -and $inner[-1] -eq "'"))) { $inner = $inner.Substring(1, $inner.Length - 2) }
            Test-CommandText $inner ($depth + 1)
        }
    }

    # cd меняет каталог для относительных путей дальше по строке.
    if ($verb -in @('cd', 'chdir', 'set-location', 'sl', 'pushd', 'push-location')) {
        $arg = @(Get-Named $info @('-path', '-literalpath')) + @($info.Pos) | Select-Object -First 1
        if ($arg) { $full = Resolve-TokenPath $arg; if ($full -and (Test-Path -LiteralPath $full -PathType Container)) { $script:Cwd = $full } }
        return
    }

    # Реестр.
    if ($verb -eq 'reg') {
        $sub = if ($info.Pos.Count -gt 0) { (ConvertTo-Plain $info.Pos[0]).ToLowerInvariant() } else { '' }
        if ($sub -in @('add', 'delete')) {
            $key = if ($info.Pos.Count -gt 1) { $info.Pos[1] } else { '' }
            $vname = @(Get-Named $info @('/v')) | Select-Object -First 1
            if (-not (Test-RegAllowed $key $vname)) { Block "изменение реестра ($(ConvertTo-Plain $key)) — можно только HKCU\Software\Classes\AcroBatya* и значение AcroBatya в RegisteredApplications" }
        } elseif ($sub -in @('import', 'copy', 'restore', 'load', 'unload')) {
            Block "изменение реестра (reg $sub)"
        } elseif ($sub -in @('export', 'save') -and $info.Pos.Count -gt 2) {
            Assert-Target $info.Pos[2] 'write'
        }
        return
    }
    if ($RegCmdlets -contains $verb) {
        $cands = @(Get-Named $info @('-path', '-literalpath')) + @($info.Pos)
        $regPaths = @($cands | Where-Object { Test-RegistryToken $_ })
        if ($regPaths.Count -gt 0) {
            $vname = @(Get-Named $info @('-name')) | Select-Object -First 1
            if ($verb -notmatch 'itemproperty$|^(sp|rp|rnp|clp|cpp|mp)$') { $vname = '' }
            foreach ($k in $regPaths) {
                if (-not (Test-RegAllowed $k $vname)) { Block "изменение реестра ($(ConvertTo-Plain $k)) — можно только HKCU\Software\Classes\AcroBatya* и значение AcroBatya в RegisteredApplications" }
            }
            return
        }
    }

    # Флаги вывода — у любой программы. msiexec /a: TARGETDIR в C:\Tools\libreoffice разрешён явно.
    $extra = @()
    if ($verb -eq 'msiexec' -and @($rest | Where-Object { (ConvertTo-Plain $_) -match '^(?i)[/-]a$' }).Count -gt 0) { $extra = $MsiAdminTargets }
    foreach ($f in $OutputFlags) {
        if ($info.Named.ContainsKey($f)) { foreach ($v in $info.Named[$f]) { Assert-Target $v 'write' $extra } }
    }

    if ($DeleteVerbs -contains $verb) {
        foreach ($t in (@(Get-Named $info @('-path', '-literalpath')) + @($info.Pos))) { Assert-Target $t 'delete' }
        return
    }
    if ($WriteFirstVerbs -contains $verb) {
        $named = @(Get-Named $info @('-path', '-literalpath', '-filepath'))
        if ($named.Count -gt 0) { foreach ($t in $named) { Assert-Target $t 'write' } }
        elseif ($info.Pos.Count -gt 0) { Assert-Target $info.Pos[0] 'write' }
        return
    }
    if ($WriteAllVerbs -contains $verb) {
        foreach ($t in (@(Get-Named $info @('-path', '-literalpath')) + @($info.Pos))) { Assert-Target $t 'write' }
        return
    }
    if ($CopyVerbs -contains $verb) {
        $dst = @(Get-Named $info @('-destination', '-t', '--target-directory'))
        if ($dst.Count -gt 0) { foreach ($t in $dst) { Assert-Target $t 'write' } }
        elseif ($info.Pos.Count -ge 2) { Assert-Target $info.Pos[$info.Pos.Count - 1] 'write' }
        if ($verb -in @('mv', 'move', 'mi', 'move-item')) {
            # Перенос удаляет исходное место.
            $src = @(Get-Named $info @('-path', '-literalpath'))
            if ($src.Count -eq 0 -and $info.Pos.Count -ge 2) { $src = @($info.Pos[0..($info.Pos.Count - 2)]) }
            foreach ($t in $src) { Assert-Target $t 'delete' }
        }
        return
    }
    if ($CopySecondVerbs -contains $verb) {
        if ($info.Pos.Count -ge 2) { Assert-Target $info.Pos[1] 'write' }
        if ($verb -eq 'robocopy' -and $info.Pos.Count -ge 1 -and @($rest | Where-Object { (ConvertTo-Plain $_) -match '^(?i)/(mov|move|purge|mir)$' }).Count -gt 0) {
            Assert-Target $info.Pos[0] 'delete'
        }
        return
    }
    if ($RenameVerbs -contains $verb) {
        $t = @(Get-Named $info @('-path', '-literalpath')) + @($info.Pos) | Select-Object -First 1
        if ($t) { Assert-Target $t 'write' }
        return
    }
    if ($verb -eq 'expand-archive' -and $info.Pos.Count -ge 2) { Assert-Target $info.Pos[1] 'write'; return }
    if ($verb -eq 'tar') {
        foreach ($t in (Get-Named $info @('-c', '--directory'))) { Assert-Target $t 'write' }
        return
    }
    if ($verb -eq '7z') {
        foreach ($w in $rest) { $p = ConvertTo-Plain $w; if ($p -match '^-o(.+)$') { Assert-Target $Matches[1] 'write' } }
        return
    }
    if ($verb -eq 'cmake') {
        foreach ($t in (Get-Named $info @('-b'))) { Assert-Target $t 'write' }
        foreach ($w in $rest) { $p = ConvertTo-Plain $w; if ($p -match '^-B(.+)$') { Assert-Target $Matches[1] 'write' } }
        return
    }
    if ($verb -eq 'git' -and $info.Pos.Count -ge 3 -and (ConvertTo-Plain $info.Pos[0]) -ieq 'clone') { Assert-Target $info.Pos[2] 'write'; return }
    # python -m venv <каталог>
    for ($i = 0; $i -lt $rest.Count - 1; $i++) {
        if ((ConvertTo-Plain $rest[$i]) -eq '-m' -and (ConvertTo-Plain $rest[$i + 1]) -eq 'venv') {
            for ($j = $i + 2; $j -lt $rest.Count; $j++) { if (-not (Test-IsFlag $rest[$j])) { Assert-Target $rest[$j] 'write'; break } }
            break
        }
    }
}

function Test-CommandText([string]$text, [int]$depth) {
    foreach ($r in $Rules) { if ($text -match $r.P) { Block $r.R } }
    foreach ($seg in (Get-Segments $text)) { Test-Segment $seg $depth }
}

# ---------- вход ----------
try {
    $raw = [Console]::In.ReadToEnd()
    # Вызывающий с консолью UTF-8 (кодовая страница 65001) пишет в stdin BOM, а InputEncoding без BOM
    # оставляет его символом U+FEFF — ConvertFrom-Json на нём падал, и хук пропускал всё (код 0).
    if ($raw) { $raw = $raw.TrimStart([char]0xFEFF, [char]0x200B).Trim() }
    $data = $raw | ConvertFrom-Json
} catch { exit 0 }
$cmd = [string]$data.tool_input.command
if ([string]::IsNullOrWhiteSpace($cmd)) { exit 0 }
$script:Cwd = $ProjectRoot
if ($data.cwd) {
    $c = ([string]$data.cwd) -replace '/', '\'
    if ($c -match '^\\([a-zA-Z])\\') { $c = $Matches[1] + ':' + $c.Substring(2) }
    if (Test-Path -LiteralPath $c -PathType Container) { $script:Cwd = (Resolve-Path -LiteralPath $c).Path }
}

try {
    Test-CommandText $cmd 0
} catch {
    # Сбой разбора — не повод останавливать работу (как в образце): пропускаем, но сообщаем.
    [Console]::Error.WriteLine("guard.ps1: внутренняя ошибка разбора ($($_.Exception.Message)) — команда пропущена без проверки")
    exit 0
}
exit 0
