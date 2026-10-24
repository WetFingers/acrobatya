<#
.SYNOPSIS
    Тестовая PKI «АкроБати» (план 4.1, эталон 9.2): tests/fixtures/pki/ — корневой и выпускающий ЦС, подписанты
    RSA и ECDSA, отозванный подписант, служба времени (TSA), OCSP-респондер, .pfx, CRL, OCSP-ответы.

.DESCRIPTION
    powershell -NoProfile -ExecutionPolicy Bypass -File tools\pki\make_test_pki.ps1 [-Out <каталог>] [-OpenSsl <openssl.exe>]

    Что (все сертификаты действуют 2026-01-01 … 2036-01-01, подпись SHA-256, имена — UTF8String):
      root.pem/.key/.cer      корневой ЦС, RSA-4096, «АкроБатя тест — корневой ЦС»;
      inter.pem/.key/.cer     выпускающий ЦС, RSA-3072, pathlen 0, CRL DP и caIssuers на корень;
      signer-rsa.pem/.key     подписант RSA-2048, CN «АкроБатя тест», OU RSA; KU digitalSignature + nonRepudiation,
                              EKU emailProtection + documentSigning (RFC 9336) + Adobe Authentic Documents;
      signer-ec.pem/.key      то же, ECDSA P-256, OU ECDSA;
      revoked.pem/.key        подписант RSA-2048, OU «Отозван» — отозван 2026-06-01 (keyCompromise);
      tsa.pem/.key            служба времени RSA-2048, единственный EKU id-kp-timeStamping — критический (RFC 3161 2.3);
      ocsp.pem/.key           OCSP-респондер выпускающего ЦС RSA-2048, EKU OCSPSigning, id-pkix-ocsp-nocheck;
      chain.pem               inter + root;
      root.crl                CRL корня (пустая), 2026-01-01 … 2036-01-01;
      inter-empty.crl         CRL выпускающего ЦС без отозванных, 2026-01-01 … 2036-01-01;
      inter-revoked.crl       CRL выпускающего ЦС с revoked.pem, 2026-07-01 … 2036-01-01;
      signer-rsa.pfx, signer-ec.pfx   PKCS#12 AES-256-CBC/PBKDF2 + MAC SHA-256 (импортируется в Windows), пароль acrobatya;
      signer-rsa-legacy.pfx   PKCS#12 -legacy (RC2-40 + 3DES, MAC SHA-1), пароль acrobatya;
      signer-rsa-cyr.pfx      PKCS#12 AES, пароль «пароль» (UTF-8);
      ocsp-*.der              ответы OCSP (make_ocsp_fixtures.ps1): good, ec-good, revoked, unknown;
      ca/root, ca/inter       базы openssl ca (index.txt, serial, crlnumber) — для make_ocsp_fixtures.ps1;
      pki.json                состав, пароли, серийные номера и ожидания тестов.
    Адреса отзыва — домен .test (http://pki.acrobatya.test/…): тесты отдают их FakeHttpFetcher, сети нет.
    Ключи — тестовые, не секрет. Ключи случайные (файлы не детерминированы); даты, серийные номера и имена —
    фиксированы. Результат коммитится; пересоздавать без нужды не надо (OCSP-ответы — отдельно, тем же
    make_ocsp_fixtures.ps1).
    openssl — 3.5 из Git for Windows (mingw64 — с провайдером legacy для -legacy) или -OpenSsl/OTTISK_OPENSSL.
    Код выхода 0 — всё создано. Файл в UTF-8 с BOM: Windows PowerShell 5.1 иначе читает кириллицу неверно.
#>
[CmdletBinding()]
param(
    [string]$Out = '',
    [string]$OpenSsl = ''
)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object System.Text.UTF8Encoding $false

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $Out) { $Out = Join-Path $repo 'tests\fixtures\pki' }
$cnf = Join-Path $PSScriptRoot 'pki.cnf'

function Find-OpenSsl([string]$hint) {
    foreach ($c in @($hint, $env:OTTISK_OPENSSL, 'C:\Program Files\Git\mingw64\bin\openssl.exe',
                     'C:\Program Files\Git\usr\bin\openssl.exe')) {
        if ($c -and (Test-Path -LiteralPath $c)) { return (Resolve-Path -LiteralPath $c).Path }
    }
    $cmd = Get-Command openssl -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    throw 'openssl.exe не найден: укажите -OpenSsl или OTTISK_OPENSSL'
}
$script:openssl = Find-OpenSsl $OpenSsl

# Git Bash не трогает аргументы вида /CN=…; openssl на Windows берёт аргументы как UTF-8 только с OPENSSL_WIN32_UTF8.
$env:MSYS_NO_PATHCONV = '1'
$env:OPENSSL_WIN32_UTF8 = '1'

function Invoke-OpenSsl([string[]]$Arguments) {
    # stderr openssl («Using configuration…», «.....+++») — не ошибка PowerShell; решает код возврата.
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $output = & $script:openssl @Arguments 2>&1 | ForEach-Object { "$_" }
    $code = $LASTEXITCODE
    $ErrorActionPreference = $prev
    if ($code -ne 0) {
        $output | ForEach-Object { [Console]::Error.WriteLine($_) }
        throw "openssl $($Arguments -join ' ') — код $code"
    }
    return $output
}

function Write-Text([string]$path, [string]$text) {
    [IO.File]::WriteAllText($path, $text, (New-Object System.Text.UTF8Encoding $false))
}

$version = (Invoke-OpenSsl @('version')) -join ''
if ($version -notmatch 'OpenSSL 3\.([5-9]|\d\d)') { throw "нужен OpenSSL ≥ 3.5, найден: $version" }
Write-Host "openssl: $script:openssl ($version)"

# --- каталоги ----------------------------------------------------------------------------------------------------
if (Test-Path -LiteralPath $Out) { Remove-Item -LiteralPath $Out -Recurse -Force }
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$work = Join-Path ([IO.Path]::GetTempPath()) ('ottisk-pki-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $work | Out-Null
$env:PKI_NEWCERTS = $work
$env:PKI_DN = 'dn_root'
$dbRoot = Join-Path $Out 'ca\root'
$dbInter = Join-Path $Out 'ca\inter'
foreach ($db in @(@($dbRoot, '1000'), @($dbInter, '2000'))) {
    New-Item -ItemType Directory -Force -Path $db[0] | Out-Null
    Write-Text (Join-Path $db[0] 'index.txt') ''
    Write-Text (Join-Path $db[0] 'serial') ($db[1] + "`n")
    Write-Text (Join-Path $db[0] 'crlnumber') "1000`n"
}
$env:PKI_CA_DIR = $dbRoot
Write-Text (Join-Path $Out '.gitattributes') "# Тестовая PKI (tools/pki/make_test_pki.ps1): байты как есть, без CRLF-преобразований`n* -text`n"

$from = '20260101000000Z'
$to = '20360101000000Z'
function P([string]$name) { return (Join-Path $Out $name) }

function New-Key([string]$name, [string]$kind) {
    if ($kind -eq 'ec') {
        Invoke-OpenSsl @('genpkey', '-algorithm', 'EC', '-pkeyopt', 'ec_paramgen_curve:P-256', '-pkeyopt',
                         'ec_param_enc:named_curve', '-out', (P "$name.key")) | Out-Null
    } else {
        Invoke-OpenSsl @('genpkey', '-algorithm', 'RSA', '-pkeyopt', "rsa_keygen_bits:$kind", '-out', (P "$name.key")) | Out-Null
    }
}

# Сертификат name, выпущенный ЦС ca (root|inter) с расширениями ext и именем из секции dn.
function New-Cert([string]$name, [string]$ca, [string]$dn, [string]$ext, [string]$start = $from) {
    $env:PKI_DN = $dn
    $csr = Join-Path $work "$name.csr"
    Invoke-OpenSsl @('req', '-new', '-config', $cnf, '-key', (P "$name.key"), '-sha256', '-out', $csr) | Out-Null
    if ($ca -eq 'root') { $env:PKI_CA_DIR = $dbRoot } else { $env:PKI_CA_DIR = $dbInter }
    Invoke-OpenSsl @('ca', '-config', $cnf, '-batch', '-notext', '-md', 'sha256', '-in', $csr, '-out', (P "$name.pem"),
                     '-cert', (P "$ca.pem"), '-keyfile', (P "$ca.key"), '-extensions', $ext,
                     '-startdate', $start, '-enddate', $to) | Out-Null
}

# --- корень и выпускающий ЦС --------------------------------------------------------------------------------------
New-Key 'root' '4096'
$env:PKI_DN = 'dn_root'
Invoke-OpenSsl @('req', '-x509', '-new', '-config', $cnf, '-key', (P 'root.key'), '-sha256', '-extensions', 'ext_root',
                 '-set_serial', '0x0A01', '-not_before', $from, '-not_after', $to, '-out', (P 'root.pem')) | Out-Null
New-Key 'inter' '3072'
New-Cert 'inter' 'root' 'dn_inter' 'ext_inter'

# --- конечные сертификаты -----------------------------------------------------------------------------------------
New-Key 'signer-rsa' '2048'
New-Cert 'signer-rsa' 'inter' 'dn_signer_rsa' 'ext_signer'
New-Key 'signer-ec' 'ec'
New-Cert 'signer-ec' 'inter' 'dn_signer_ec' 'ext_signer'
New-Key 'revoked' '2048'
New-Cert 'revoked' 'inter' 'dn_revoked' 'ext_signer'
New-Key 'tsa' '2048'
New-Cert 'tsa' 'inter' 'dn_tsa' 'ext_tsa'
New-Key 'ocsp' '2048'
New-Cert 'ocsp' 'inter' 'dn_ocsp' 'ext_ocsp'

$chain = (Get-Content -Raw -LiteralPath (P 'inter.pem')) + (Get-Content -Raw -LiteralPath (P 'root.pem'))
Write-Text (P 'chain.pem') ($chain -replace "`r`n", "`n")
foreach ($n in @('root', 'inter')) {
    Invoke-OpenSsl @('x509', '-in', (P "$n.pem"), '-outform', 'DER', '-out', (P "$n.cer")) | Out-Null
}

# --- CRL и отзыв --------------------------------------------------------------------------------------------------
function New-Crl([string]$ca, [string]$file, [string]$last) {
    if ($ca -eq 'root') { $env:PKI_CA_DIR = $dbRoot } else { $env:PKI_CA_DIR = $dbInter }
    $pem = Join-Path $work "$file.pem"
    Invoke-OpenSsl @('ca', '-config', $cnf, '-gencrl', '-md', 'sha256', '-cert', (P "$ca.pem"), '-keyfile', (P "$ca.key"),
                     '-crlexts', 'ext_crl', '-crl_lastupdate', $last, '-crl_nextupdate', $to, '-out', $pem) | Out-Null
    Invoke-OpenSsl @('crl', '-in', $pem, '-outform', 'DER', '-out', (P $file)) | Out-Null
}
New-Crl 'root' 'root.crl' $from
New-Crl 'inter' 'inter-empty.crl' $from

$env:PKI_CA_DIR = $dbInter
Invoke-OpenSsl @('ca', '-config', $cnf, '-revoke', (P 'revoked.pem'), '-crl_reason', 'keyCompromise',
                 '-cert', (P 'inter.pem'), '-keyfile', (P 'inter.key')) | Out-Null
# Дата отзыва в базе — фиксированная (openssl ca ставит «сейчас»): 2026-06-01, причина keyCompromise.
$index = Join-Path $dbInter 'index.txt'
$lines = [IO.File]::ReadAllLines($index) | ForEach-Object {
    $f = $_ -split "`t"
    if ($f[0] -eq 'R') { $f[2] = '260601000000Z,keyCompromise' }
    $f -join "`t"
}
Write-Text $index (($lines -join "`n") + "`n")
New-Crl 'inter' 'inter-revoked.crl' '20260701000000Z'

# --- PKCS#12 ------------------------------------------------------------------------------------------------------
function New-Pfx([string]$name, [string]$file, [string[]]$extra, [string]$passArg) {
    Invoke-OpenSsl (@('pkcs12', '-export', '-inkey', (P "$name.key"), '-in', (P "$name.pem"), '-certfile', (P 'chain.pem'),
                      '-name', "AcroBatya test $name", '-passout', $passArg, '-out', (P $file)) + $extra) | Out-Null
}
$aes = @('-keypbe', 'AES-256-CBC', '-certpbe', 'AES-256-CBC', '-macalg', 'sha256')
New-Pfx 'signer-rsa' 'signer-rsa.pfx' $aes 'pass:acrobatya'
New-Pfx 'signer-ec' 'signer-ec.pfx' $aes 'pass:acrobatya'
New-Pfx 'signer-rsa' 'signer-rsa-legacy.pfx' @('-legacy') 'pass:acrobatya'
$passFile = Join-Path $work 'pass-cyr.txt'
Write-Text $passFile 'пароль'
New-Pfx 'signer-rsa' 'signer-rsa-cyr.pfx' $aes "file:$passFile"

# --- OCSP-ответы --------------------------------------------------------------------------------------------------
& (Join-Path $PSScriptRoot 'make_ocsp_fixtures.ps1') -Pki $Out -OpenSsl $script:openssl
if (-not $?) { throw 'make_ocsp_fixtures.ps1 упал' }

# --- pki.json -----------------------------------------------------------------------------------------------------
function Serial([string]$name) {
    $s = (Invoke-OpenSsl @('x509', '-in', (P "$name.pem"), '-noout', '-serial')) -join ''
    return ($s -replace '^serial=', '').Trim()
}
$certs = [ordered]@{}
foreach ($c in @(
        @('root', 'root', 'rsa-4096', 'root.key'), @('inter', 'inter', 'rsa-3072', 'inter.key'),
        @('signer-rsa', 'signer', 'rsa-2048', 'signer-rsa.key'), @('signer-ec', 'signer', 'ec-p256', 'signer-ec.key'),
        @('revoked', 'signer', 'rsa-2048', 'revoked.key'), @('tsa', 'tsa', 'rsa-2048', 'tsa.key'),
        @('ocsp', 'ocsp', 'rsa-2048', 'ocsp.key'))) {
    $certs[$c[0]] = [ordered]@{ file = "$($c[0]).pem"; key = $c[3]; role = $c[1]; key_type = $c[2]; serial = (Serial $c[0]) }
}
$certs['root']['subject_cn'] = 'АкроБатя тест — корневой ЦС'
$certs['inter']['subject_cn'] = 'АкроБатя тест — выпускающий ЦС'
$certs['signer-rsa']['subject_cn'] = 'АкроБатя тест'
$certs['signer-ec']['subject_cn'] = 'АкроБатя тест'
$certs['revoked']['subject_cn'] = 'АкроБатя тест'
$certs['tsa']['subject_cn'] = 'АкроБатя тест — служба времени'
$certs['ocsp']['subject_cn'] = 'АкроБатя тест — OCSP'

$json = [ordered]@{
    generator  = 'tools/pki/make_test_pki.ps1'
    openssl    = $version
    not_before = '2026-01-01T00:00:00Z'
    not_after  = '2036-01-01T00:00:00Z'
    certs      = $certs
    chain      = 'chain.pem'
    der        = [ordered]@{ root = 'root.cer'; inter = 'inter.cer' }
    pfx        = [ordered]@{
        'signer-rsa.pfx'        = [ordered]@{ cert = 'signer-rsa'; password = 'acrobatya'; pbe = 'AES-256-CBC'; mac = 'sha256' }
        'signer-ec.pfx'         = [ordered]@{ cert = 'signer-ec'; password = 'acrobatya'; pbe = 'AES-256-CBC'; mac = 'sha256' }
        'signer-rsa-legacy.pfx' = [ordered]@{ cert = 'signer-rsa'; password = 'acrobatya'; pbe = 'RC2-40/3DES'; mac = 'sha1' }
        'signer-rsa-cyr.pfx'    = [ordered]@{ cert = 'signer-rsa'; password = 'пароль'; pbe = 'AES-256-CBC'; mac = 'sha256' }
    }
    crl        = [ordered]@{
        'root.crl'          = [ordered]@{ issuer = 'root'; this_update = '2026-01-01T00:00:00Z'; revoked = @() }
        'inter-empty.crl'   = [ordered]@{ issuer = 'inter'; this_update = '2026-01-01T00:00:00Z'; revoked = @() }
        'inter-revoked.crl' = [ordered]@{ issuer = 'inter'; this_update = '2026-07-01T00:00:00Z'; revoked = @('revoked') }
    }
    revoked    = [ordered]@{ cert = 'revoked'; date = '2026-06-01T00:00:00Z'; reason = 'keyCompromise' }
    ocsp       = [ordered]@{
        'ocsp-good.der'    = [ordered]@{ cert = 'signer-rsa'; status = 'good' }
        'ocsp-ec-good.der' = [ordered]@{ cert = 'signer-ec'; status = 'good' }
        'ocsp-revoked.der' = [ordered]@{ cert = 'revoked'; status = 'revoked' }
        'ocsp-unknown.der' = [ordered]@{ serial = '0BAD'; status = 'unknown' }
    }
    urls       = [ordered]@{
        'http://pki.acrobatya.test/root.crl'  = 'root.crl'
        'http://pki.acrobatya.test/inter.crl' = 'inter-revoked.crl'
        'http://pki.acrobatya.test/root.cer'  = 'root.cer'
        'http://pki.acrobatya.test/inter.cer' = 'inter.cer'
        'http://pki.acrobatya.test/ocsp'      = 'OCSP responder (ocsp.pem/ocsp.key, ca/inter/index.txt)'
    }
    tsa        = [ordered]@{ cert = 'tsa'; config = 'tools/pki/tsa.cnf'; server = 'tools/pki/tsa_serve.py'; policy = '1.2.3.4.1' }
}
Write-Text (P 'pki.json') (($json | ConvertTo-Json -Depth 6) + "`n")

Remove-Item -LiteralPath $work -Recurse -Force
Get-ChildItem -LiteralPath $Out -File | ForEach-Object { '{0,-24} {1,6} B' -f $_.Name, $_.Length } | Write-Host
exit 0
