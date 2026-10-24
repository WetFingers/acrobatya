// unit.docmdp_write — сертифицирующая подпись (план 4.5, п. 3; эталон 3.8 «Инструмент», «Граничные случаи»;
// sign-16): SignRequest::certify пишет /Reference DocMDP в /V и /Perms /DocMDP каталога в той же секции; P=2
// + заполнение поля — действительна с разрешёнными изменениями, + комментарий — недействительна (правки
// напрямую MuPDF на forms-all.pdf, как в 4.4); P=3 + комментарий — разрешено; P=1 + поле — нарушение, + метка
// документа — нет; вторая сертификация и сертификация подписанного — отказ до записи; утверждающая подпись
// после P=2 — разрешена; невидимая сертификация; правила mdpAllows.
#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/sign/doc_timestamp.h"
#include "core/sign/docmdp.h"
#include "core/sign/signer.h"
#include "core/sign/trust_store.h"
#include "core/sign/verifier.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "support/pki.h"
#include "support/sign_support.h"
#include "support/test_pdf.h"
#include "support/verify_support.h"

extern "C" {
#include <mupdf/pdf.h>
}

namespace fs = std::filesystem;
using ot::Document;
using ot::sign::MdpAction;
using ot::sign::MdpPermission;
using ot::sign::SignError;
using ot::sign::SignRequest;
using ot::sign::SignStatus;
using ot::sign::TrustStore;
using ot::sign::Verdict;

namespace {

TrustStore trust() {
    TrustStore t;
    const std::string s = ot::test::readPki("root.cer");
    t.add(ot::sign::Bytes(s.begin(), s.end()), ot::sign::kTrustSignatures | ot::sign::kTrustCertification);
    return t;
}

fs::path formsCopy(const fs::path& dir, const std::wstring& name) {
    return ot::test::copyFile(ot::test::sourceDir() / L"tests" / L"fixtures" / L"forms" / L"forms-all.pdf",
                              dir / name);
}

SignRequest certifyRequest(MdpPermission p) {
    SignRequest r;
    r.identity = ot::test::pkiIdentity();
    r.certify = p;
    r.placement.rect = ot::RectF{36, 36, 236, 96};
    return r;
}

ot::sign::SignResult sign(const fs::path& file, const SignRequest& r) {
    auto doc = Document::openOrThrow(file);
    return ot::sign::signDocument(*doc, r);
}

ot::sign::DocumentVerification verify(const fs::path& file) {
    const TrustStore t = trust();
    const auto offline = ot::test::offlineFetcher();
    ot::sign::VerifyOptions vo;
    vo.trust = &t;
    vo.fetcher = offline.get();
    auto doc = Document::openOrThrow(file);
    return ot::sign::verifyDocument(*doc, vo);
}

const ot::sign::SignatureInfo& certification(const ot::sign::DocumentVerification& v) {
    for (const ot::sign::SignatureInfo& s : v.signatures) {
        if (s.kind == ot::sign::SignatureKind::Certification) {
            return s;
        }
    }
    throw std::runtime_error("no certification signature");
}

}  // namespace

// P=2: словарь /V и каталог; заполнение поля — разрешено, комментарий — нарушение.
TEST(docmdp_write, p2_fill_allowed_note_breaks) {
    OT_FEATURE("sign-16");
    ot::test::TempDir dir;
    const fs::path file = formsCopy(dir.path(), L"p2.pdf");
    const ot::sign::SignResult r = sign(file, certifyRequest(MdpPermission::FormFilling));
    EXPECT_TRUE(r.visible);
    {
        auto doc = Document::openOrThrow(file);
        const ot::sign::DocMdp mdp = ot::sign::readDocMdp(*doc);
        EXPECT_EQ(mdp.permission, MdpPermission::FormFilling);
        EXPECT_EQ(mdp.field, r.field);
        fz_context* ctx = doc->context().get();
        std::string method;
        std::string version;
        ot::mu::call(ctx, [&] {
            pdf_obj* v = pdf_dict_getp(ctx, pdf_trailer(ctx, doc->pdf()), "Root/Perms/DocMDP");
            pdf_obj* ref = pdf_array_get(ctx, pdf_dict_get(ctx, v, PDF_NAME(Reference)), 0);
            method = pdf_to_name(ctx, pdf_dict_get(ctx, ref, PDF_NAME(TransformMethod)));
            version = pdf_to_name(ctx, pdf_dict_getp(ctx, ref, "TransformParams/V"));
        });
        EXPECT_EQ(method, "DocMDP");
        EXPECT_EQ(version, "1.2");
    }
    const fs::path base = dir.path() / L"base.pdf";
    fs::copy_file(file, base);
    {
        const ot::sign::DocumentVerification v = verify(file);
        EXPECT_EQ(v.status, ot::sign::DocumentStatus::Certified);
        EXPECT_EQ(certification(v).verdict, Verdict::Valid);
        EXPECT_EQ(certification(v).docMdp, MdpPermission::FormFilling);
    }
    ot::test::editIncrementally(file, [](fz_context* ctx, pdf_document*, pdf_page* page) {
        ot::test::setTextField(ctx, page, "name", "Petr Ivanov");
    });
    EXPECT_EQ(certification(verify(file)).verdict, Verdict::ValidWithChanges);

    ot::test::editIncrementally(
        base, [](fz_context* ctx, pdf_document*, pdf_page* page) { ot::test::addTextNote(ctx, page); });
    const ot::sign::DocumentVerification noted = verify(base);
    const ot::sign::SignatureInfo& s = certification(noted);
    EXPECT_EQ(s.verdict, Verdict::Invalid);
    std::string why;
    for (const auto& f : s.findings) {
        why += std::string(ot::sign::issueName(f.issue)) + ": " + f.detail + "; ";
    }
    EXPECT_TRUE(s.has(ot::sign::Issue::DisallowedChanges)) << why;
}

// P=3: комментарий разрешён.
TEST(docmdp_write, p3_note_allowed) {
    OT_FEATURE("sign-16");
    ot::test::TempDir dir;
    const fs::path file = formsCopy(dir.path(), L"p3.pdf");
    (void)sign(file, certifyRequest(MdpPermission::Annotations));
    ot::test::editIncrementally(
        file, [](fz_context* ctx, pdf_document*, pdf_page* page) { ot::test::addTextNote(ctx, page); });
    const ot::sign::DocumentVerification v = verify(file);
    EXPECT_EQ(certification(v).docMdp, MdpPermission::Annotations);
    EXPECT_EQ(certification(v).verdict, Verdict::ValidWithChanges);
}

// P=1: заполнение поля — нарушение; метка времени документа — не изменение.
TEST(docmdp_write, p1_field_breaks_timestamp_does_not) {
    OT_FEATURE("sign-16");
    ot::test::TempDir dir;
    const fs::path file = formsCopy(dir.path(), L"p1.pdf");
    (void)sign(file, certifyRequest(MdpPermission::NoChanges));
    const fs::path filled = dir.path() / L"filled.pdf";
    fs::copy_file(file, filled);
    {
        const ot::test::LocalTsa tsa = ot::test::localTsa();
        auto doc = Document::openOrThrow(file);
        (void)ot::sign::addDocumentTimestamp(*doc, tsa.client);
    }
    EXPECT_EQ(certification(verify(file)).verdict, Verdict::Valid);
    ot::test::editIncrementally(filled, [](fz_context* ctx, pdf_document*, pdf_page* page) {
        ot::test::setTextField(ctx, page, "name", "Petr Ivanov");
    });
    EXPECT_EQ(certification(verify(filled)).verdict, Verdict::Invalid);
}

// Сертифицировать можно только документ без подписей: вторая сертификация и сертификация подписанного — отказ
// до записи; утверждающая подпись после P=2 — разрешена, сертификация остаётся действительной.
TEST(docmdp_write, second_certification_refused) {
    OT_FEATURE("sign-16");
    ot::test::TempDir dir;
    const fs::path file = formsCopy(dir.path(), L"дважды.pdf");
    (void)sign(file, certifyRequest(MdpPermission::FormFilling));
    {
        auto doc = Document::openOrThrow(file);
        EXPECT_EQ(ot::sign::certifyBlock(*doc), ot::sign::CertifyBlock::Signed);
    }
    const std::string before = ot::paths::readFile(file);
    try {
        (void)sign(file, certifyRequest(MdpPermission::Annotations));
        ADD_FAILURE() << "вторая сертификация";
    } catch (const SignError& e) {
        EXPECT_EQ(e.status(), SignStatus::CannotCertify);
    }
    EXPECT_EQ(ot::paths::readFile(file), before);

    SignRequest approval;
    approval.identity = ot::test::pkiIdentity();
    approval.placement.rect = ot::RectF{300, 36, 500, 96};
    (void)sign(file, approval);
    const ot::sign::DocumentVerification v = verify(file);
    ASSERT_EQ(v.signatures.size(), 2u);
    EXPECT_EQ(certification(v).verdict, Verdict::ValidWithChanges);
    EXPECT_EQ(v.status, ot::sign::DocumentStatus::Certified);

    const fs::path signedFile = ot::test::unsignedCopy(dir.path(), L"подписан.pdf");
    (void)sign(signedFile, approval);
    {
        auto doc = Document::openOrThrow(signedFile);
        EXPECT_EQ(ot::sign::certifyBlock(*doc), ot::sign::CertifyBlock::Signed);
    }
    EXPECT_THROW((void)sign(signedFile, certifyRequest(MdpPermission::NoChanges)), SignError);
    {
        auto doc = Document::openOrThrow(formsCopy(dir.path(), L"чистый.pdf"));
        EXPECT_EQ(ot::sign::certifyBlock(*doc), ot::sign::CertifyBlock::None);
    }
}

// Невидимая сертификация (/Rect [0 0 0 0]); с блокировкой — DocMDP первым в /Reference, FieldMDP после.
TEST(docmdp_write, invisible_and_locked) {
    OT_FEATURE("sign-16");
    ot::test::TempDir dir;
    const fs::path file = formsCopy(dir.path(), L"невидимая.pdf");
    SignRequest r = certifyRequest(MdpPermission::FormFilling);
    r.placement.rect = ot::RectF{};
    r.lock = true;
    const ot::sign::SignResult res = sign(file, r);
    EXPECT_FALSE(res.visible);
    auto doc = Document::openOrThrow(file);
    fz_context* ctx = doc->context().get();
    std::string first;
    std::string second;
    int refs = 0;
    ot::mu::call(ctx, [&] {
        pdf_obj* arr = pdf_dict_getp(ctx, pdf_trailer(ctx, doc->pdf()), "Root/Perms/DocMDP/Reference");
        refs = pdf_array_len(ctx, arr);
        first = pdf_to_name(ctx, pdf_dict_get(ctx, pdf_array_get(ctx, arr, 0), PDF_NAME(TransformMethod)));
        second = pdf_to_name(ctx, pdf_dict_get(ctx, pdf_array_get(ctx, arr, 1), PDF_NAME(TransformMethod)));
    });
    EXPECT_EQ(refs, 2);
    EXPECT_EQ(first, "DocMDP");
    EXPECT_EQ(second, "FieldMDP");
    EXPECT_EQ(ot::sign::readDocMdp(*doc).permission, MdpPermission::FormFilling);
}

// Правила для интерфейса: что можно делать с сертифицированным документом.
TEST(docmdp_write, rules) {
    OT_FEATURE("sign-16");
    using ot::sign::mdpAllows;
    EXPECT_TRUE(mdpAllows(MdpPermission::NoChanges, MdpAction::AddValidationInfo));
    EXPECT_FALSE(mdpAllows(MdpPermission::NoChanges, MdpAction::FillForms));
    EXPECT_FALSE(mdpAllows(MdpPermission::NoChanges, MdpAction::Sign));
    EXPECT_TRUE(mdpAllows(MdpPermission::FormFilling, MdpAction::FillForms));
    EXPECT_TRUE(mdpAllows(MdpPermission::FormFilling, MdpAction::Sign));
    EXPECT_FALSE(mdpAllows(MdpPermission::FormFilling, MdpAction::Annotate));
    EXPECT_TRUE(mdpAllows(MdpPermission::Annotations, MdpAction::Annotate));
    EXPECT_FALSE(mdpAllows(MdpPermission::Annotations, MdpAction::EditPages));
    EXPECT_FALSE(mdpAllows(MdpPermission::Annotations, MdpAction::EditContent));
    EXPECT_TRUE(mdpAllows(MdpPermission::None, MdpAction::EditPages));
}
