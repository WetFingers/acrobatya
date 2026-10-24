// engine.sign.matrix — регрессия крипто-ядра подписей (план 4.3, п. 4; эталон 3.8 «Acrobat» — внешняя
// проверка openssl cms -verify -cades и pyHanko; 11 риск 2; sign-18 — совместимость форматов). Матрица
// «RSA/ECDSA × .pfx/CNG × B-B/B-T» и ещё B-B adbe.pkcs7.detached — 12 файлов в
// build/<пресет>/sign-matrix/{bb,bt}. Каждый файл: своя CMS_verify с цепочкой до корня PKI, внешний openssl
// cms -verify -cades по /ByteRange, pyHanko (tools/compat_check.py: INTACT, TRUSTED, UNTOUCHED; у B-T — ещё
// TIMESTAMPED, TIMESTAMP_TRUSTED; там же qpdf, pdfcpu strict, mutool, pdfium). Пары .pfx ↔ CNG: тот же набор
// и порядок signed-атрибутов, ESS и content-type, алгоритмы и сертификаты; OpenSSL-путь над хэшем /ByteRange
// CNG-файла даёт те же signed-атрибуты (RSA — и ту же подпись). CNG — PFX-импорт тестовой PKI в памяти
// процесса, без окон. sign_live.DISABLED_windows_my — живая подпись сертификатом «Личное»
// (OTTISK_LIVE_CERT=<отпечаток>, PIN спрашивает Windows) — только человек на экране.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include "core/doc/document.h"
#include "core/sign/cms.h"
#include "core/sign/signer.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "support/pki.h"
#include "support/sign_support.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;
using ot::Document;
using ot::sign::Bytes;
using ot::sign::CmsInfo;
using ot::sign::SignRequest;

namespace {

enum class Mode { Bb, Bt, Pkcs7 };

struct Cell {
    std::string key;  // rsa | ec
    bool cng = false;
    Mode mode = Mode::Bb;
    fs::path file;
};

const char* modeName(Mode m) {
    switch (m) {
        case Mode::Bt:
            return "bt";
        case Mode::Pkcs7:
            return "pkcs7";
        case Mode::Bb:
        default:
            return "bb";
    }
}

SignRequest request(const Cell& c, const ot::test::LocalTsa& tsa) {
    const std::string pfx = "signer-" + c.key + ".pfx";
    SignRequest r;
    if (c.cng) {
        r.identity = ot::test::windowsIdentity(pfx.c_str());
    } else {
        r.identity = ot::test::pkiIdentity(pfx.c_str());
    }
    r.reason = "Матрица подписей";
    r.location = "Москва";
    r.placement.rect = ot::RectF{72, 72, 272, 132};
    r.signingTime = 1793088000;  // 2026-10-27T08:00:00Z
    if (c.mode == Mode::Bt) {
        r.timestamp = tsa.client;
    }
    if (c.mode == Mode::Pkcs7) {
        r.subFilter = ot::sign::SubFilter::Pkcs7Detached;
    }
    return r;
}

// Подписанные байты (/ByteRange) отдельным файлом — для openssl cms -verify -content.
std::string rangeBytes(const fs::path& file, const std::vector<ot::sign::ByteRange>& ranges) {
    const std::string all = ot::paths::readFile(file);
    std::string out;
    for (const auto& r : ranges) {
        out.append(all, static_cast<std::size_t>(r.offset), static_cast<std::size_t>(r.length));
    }
    return out;
}

std::vector<std::string> attributeOids(const CmsInfo& info) {
    std::vector<std::string> out;
    for (const auto& a : info.signedAttributes) {
        out.push_back(a.oid);
    }
    return out;
}

std::vector<Bytes> certificateSet(const CmsInfo& info) {
    std::vector<Bytes> out;
    for (const auto& c : info.certificates) {
        out.push_back(c.der);
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace

TEST(sign, matrix) {
    OT_FEATURE("sign-18");
    OT_FEATURE("sign-08");
    const fs::path root = fs::path(ot::paths::fromUtf8(OTTISK_BINARY_DIR)) / L"sign-matrix";
    const fs::path bb = root / L"bb";
    const fs::path bt = root / L"bt";
    fs::remove_all(root);
    fs::create_directories(bb);
    fs::create_directories(bt);

    const ot::test::LocalTsa tsa = ot::test::localTsa();
    std::vector<Cell> cells;
    for (const char* key : {"rsa", "ec"}) {
        for (const bool cng : {false, true}) {
            for (const Mode mode : {Mode::Bb, Mode::Bt, Mode::Pkcs7}) {
                Cell c;
                c.key = key;
                c.cng = cng;
                c.mode = mode;
                const std::string name = c.key + (cng ? "-cng-" : "-pfx-") + modeName(mode) + ".pdf";
                c.file = ot::test::unsignedCopy(mode == Mode::Bt ? bt : bb, ot::paths::widen(name));
                auto doc = Document::openOrThrow(c.file);
                const ot::sign::SignResult result = ot::sign::signDocument(*doc, request(c, tsa));
                EXPECT_EQ(result.timestamped, mode == Mode::Bt) << name;
                cells.push_back(std::move(c));
            }
        }
    }
    ASSERT_EQ(cells.size(), 12u);

    const auto openssl = ot::test::opensslExe();
    const ot::test::TempDir tmp;
    for (const Cell& c : cells) {
        SCOPED_TRACE(ot::paths::toUtf8(c.file.filename()));
        const ot::test::DocView view = ot::test::readSignatures(c.file);
        ASSERT_EQ(view.sigs.size(), 1u);
        const ot::test::SigView& s = view.sigs.front();
        ot::test::expectCmsValid(c.file, s);
        EXPECT_TRUE(ot::test::coversFile(c.file, s));
        EXPECT_EQ(s.subFilter, c.mode == Mode::Pkcs7 ? "adbe.pkcs7.detached" : "ETSI.CAdES.detached");
        const CmsInfo info = ot::sign::parseCms(s.contents);
        EXPECT_EQ(info.timestampToken.empty(), c.mode != Mode::Bt);
        if (!openssl) {
            continue;
        }
        const fs::path sig = tmp.path() / L"sig.p7s";
        const fs::path content = tmp.path() / L"content.bin";
        const fs::path out = tmp.path() / L"out.bin";
        ot::paths::writeFile(
            sig, std::string_view(reinterpret_cast<const char*>(s.contents.data()), s.contents.size()));
        ot::paths::writeFile(content, rangeBytes(c.file, s.ranges));
        const auto r =
            ot::test::runProcess(*openssl, {L"cms", L"-verify", L"-cades", L"-binary", L"-inform", L"DER",
                                            L"-in", sig.wstring(), L"-content", content.wstring(), L"-CAfile",
                                            (ot::test::pkiDir() / "root.pem").wstring(), L"-purpose",
                                            L"smimesign", L"-out", out.wstring()});
        EXPECT_EQ(r.exitCode, 0) << r.output;
    }

    // Пары .pfx ↔ CNG: один формат.
    for (const Cell& c : cells) {
        if (!c.cng) {
            continue;
        }
        SCOPED_TRACE(ot::paths::toUtf8(c.file.filename()));
        const auto twin = std::find_if(cells.begin(), cells.end(), [&](const Cell& o) {
            return !o.cng && o.key == c.key && o.mode == c.mode;
        });
        ASSERT_NE(twin, cells.end());
        const ot::test::SigView ours = ot::test::readSignatures(c.file).sigs.front();
        const CmsInfo a = ot::sign::parseCms(ours.contents);
        const CmsInfo b = ot::sign::parseCms(ot::test::readSignatures(twin->file).sigs.front().contents);
        EXPECT_EQ(attributeOids(a), attributeOids(b));
        ASSERT_NE(a.signedAttribute(ot::sign::oid::kSigningCertificateV2), nullptr);
        EXPECT_EQ(a.signedAttribute(ot::sign::oid::kSigningCertificateV2)->value,
                  b.signedAttribute(ot::sign::oid::kSigningCertificateV2)->value);
        EXPECT_EQ(a.signedAttribute(ot::sign::oid::kContentType)->value,
                  b.signedAttribute(ot::sign::oid::kContentType)->value);
        EXPECT_EQ(a.signedAttributesDer.size(), b.signedAttributesDer.size());
        EXPECT_EQ(a.digestAlgorithm, b.digestAlgorithm);
        EXPECT_EQ(a.signatureAlgorithm, b.signatureAlgorithm);
        EXPECT_EQ(a.signedDataVersion, b.signedDataVersion);
        EXPECT_EQ(a.signerVersion, b.signerVersion);
        EXPECT_EQ(certificateSet(a), certificateSet(b));
        // OpenSSL-путь над хэшем /ByteRange этого файла.
        const std::string pfx = "signer-" + c.key + ".pfx";
        ot::sign::CmsBuilder reference(*ot::test::pkiIdentity(pfx.c_str()));
        const CmsInfo ref =
            ot::sign::parseCms(reference.buildFromDigest(ot::sign::ossl::sha256Ranges(c.file, ours.ranges)));
        EXPECT_EQ(a.signedAttributesDer, ref.signedAttributesDer);
        if (c.key == "rsa") {
            EXPECT_EQ(a.signature, ref.signature);
        }
    }

    // pyHanko и валидаторы PDF.
    const char* python = std::getenv("OTTISK_PYTHON");  // NOLINT(concurrency-mt-unsafe)
    if (python == nullptr || *python == '\0') {
        std::cout << "sign-matrix: OTTISK_PYTHON not set, pyHanko skipped\n";
        return;
    }
    const fs::path script = ot::test::sourceDir() / L"tools" / L"compat_check.py";
    const auto check = [&](const fs::path& dir, const wchar_t* expect) {
        const auto r = ot::test::runProcess(
            ot::paths::fromUtf8(python),
            {L"-X", L"utf8", L"-I", script.wstring(), L"--quiet", L"--trust",
             (ot::test::pkiDir() / "root.pem").wstring(), L"--expect-sig", expect, dir.wstring()},
            600000);
        if (r.exitCode == 77) {
            std::cout << "sign-matrix: compat_check skipped: " << r.output << "\n";
            return;
        }
        EXPECT_EQ(r.exitCode, 0) << r.output;
    };
    check(bb, L"INTACT,TRUSTED,UNTOUCHED");
    check(bt, L"INTACT,TRUSTED,UNTOUCHED,TIMESTAMPED,TIMESTAMP_TRUSTED");
    std::cout << "sign-matrix: 12 files in " << ot::paths::toUtf8(root) << "\n";
}

// Живая подпись сертификатом из Cert:\CurrentUser\My (план 4.3, п. 4 — «Нужна ручная проверка»): только
// человек. $env:OTTISK_LIVE_CERT='<отпечаток>'; build\debug\bin\engine_tests.exe
// --gtest_also_run_disabled_tests
// --gtest_filter=sign_live.* — PIN (если нужен) спрашивает Windows; итог —
// build\debug\compat\sign\live\my-cert.pdf.
TEST(sign_live, DISABLED_windows_my) {
    const char* thumbprint = std::getenv("OTTISK_LIVE_CERT");  // NOLINT(concurrency-mt-unsafe)
    if (thumbprint == nullptr || *thumbprint == '\0') {
        GTEST_SKIP() << "OTTISK_LIVE_CERT is not set (certificate thumbprint from Cert:\\CurrentUser\\My)";
    }
    const fs::path out = fs::path(ot::paths::fromUtf8(OTTISK_BINARY_DIR)) / L"compat" / L"sign" / L"live";
    fs::create_directories(out);
    const fs::path file = ot::test::unsignedCopy(out, L"my-cert.pdf");
    ot::sign::WindowsCert cert;
    cert.thumbprint = thumbprint;
    SignRequest r;
    r.identity = cert;
    r.reason = "Проверка подписи из хранилища Windows";
    r.placement.rect = ot::RectF{72, 72, 272, 132};
    auto doc = Document::openOrThrow(file);
    const ot::sign::SignResult result = ot::sign::signDocument(*doc, r);
    doc.reset();
    const ot::test::DocView view = ot::test::readSignatures(file);
    ASSERT_EQ(view.sigs.size(), 1u);
    auto bio = ot::sign::ossl::openRangeBio(file, view.sigs.front().ranges);
    std::string error;
    EXPECT_TRUE(ot::sign::cmsVerifyDetached(view.sigs.front().contents, bio.get(), nullptr, &error)) << error;
    std::cout << "signed " << ot::paths::toUtf8(file) << " (" << result.contentsSize << " bytes CMS)\n";
}
