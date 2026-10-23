<#
.SYNOPSIS
    Языковые модели Tesseract (tessdata_fast) для OCR «Оттиска»: rus, eng, deu → C:\Tools\tessdata.

.DESCRIPTION
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\get-tessdata.ps1 [-Dest C:\Tools\tessdata] [-VerifyOnly]

    Каждый файл качается в свой пустой каталог C:\Tools\dl\tessdata\<язык>, сверяется размер и sha256
    (зафиксированы ниже, снимок github.com/tesseract-ocr/tessdata_fast от 2026-10-23) и только потом
    переносится в -Dest. Путь назначения — только ASCII: Tesseract открывает traineddata узким fopen.
    -VerifyOnly — ничего не качать, только сверить то, что уже лежит в -Dest. Код выхода 0 — всё совпало.
    Файл в UTF-8 с BOM: Windows PowerShell 5.1 иначе читает кириллицу неверно.
#>
[CmdletBinding()]
param(
    [string]$Dest = 'C:\Tools\tessdata',
    [switch]$VerifyOnly
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding $false

$Base = 'https://github.com/tesseract-ocr/tessdata_fast/raw/main'
$Models = @(
    @{ Lang = 'rus'; Size = 3861738; Sha256 = 'e16e5e036cce1d9ec2b00063cf8b54472625b9e14d893a169e2b0dedeb4df225' },
    @{ Lang = 'eng'; Size = 4113088; Sha256 = '7d4322bd2a7749724879683fc3912cb542f19906c83bcc1a52132556427170b2' },
    @{ Lang = 'deu'; Size = 1525436; Sha256 = '19d219bbb6672c869d20a9636c6816a81eb9a71796cb93ebe0cb1530e2cdb22d' }
)

if ($Dest -match '[^\x00-\x7F]') { throw "Путь tessdata должен быть ASCII (Tesseract открывает его узким fopen): $Dest" }
New-Item -ItemType Directory -Force $Dest | Out-Null
$bad = 0
foreach ($m in $Models) {
    $name = "$($m.Lang).traineddata"
    $target = Join-Path $Dest $name
    if (-not $VerifyOnly) {
        $dl = Join-Path 'C:\Tools\dl\tessdata' $m.Lang
        if (Test-Path $dl) { Remove-Item -LiteralPath $dl -Recurse -Force }
        New-Item -ItemType Directory -Force $dl | Out-Null
        $tmp = Join-Path $dl $name
        & curl.exe -sS -L --fail -o $tmp "$Base/$name"
        if ($LASTEXITCODE -ne 0) { throw "curl ${name}: код $LASTEXITCODE" }
        $len = (Get-Item $tmp).Length
        if ($len -ne $m.Size) { throw "${name}: размер $len, ожидался $($m.Size)" }
        $hash = (Get-FileHash $tmp -Algorithm SHA256).Hash.ToLower()
        if ($m.Sha256 -and $hash -ne $m.Sha256) { throw "${name}: sha256 $hash, ожидался $($m.Sha256)" }
        Move-Item -LiteralPath $tmp -Destination $target -Force
    }
    if (-not (Test-Path $target)) { Write-Host "НЕТ  $target"; $bad++; continue }
    $len = (Get-Item $target).Length
    $hash = (Get-FileHash $target -Algorithm SHA256).Hash.ToLower()
    $ok = ($len -eq $m.Size) -and (-not $m.Sha256 -or $hash -eq $m.Sha256)
    if (-not $ok) { $bad++ }
    Write-Host ("{0}  {1,-18} {2,10:N0} Б  {3}" -f $(if ($ok) { 'OK ' } else { 'ПЛОХО' }), $name, $len, $hash)
}
exit $bad
