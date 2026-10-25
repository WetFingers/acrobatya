<#
.SYNOPSIS
    Плоские анкеты для Fill & Sign (план 5.3) → tests\fixtures\forms\flat (anketa.pdf, flat.json).

.DESCRIPTION
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\gen\make_flat_forms.ps1 [-Out <корень>] [-Python <python.exe>]

    Сейчас — одна анкета своей геометрии (make_flat_forms.py: подчёркивания, гребёнки, рамки, флажки — ожидания
    в flat.json). Сессия 6.4 (автоопределение полей) добавляет сюда анкеты из Word.
    Python — $env:OTTISK_PYTHON или C:\Tools\venv-test\Scripts\python.exe, всегда с -X utf8 -I.
    Файл в UTF-8 с BOM: Windows PowerShell 5.1 иначе читает кириллицу неверно.
#>
[CmdletBinding()]
param(
    [string]$Out = '',
    [string]$Python = ''
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding $false

$gen = $PSScriptRoot
$repo = (Resolve-Path (Join-Path $gen '..\..')).Path
if (-not $Out) { $Out = $repo }
if (-not $Python) { $Python = $env:OTTISK_PYTHON }
if (-not $Python) { $Python = 'C:\Tools\venv-test\Scripts\python.exe' }

& $Python -X utf8 -I (Join-Path $gen 'make_flat_forms.py') --out $Out
if ($LASTEXITCODE -ne 0) { throw "make_flat_forms.py: код $LASTEXITCODE" }
