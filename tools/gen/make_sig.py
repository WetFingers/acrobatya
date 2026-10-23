# -*- coding: utf-8 -*-
"""make_sig.py — простая подпись PAdES B-B: tests/fixtures/sig/{key.pem, cert.pem, unsigned.pdf, signed-bb.pdf, sig.json}.

Что:
  key.pem   — RSA 2048, PKCS#8 без пароля (тестовый ключ, не секрет);
  cert.pem  — самоподписанный X.509 v3, CN=Оттиск тест, O=Оттиск, C=RU,
              действует 2026-01-01 .. +3650 дней, KeyUsage digitalSignature+nonRepudiation;
  unsigned.pdf — 1 страница и пустое место под подпись;
  signed-bb.pdf — PAdES B-B (ETSI.CAdES.detached, SHA-256, видимое поле Signature1),
              время подписи 2026-01-01T12:00:00Z;
  sig.json  — ожидания (pyhanko sign validate: INTACT, с --trust cert.pem — TRUSTED).
Полная PKI (корень, промежуточный ЦС, TSA, OCSP/CRL, .pfx) — НЕ здесь, сессия 4.1.
Откуда: ключ и сертификат — cryptography (зависимость pyHanko) из детерминированного ГСЧ;
подпись — pyHanko 0.38.0 (API Python, не CLI — нужна фиксированная дата).
Детерминизм: простые числа RSA ищутся в random.Random(seed=1); подпись RSA PKCS#1 v1.5
детерминирована; время подписи и /M — фиксированы подменой datetime в модулях pyHanko;
/ID и прочие случайные байты pyHanko — _genlib.DetRandom(seed).
Запуск: "C:\\Tools\\venv-test\\Scripts\\python.exe" -X utf8 -I tools/gen/make_sig.py [--out <корень>]
"""
import random
import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path

sys.dont_write_bytecode = True                     # без __pycache__ в tools/gen
sys.path.insert(0, str(Path(__file__).resolve().parent))

import _genlib as g  # noqa: E402

E = 65537


def _is_probable_prime(n: int, rng: random.Random, rounds: int = 48) -> bool:
    if n < 4:
        return n in (2, 3)
    for p in (3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47):
        if n % p == 0:
            return n == p
    d, s = n - 1, 0
    while d % 2 == 0:
        d //= 2
        s += 1
    for _ in range(rounds):
        a = rng.randrange(2, n - 2)
        x = pow(a, d, n)
        if x in (1, n - 1):
            continue
        for _ in range(s - 1):
            x = pow(x, 2, n)
            if x == n - 1:
                break
        else:
            return False
    return True


def _prime(rng: random.Random, bits: int) -> int:
    while True:
        c = rng.getrandbits(bits) | (3 << (bits - 2)) | 1
        if (c - 1) % E != 0 and _is_probable_prime(c, rng):
            return c


def det_rsa_key(seed: int, bits: int = 2048):
    from cryptography.hazmat.primitives.asymmetric import rsa
    rng = random.Random(seed)
    while True:
        p = _prime(rng, bits // 2)
        q = _prime(rng, bits // 2)
        if p != q and (p * q).bit_length() == bits:
            break
    if p < q:
        p, q = q, p
    phi = (p - 1) * (q - 1)
    d = pow(E, -1, phi)
    pub = rsa.RSAPublicNumbers(E, p * q)
    priv = rsa.RSAPrivateNumbers(p, q, d, rsa.rsa_crt_dmp1(d, p), rsa.rsa_crt_dmq1(d, q),
                                 rsa.rsa_crt_iqmp(p, q), pub)
    return priv.private_key()


def make_cert(key):
    from cryptography import x509
    from cryptography.hazmat.primitives import hashes
    from cryptography.x509.oid import NameOID
    name = x509.Name([
        x509.NameAttribute(NameOID.COUNTRY_NAME, "RU"),
        x509.NameAttribute(NameOID.ORGANIZATION_NAME, "Оттиск"),
        x509.NameAttribute(NameOID.COMMON_NAME, "Оттиск тест"),
    ])
    nb = datetime(2026, 1, 1, 0, 0, 0, tzinfo=timezone.utc)
    b = (x509.CertificateBuilder().subject_name(name).issuer_name(name).public_key(key.public_key())
         .serial_number(0x07715EA1).not_valid_before(nb).not_valid_after(nb + timedelta(days=3650))
         .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
         .add_extension(x509.KeyUsage(digital_signature=True, content_commitment=True, key_encipherment=False,
                                      data_encipherment=False, key_agreement=False, key_cert_sign=False,
                                      crl_sign=False, encipher_only=False, decipher_only=False), critical=True)
         .add_extension(x509.SubjectKeyIdentifier.from_public_key(key.public_key()), critical=False)
         .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(key.public_key()), critical=False))
    return b.sign(key, hashes.SHA256())


def unsigned_pdf(out: Path):
    doc = g.Doc("Документ для подписи", "tests/fixtures/sig/unsigned.pdf", creator="tools/gen/make_sig.py")
    f = doc.font("arial")
    fb = doc.font("arialbd")
    cv = doc.canvas()
    cv.text(72, 760, "Документ для подписи (PAdES B-B)", fb, 18)
    y = 720
    for par in ("Этот документ подписан тестовым самоподписанным сертификатом «Оттиск тест».",
                "Подпись видимая: прямоугольник внизу страницы. Сертификат лежит рядом: cert.pem.",
                "This document carries a PAdES baseline B-B signature made with pyHanko."):
        y = cv.paragraph(72, y, 450, par, f, 11) - 6
    cv.rect(72, 90, 260, 70, stroke=(0.7, 0.7, 0.75), lw=0.5)
    doc.add_page(cv)
    doc.save(out)


def main():
    args = g.common_args(__doc__)
    root = Path(args.out)
    dst = root / "tests" / "fixtures" / "sig"
    dst.mkdir(parents=True, exist_ok=True)
    from cryptography.hazmat.primitives import serialization
    key = det_rsa_key(g.SEED)
    cert = make_cert(key)
    (dst / "key.pem").write_bytes(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                                    serialization.NoEncryption()))
    (dst / "cert.pem").write_bytes(cert.public_bytes(serialization.Encoding.PEM))
    unsigned = dst / "unsigned.pdf"
    unsigned_pdf(unsigned)

    g.patch_randomness(g.SEED)
    from pyhanko.pdf_utils.incremental_writer import IncrementalPdfFileWriter
    from pyhanko.sign import fields, signers
    from pyhanko.sign.signers import pdf_cms, pdf_signer
    from pyhanko.stamp import TextStampStyle
    import pyhanko.stamp.text  # noqa: F401  (%(ts)s в штампе)
    g.patch_pyhanko_time()                         # /M, /ModDate, xmp:ModifyDate, штамп
    assert pdf_signer.datetime is g.FixedDatetime and pdf_cms.datetime is g.FixedDatetime
    signer = signers.SimpleSigner.load(str(dst / "key.pem"), str(dst / "cert.pem"), key_passphrase=None)
    meta = signers.PdfSignatureMetadata(field_name="Signature1", md_algorithm="sha256",
                                        subfilter=fields.SigSeedSubFilter.PADES,
                                        reason="Тестовая подпись B-B", location="Москва",
                                        name="Оттиск тест")
    style = TextStampStyle(stamp_text="Ottisk test signature (PAdES B-B)\nTime: %(ts)s", border_width=1)
    out = dst / "signed-bb.pdf"
    with open(unsigned, "rb") as inf:
        w = IncrementalPdfFileWriter(inf)
        fields.append_signature_field(w, fields.SigFieldSpec("Signature1", on_page=0, box=(72, 90, 332, 160)))
        with open(out, "wb") as outf:
            signers.PdfSigner(meta, signer=signer, stamp_style=style).sign_pdf(w, output=outf)

    # проверка pyHanko (CLI из venv; нужен UTF-8 — в имени сертификата кириллица)
    import os
    env = dict(os.environ, PYTHONUTF8="1", PYTHONIOENCODING="utf-8")
    exe = Path(sys.executable).parent / "pyhanko.exe"
    r1 = g.run([exe, "sign", "validate", out], check=False, env=env)
    r2 = g.run([exe, "sign", "validate", "--trust", dst / "cert.pem", "--trust-replace", out], check=False, env=env)
    t1 = r1.stdout.decode("utf-8", "replace").strip()
    t2 = r2.stdout.decode("utf-8", "replace").strip()
    if ":INTACT:UNTRUSTED" not in t1 or ":INTACT:TRUSTED" not in t2 or r2.returncode != 0:
        raise RuntimeError("pyhanko sign validate: неожиданно\n%s\n%s\n%s" % (
            t1, t2, r2.stderr.decode("utf-8", "replace")[-600:]))
    g.write_json(dst / "sig.json", {
        "generator": "tools/gen/make_sig.py",
        "key": "key.pem", "cert": "cert.pem",
        "cert_subject": "C=RU, O=Оттиск, CN=Оттиск тест", "cert_serial": "0x07715EA1",
        "cert_valid": ["2026-01-01T00:00:00Z", "2035-12-30T00:00:00Z"],
        "files": {
            "unsigned.pdf": {"pages": 1, "signatures": 0},
            "signed-bb.pdf": {"pages": 1, "signatures": 1, "field": "Signature1", "level": "PAdES B-B",
                              "subfilter": "ETSI.CAdES.detached", "digest": "sha256",
                              "signing_time": "2026-01-01T12:00:00Z", "visible": True,
                              "box": [72, 90, 332, 160],
                              "expect_untrusted": "INTACT:UNTRUSTED (самоподписанный, вне доверия)",
                              "expect_with_trust_cert_pem": "INTACT:TRUSTED",
                              "pyhanko_status_untrusted": t1.split(":", 2)[2],
                              "pyhanko_status_trusted": t2.split(":", 2)[2]},
        },
    })
    g.say("sig: signed-bb.pdf %d байт; pyhanko: %s | с --trust cert.pem: %s" % (
        out.stat().st_size, t1.split(":", 2)[2], t2.split(":", 2)[2]))


if __name__ == "__main__":
    main()
