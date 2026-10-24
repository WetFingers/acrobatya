// unit.verifier_structure — структура и целостность подписей (план 4.4, п. 1; эталон 3.8 «Валидатор»,
// «Граничные случаи»; sign-25 — ядро проверки, sign-27 — данные свойств): свои подписи B-B (CAdES RSA,
// adbe.pkcs7.detached, ECDSA) целы и без якорей — «подписант не доверен»; изменённый байт в подписанном
// диапазоне — «недействительна» (хэш не совпал); signed-атрибуты без message-digest, подмена ESS —
// недействительна; /SubFilter adbe.x509.rsa_sha1 — «не поддерживается»; вторая подпись — у первой «есть
// изменения после подписи»; метка документа — imprint по /ByteRange; MD5 — отказ, SHA-1 — предупреждение;
// файлы EU DSS (неверный /ByteRange, перекрытие, HASH_FAILURE, без сертификата, обёрнутая CMS, неизвестный
// алгоритм) — по tests/fixtures/sig/verifier_expected.json.
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/doc/document.h"
#include "core/sign/cms.h"
#include "core/sign/doc_timestamp.h"
#include "core/sign/signer.h"
#include "core/sign/verifier.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "support/sign_support.h"
#include "support/test_pdf.h"
#include "support/verify_support.h"

namespace fs = std::filesystem;
using nlohmann::json;
using ot::Document;
using ot::sign::Bytes;
using ot::sign::DocumentStatus;
using ot::sign::DocumentVerification;
using ot::sign::Issue;
using ot::sign::SignatureInfo;
using ot::sign::SignatureKind;
using ot::sign::SignRequest;
using ot::sign::Verdict;

namespace {

SignRequest bbRequest(const char* pfx = "signer-rsa.pfx") {
    SignRequest r;
    r.identity = ot::test::pkiIdentity(pfx);
    r.reason = "Согласовано";
    r.location = "Москва";
    return r;
}

fs::path signedCopy(const fs::path& dir, const std::wstring& name, const SignRequest& request) {
    const fs::path path = ot::test::unsignedCopy(dir, name);
    auto doc = Document::openOrThrow(path);
    (void)ot::sign::signDocument(*doc, request);
    return path;
}

DocumentVerification verifyFile(const fs::path& path) {
    auto doc = Document::openOrThrow(path);
    return ot::sign::verifyDocument(*doc);
}

// Байты DER: первое вхождение pattern заменить на replacement той же длины.
void replaceInDer(Bytes& der, const Bytes& pattern, const Bytes& replacement) {
    const auto it = std::search(der.begin(), der.end(), pattern.begin(), pattern.end());
    ASSERT_NE(it, der.end());
    std::copy(replacement.begin(), replacement.end(), it);
}

}  // namespace

TEST(verifier_structure, own_bb_is_intact_and_untrusted_without_anchors) {
    OT_FEATURE("sign-25");
    OT_FEATURE("sign-27");
    ot::test::TempDir dir;
    const fs::path path = signedCopy(dir.path(), L"bb.pdf", bbRequest());
    const DocumentVerification v = verifyFile(path);
    ASSERT_EQ(v.signatures.size(), 1u);
    EXPECT_EQ(v.versions, 2);
    const SignatureInfo& s = v.signatures[0];
    EXPECT_EQ(s.field, "Signature1");
    EXPECT_EQ(s.kind, SignatureKind::Approval);
    EXPECT_EQ(s.type, "Sig");
    EXPECT_EQ(s.filter, "Adobe.PPKLite");
    EXPECT_EQ(s.subFilter, "ETSI.CAdES.detached");
    EXPECT_EQ(s.reason, "Согласовано");
    EXPECT_EQ(s.location, "Москва");
    EXPECT_FALSE(s.claimedTime.empty());
    ASSERT_EQ(s.byteRange.size(), 2u);
    EXPECT_EQ(s.byteRange[0].offset, 0);
    EXPECT_TRUE(s.coversWholeFile);
    EXPECT_EQ(s.rangeEnd, s.fileSize);
    EXPECT_EQ(s.revision, 2);
    EXPECT_GT(s.contentsSize, 1000u);
    EXPECT_LT(s.contentsSize, ot::sign::PdfSigner::kMaxContentsSize);
    EXPECT_EQ(s.digestAlgorithm, ot::sign::oid::kSha256);
    EXPECT_TRUE(s.signedAttributes);
    EXPECT_TRUE(s.ess);
    ASSERT_TRUE(s.signer.has_value());
    EXPECT_EQ(s.signer->commonName, "АкроБатя тест");
    EXPECT_GE(s.certificates.size(), 2u);
    EXPECT_TRUE(s.structureOk);
    EXPECT_TRUE(s.intact);
    EXPECT_TRUE(s.findings.empty() || !s.has(Issue::DigestMismatch));
    EXPECT_EQ(s.verdict, Verdict::ValidUntrusted) << "без якорей подписант не доверен";
    EXPECT_EQ(v.status, DocumentStatus::Problems);
}

TEST(verifier_structure, pkcs7_detached_and_ecdsa_are_read) {
    OT_FEATURE("sign-25");
    ot::test::TempDir dir;
    SignRequest pkcs7 = bbRequest();
    pkcs7.subFilter = ot::sign::SubFilter::Pkcs7Detached;
    const DocumentVerification a = verifyFile(signedCopy(dir.path(), L"pkcs7.pdf", pkcs7));
    ASSERT_EQ(a.signatures.size(), 1u);
    EXPECT_EQ(a.signatures[0].subFilter, "adbe.pkcs7.detached");
    EXPECT_TRUE(a.signatures[0].intact);

    const DocumentVerification b =
        verifyFile(signedCopy(dir.path(), L"ecdsa.pdf", bbRequest("signer-ec.pfx")));
    ASSERT_EQ(b.signatures.size(), 1u);
    EXPECT_TRUE(b.signatures[0].intact);
    EXPECT_EQ(b.signatures[0].signatureAlgorithm, ot::sign::oid::kEcdsaWithSha256);
}

TEST(verifier_structure, changed_byte_in_signed_range_is_invalid) {
    OT_FEATURE("sign-25");
    ot::test::TempDir dir;
    const fs::path path = signedCopy(dir.path(), L"изменён.pdf", bbRequest());
    // Вторая строка файла — двоичный комментарий: байт вне синтаксиса, но в подписанном диапазоне.
    std::string bytes = ot::paths::readFile(path);
    const std::size_t at = bytes.find('\n') + 2;
    ASSERT_GE(static_cast<unsigned char>(bytes[at]), 0x80u);
    bytes[at] = static_cast<char>(bytes[at] ^ 0x01);
    ot::paths::writeFile(path, bytes);
    const DocumentVerification v = verifyFile(path);
    ASSERT_EQ(v.signatures.size(), 1u);
    const SignatureInfo& s = v.signatures[0];
    EXPECT_TRUE(s.structureOk);
    EXPECT_FALSE(s.intact);
    EXPECT_TRUE(s.has(Issue::DigestMismatch));
    EXPECT_EQ(s.verdict, Verdict::Invalid);
    EXPECT_EQ(v.status, DocumentStatus::Invalid);
}

TEST(verifier_structure, attributes_without_message_digest_are_invalid) {
    ot::test::TempDir dir;
    const fs::path path = signedCopy(dir.path(), L"no-md.pdf", bbRequest());
    // OID message-digest 1.2.840.113549.1.9.4 → 1.2.840.113549.1.9.99 (та же длина).
    ot::test::patchContents(path, 0, [](Bytes& der) {
        replaceInDer(der, {0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x09, 0x04},
                     {0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x09, 0x63});
    });
    const DocumentVerification v = verifyFile(path);
    ASSERT_EQ(v.signatures.size(), 1u);
    EXPECT_TRUE(v.signatures[0].has(Issue::NoMessageDigest));
    EXPECT_FALSE(v.signatures[0].intact);
    EXPECT_EQ(v.signatures[0].verdict, Verdict::Invalid);
}

TEST(verifier_structure, ess_of_another_certificate_is_invalid) {
    ot::test::TempDir dir;
    const fs::path path = signedCopy(dir.path(), L"ess.pdf", bbRequest());
    const Bytes der = ot::test::readSignatures(path).sigs.at(0).contents;
    const ot::sign::CmsInfo info = ot::sign::parseCms(der);
    ASSERT_TRUE(info.signingCertificateV2.has_value());
    const Bytes hash = info.signingCertificateV2->certHash;
    Bytes other = hash;
    other[0] ^= 0xff;
    ot::test::patchContents(path, 0, [&](Bytes& d) { replaceInDer(d, hash, other); });
    const DocumentVerification v = verifyFile(path);
    ASSERT_EQ(v.signatures.size(), 1u);
    EXPECT_TRUE(v.signatures[0].has(Issue::EssMismatch));
    EXPECT_EQ(v.signatures[0].verdict, Verdict::Invalid);
}

TEST(verifier_structure, x509_rsa_sha1_is_not_supported) {
    ot::test::TempDir dir;
    const fs::path path = signedCopy(dir.path(), L"x509.pdf", bbRequest());
    ot::test::patchBytes(path, "/ETSI.CAdES.detached", "/adbe.x509.rsa_sha1 ");
    const DocumentVerification v = verifyFile(path);
    ASSERT_EQ(v.signatures.size(), 1u);
    EXPECT_EQ(v.signatures[0].subFilter, "adbe.x509.rsa_sha1");
    EXPECT_TRUE(v.signatures[0].has(Issue::UnsupportedSubFilter));
    EXPECT_FALSE(v.signatures[0].intact);
    EXPECT_EQ(v.signatures[0].verdict, Verdict::Unknown);
    EXPECT_EQ(v.status, DocumentStatus::Problems);
}

TEST(verifier_structure, second_signature_marks_first_as_extended) {
    ot::test::TempDir dir;
    const fs::path path = signedCopy(dir.path(), L"две.pdf", bbRequest());
    {
        auto doc = Document::openOrThrow(path);
        (void)ot::sign::signDocument(*doc, bbRequest("signer-ec.pfx"));
    }
    const DocumentVerification v = verifyFile(path);
    ASSERT_EQ(v.signatures.size(), 2u);
    EXPECT_EQ(v.versions, 3);
    const SignatureInfo& first = v.signatures[0];
    const SignatureInfo& second = v.signatures[1];
    EXPECT_EQ(first.field, "Signature1");
    EXPECT_EQ(second.field, "Signature2");
    EXPECT_TRUE(first.intact);
    EXPECT_TRUE(second.intact);
    EXPECT_FALSE(first.coversWholeFile);
    EXPECT_TRUE(second.coversWholeFile);
    EXPECT_EQ(first.revision, 2);
    EXPECT_EQ(second.revision, 3);
    EXPECT_LT(first.rangeEnd, second.rangeEnd);
}

TEST(verifier_structure, document_timestamp_imprint_covers_byte_range) {
    ot::test::TempDir dir;
    const fs::path path = ot::test::unsignedCopy(dir.path(), L"dts.pdf");
    const ot::test::LocalTsa tsa = ot::test::localTsa();
    {
        auto doc = Document::openOrThrow(path);
        (void)ot::sign::addDocumentTimestamp(*doc, tsa.client);
    }
    DocumentVerification v = verifyFile(path);
    ASSERT_EQ(v.signatures.size(), 1u);
    EXPECT_EQ(v.signatures[0].kind, SignatureKind::DocTimestamp);
    EXPECT_EQ(v.signatures[0].type, "DocTimeStamp");
    EXPECT_EQ(v.signatures[0].subFilter, "ETSI.RFC3161");
    EXPECT_TRUE(v.signatures[0].intact);
    ASSERT_TRUE(v.signatures[0].signer.has_value());
    EXPECT_FALSE(v.signatures[0].signer->commonName.empty());

    std::string bytes = ot::paths::readFile(path);
    const std::size_t at = bytes.find('\n') + 2;
    bytes[at] = static_cast<char>(bytes[at] ^ 0x01);
    ot::paths::writeFile(path, bytes);
    v = verifyFile(path);
    ASSERT_EQ(v.signatures.size(), 1u);
    EXPECT_TRUE(v.signatures[0].has(Issue::DigestMismatch));
    EXPECT_EQ(v.signatures[0].verdict, Verdict::Invalid);
}

TEST(verifier_structure, widget_page_rect_and_visibility) {
    ot::test::TempDir dir;
    SignRequest visible = bbRequest();
    visible.placement.page = 0;
    visible.placement.rect = {72, 400, 272, 460};
    const DocumentVerification a = verifyFile(signedCopy(dir.path(), L"видимая.pdf", visible));
    ASSERT_EQ(a.signatures.size(), 1u);
    EXPECT_EQ(a.signatures[0].page, 0);
    EXPECT_TRUE(a.signatures[0].visible);
    EXPECT_NEAR(a.signatures[0].rect.x0, 72, 0.5);
    EXPECT_NEAR(a.signatures[0].rect.y1, 460, 0.5);

    const DocumentVerification b = verifyFile(signedCopy(dir.path(), L"невидимая.pdf", bbRequest()));
    ASSERT_EQ(b.signatures.size(), 1u);
    EXPECT_FALSE(b.signatures[0].visible);
}

TEST(verifier_structure, algorithm_strength) {
    using ot::sign::AlgorithmStrength;
    EXPECT_EQ(ot::sign::algorithmStrength("1.2.840.113549.2.5"), AlgorithmStrength::Forbidden);    // MD5
    EXPECT_EQ(ot::sign::algorithmStrength("1.2.840.113549.2.2"), AlgorithmStrength::Forbidden);    // MD2
    EXPECT_EQ(ot::sign::algorithmStrength("1.2.840.113549.1.1.4"), AlgorithmStrength::Forbidden);  // md5RSA
    EXPECT_EQ(ot::sign::algorithmStrength(ot::sign::oid::kSha1), AlgorithmStrength::Weak);
    EXPECT_EQ(ot::sign::algorithmStrength("1.2.840.113549.1.1.5"), AlgorithmStrength::Weak);  // sha1RSA
    EXPECT_EQ(ot::sign::algorithmStrength("1.2.840.10045.4.1"), AlgorithmStrength::Weak);     // ecdsa-SHA1
    EXPECT_EQ(ot::sign::algorithmStrength(ot::sign::oid::kSha256), AlgorithmStrength::Ok);
    EXPECT_EQ(ot::sign::algorithmStrength(ot::sign::oid::kRsaPss), AlgorithmStrength::Ok);
    EXPECT_EQ(ot::sign::algorithmStrength(ot::sign::oid::kEcdsaWithSha256), AlgorithmStrength::Ok);
}

TEST(verifier_structure, unsigned_document_and_summary) {
    const DocumentVerification v =
        verifyFile(ot::test::sourceDir() / L"tests" / L"fixtures" / L"sig" / L"unsigned.pdf");
    EXPECT_TRUE(v.signatures.empty());
    EXPECT_EQ(v.status, DocumentStatus::NotSigned);

    auto sig = [](Verdict verdict, SignatureKind kind = SignatureKind::Approval) {
        SignatureInfo s;
        s.verdict = verdict;
        s.kind = kind;
        return s;
    };
    EXPECT_EQ(ot::sign::documentStatus({sig(Verdict::Valid), sig(Verdict::ValidWithChanges)}),
              DocumentStatus::Valid);
    EXPECT_EQ(
        ot::sign::documentStatus({sig(Verdict::Valid, SignatureKind::Certification), sig(Verdict::Valid)}),
        DocumentStatus::Certified);
    EXPECT_EQ(ot::sign::documentStatus({sig(Verdict::Valid), sig(Verdict::ValidUntrusted)}),
              DocumentStatus::Problems);
    EXPECT_EQ(ot::sign::documentStatus({sig(Verdict::Unknown), sig(Verdict::Valid)}),
              DocumentStatus::Problems);
    EXPECT_EQ(ot::sign::documentStatus({sig(Verdict::Unknown), sig(Verdict::Invalid)}),
              DocumentStatus::Invalid);
    EXPECT_EQ(ot::sign::verdictFromFindings({}), Verdict::Valid);
    EXPECT_EQ(ot::sign::verdictFromFindings({{Issue::WeakAlgorithm, ""}}), Verdict::Valid);
    EXPECT_EQ(ot::sign::verdictFromFindings({{Issue::WeakAlgorithm, ""}, {Issue::UnsupportedSubFilter, ""}}),
              Verdict::Unknown);
    EXPECT_EQ(ot::sign::verdictFromFindings({{Issue::DigestMismatch, ""}, {Issue::UnsupportedSubFilter, ""}}),
              Verdict::Invalid);
}

TEST(verifier_structure, stage0_fixture_signed_bb) {
    const DocumentVerification v =
        verifyFile(ot::test::sourceDir() / L"tests" / L"fixtures" / L"sig" / L"signed-bb.pdf");
    ASSERT_EQ(v.signatures.size(), 1u);
    EXPECT_TRUE(v.signatures[0].intact);
    EXPECT_TRUE(v.signatures[0].coversWholeFile);
    EXPECT_EQ(v.signatures[0].claimedTime, "2026-01-01T12:00:00Z");
}

// Файлы EU DSS по ожиданиям JSON.
TEST(verifier_structure, dss_files_by_expectations) {
    OT_FEATURE("sign-25");
    const json& table = ot::test::verifierExpectations()["structure"];
    int checked = 0;
    for (const auto& [name, want] : table.items()) {
        const auto file = ot::test::dssFile(name);
        if (!file) {
            continue;
        }
        ++checked;
        SCOPED_TRACE(name);
        auto doc = Document::openOrThrow(*file);
        const DocumentVerification v = ot::sign::verifyDocument(*doc);
        if (want.is_object()) {
            EXPECT_EQ(v.signatures.size(), want["count"].get<std::size_t>());
            int approval = 0;
            int dts = 0;
            for (const SignatureInfo& s : v.signatures) {
                approval += s.kind != SignatureKind::DocTimestamp;
                dts += s.kind == SignatureKind::DocTimestamp;
                if (want.value("allIntact", false)) {
                    std::string why;
                    for (const ot::sign::Finding& f : s.findings) {
                        why += f.detail + "; ";
                    }
                    EXPECT_TRUE(s.intact) << s.field << ": " << why;
                }
            }
            EXPECT_EQ(approval, want["approval"].get<int>());
            EXPECT_EQ(dts, want["docTimestamps"].get<int>());
            continue;
        }
        std::size_t i = 0;
        for (const json& w : want) {
            const SignatureInfo* s = nullptr;
            if (w.contains("field")) {
                s = ot::test::findSignature(v, w["field"].get<std::string>());
            } else if (i < v.signatures.size()) {
                s = &v.signatures[i];
            }
            ++i;
            ASSERT_NE(s, nullptr) << "нет подписи " << w.dump();
            ot::test::expectSignature(*s, w, name);
        }
    }
    if (checked == 0) {
        GTEST_SKIP() << "нет корпуса dss (C:\\Tools\\ottisk\\corpus\\dss)";
    }
}
