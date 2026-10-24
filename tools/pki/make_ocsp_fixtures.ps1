<#
.SYNOPSIS
    OCSP-ответы тестовой PKI (план 4.1): tests/fixtures/pki/ocsp-*.der — ответы `openssl ocsp` в роли респондера
    по базе выпускающего ЦС (ca/inter/index.txt).

.DESCRIPTION
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\pki\make_ocsp_fixtures.ps1 [-Pki <каталог>] [-OpenSsl <openssl.exe>]

    Что: ocsp-good.der (signer-rsa — good), ocsp-ec-good.der (signer-ec — good), ocsp-revoked.der (revoked —
    revoked, 2026-06-01, keyCompromise), ocsp-unknown.der (серийный 0x0BAD, которого нет в базе, — unknown).
    Запросы — без nonce, CertID на SHA-1 (как OCSP_cert_to_id по умолчанию); ответы подписаны ocsp.pem
    (делегированный респондер, сертификат внутри ответа), nextUpdate — через 3650 дней, thisUpdate — момент запуска.
    Вызывается из make_test_pki.ps1; отдельно — чтобы обновить ответы без новой PKI.
    Код выхода 0 — всё создано. Файл в UTF-8 с BOM.
#>
[CmdletBinding()]
param(
    [string]$Pki = '',
    [string]$OpenSsl = ''
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding $false

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $Pki) { $Pki = Join-Path $repo 'tests\fixtures\pki' }
$Pki = (Resolve-Path -LiteralPath $Pki).Path
if (-not $OpenSsl) { $OpenSsl = $env:OTTISK_OPENSSL }
if (-not $OpenSsl) { $OpenSsl = 'C:\Program Files\Git\mingw64\bin\openssl.exe' }
if (-not (Test-Path -LiteralPath $OpenSsl)) { throw "openssl.exe не найден: $OpenSsl" }
$env:MSYS_NO_PATHCONV = '1'

function Invoke-OpenSsl([string[]]$Arguments) {
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $output = & $OpenSsl @Arguments 2>&1 | ForEach-Object { "$_" }
    $code = $LASTEXITCODE
    $ErrorActionPreference = $prev
    if ($code -ne 0) {
        $output | ForEach-Object { [Console]::Error.WriteLine($_) }
        throw "openssl $($Arguments -join ' ') — код $code"
    }
    return $output
}

function P([string]$name) { return (Join-Path $Pki $name) }
$work = Join-Path ([IO.Path]::GetTempPath()) ('ottisk-ocsp-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $work | Out-Null
$index = P 'ca\inter\index.txt'

# Ответ на запрос о сертификате (-cert файл) или о серийном номере (-serial).
function New-OcspResponse([string]$file, [string[]]$subject, [string]$expect) {
    $req = Join-Path $work "$file.req"
    Invoke-OpenSsl (@('ocsp', '-issuer', (P 'inter.pem'), '-no_nonce', '-reqout', $req) + $subject) | Out-Null
    Invoke-OpenSsl @('ocsp', '-index', $index, '-rsigner', (P 'ocsp.pem'), '-rkey', (P 'ocsp.key'), '-CA', (P 'inter.pem'),
                     '-reqin', $req, '-respout', (P $file), '-ndays', '3650') | Out-Null
    # Сверка: ответ проверяется по цепочке и даёт ожидаемый статус.
    $text = (Invoke-OpenSsl (@('ocsp', '-respin', (P $file), '-issuer', (P 'inter.pem'), '-CAfile', (P 'chain.pem'),
                                '-no_nonce') + $subject)) -join "`n"
    if ($text -notmatch ": $expect") { throw "${file}: ожидался статус $expect, получено:`n$text" }
    Write-Host "$file — $expect"
}

New-OcspResponse 'ocsp-good.der' @('-cert', (P 'signer-rsa.pem')) 'good'
New-OcspResponse 'ocsp-ec-good.der' @('-cert', (P 'signer-ec.pem')) 'good'
New-OcspResponse 'ocsp-revoked.der' @('-cert', (P 'revoked.pem')) 'revoked'
New-OcspResponse 'ocsp-unknown.der' @('-serial', '0x0BAD') 'unknown'

Remove-Item -LiteralPath $work -Recurse -Force
exit 0
