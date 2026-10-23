<#
.SYNOPSIS
    compat.rtf (план 1.4): RTF, записанный АкроБатей, открывается в Microsoft Word через COM.

.DESCRIPTION
    powershell -NoProfile -ExecutionPolicy Bypass -File tests\compat\rtf_word_check.ps1 <каталог с *.rtf>

    Для каждого *.rtf: Word.Application (невидимый, DisplayAlerts = 0) → Documents.Open(путь,
    ConfirmConversions = $false, ReadOnly = $true) → Content.Text. Ожидания: «привет.rtf» содержит «Привет»,
    прочие открываются с непустым текстом. Файлы пишет unit_tests text_export_compat.DISABLED_write_rtf
    (ctest compat.rtf.generate).
    Код выхода: 0 — всё открылось; 1 — нет; 77 — Word не зарегистрирован в COM или файлов нет (пропуск).
    Грабли COM (как tools\gen\make_office_fixtures.ps1): путь передавать как [string], Close/Quit — без
    аргументов (документ закрывается без сохранения через Saved = $true).
    Файл в UTF-8 с BOM: Windows PowerShell 5.1 иначе читает кириллицу неверно.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Dir
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding $false

if (-not [type]::GetTypeFromProgID('Word.Application')) {
    Write-Output 'SKIP: Word не зарегистрирован в COM'
    exit 77
}
if (-not (Test-Path -LiteralPath $Dir)) {
    Write-Output "SKIP: нет каталога $Dir"
    exit 77
}
$files = @(Get-ChildItem -LiteralPath $Dir -Filter '*.rtf' -File)
if ($files.Count -eq 0) {
    Write-Output "SKIP: нет *.rtf в $Dir"
    exit 77
}

$failed = 0
$word = $null
try {
    $word = New-Object -ComObject Word.Application
    $word.Visible = $false
    $word.DisplayAlerts = 0
    foreach ($f in $files) {
        $doc = $null
        try {
            $doc = $word.Documents.Open([string]$f.FullName, $false, $true)
            $text = [string]$doc.Content.Text
            if ($f.BaseName -eq 'привет') {
                $ok = $text.Contains('Привет')
            }
            else {
                $ok = $text.Trim().Length -gt 0
            }
            if ($ok) {
                Write-Output ('ok    {0} ({1} символов)' -f $f.Name, $text.Length)
            }
            else {
                Write-Output ('FAIL  {0}: в тексте нет ожидаемого: {1}' -f $f.Name, $text.Substring(0, [Math]::Min(80, $text.Length)))
                $failed++
            }
        }
        catch {
            Write-Output ('FAIL  {0}: {1}' -f $f.Name, $_.Exception.Message)
            $failed++
        }
        finally {
            if ($doc) {
                $doc.Saved = $true
                $doc.Close()
                [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($doc)
            }
        }
    }
}
finally {
    if ($word) {
        $word.Quit()
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($word)
    }
    [GC]::Collect()
    [GC]::WaitForPendingFinalizers()
}
if ($failed -gt 0) { exit 1 }
exit 0
