// unit.changes — изменения после подписи и DocMDP (план 4.4, п. 4; эталон 3.8 «Валидатор», 4.5; sign-25 —
// список изменений для вердикта, sign-16 — проверка): ревизии создаёт тест — сертифицирующая подпись PKI на
// tests/fixtures/forms/forms-all.pdf (этап 0), затем правки напрямую MuPDF (pdf_set_text_field_value,
// pdf_create_annot) и запись инкрементом; core/forms и core/annot не участвуют. P=2: заполненное поле —
// «разрешённые изменения», добавленный комментарий — «недействительна»; P=3: комментарий разрешён; P=1: любое
// изменение кроме метки документа — недействительна; FieldMDP /Lock: правка заблокированного поля —
// недействительна; новая страница — недействительна; вторая подпись — разрешённые изменения; файлы EU DSS.
#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/sign/changes.h"
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
using nlohmann::json;
using ot::Document;
using ot::sign::ChangeKind;
using ot::sign::ChangeLevel;
using ot::sign::DocumentStatus;
using ot::sign::DocumentVerification;
using ot::sign::Issue;
using ot::sign::MdpPermission;
using ot::sign::SignatureInfo;
using ot::sign::SignatureKind;
using ot::sign::TrustStore;
using ot::sign::Verdict;
using ot::sign::VerifyOptions;

namespace {

TrustStore rootTrusted() {
    TrustStore t;
    const std::string root = ot::test::readPki("root.cer");
    t.add(ot::sign::Bytes(root.begin(), root.end()),
          ot::sign::kTrustSignatures | ot::sign::kTrustCertification);
    return t;
}

fs::path formsCopy(const fs::path& dir, const std::wstring& name) {
    return ot::test::copyFile(ot::test::sourceDir() / L"tests" / L"fixtures" / L"forms" / L"forms-all.pdf",
                              dir / name);
}

fs::path certified(const fs::path& dir, const std::wstring& name, int p) {
    const fs::path path = formsCopy(dir, name);
    ot::test::certify(path, p);
    return path;
}

DocumentVerification verify(const fs::path& path) {
    static const TrustStore trust = rootTrusted();
    VerifyOptions options;
    options.trust = &trust;
    options.revocation = ot::sign::RevocationPolicy::Never;
    auto doc = Document::openOrThrow(path);
    return ot::sign::verifyDocument(*doc, options);
}

const SignatureInfo& certification(const DocumentVerification& v) {
    for (const SignatureInfo& s : v.signatures) {
        if (s.kind == SignatureKind::Certification) {
            return s;
        }
    }
    throw std::runtime_error("no certification signature");
}

bool hasChange(const SignatureInfo& s, ChangeKind kind, bool allowed) {
    return std::any_of(s.changes.changes.begin(), s.changes.changes.end(),
                       [&](const ot::sign::Change& c) { return c.kind == kind && c.allowed == allowed; });
}

std::string changesText(const SignatureInfo& s) {
    std::string out;
    for (const ot::sign::Change& c : s.changes.changes) {
        out +=
            std::string(ot::sign::changeKindName(c.kind)) + (c.allowed ? "+" : "-") + " " + c.detail + "; ";
    }
    return out;
}

}  // namespace

TEST(changes, docmdp_is_read_and_unmodified_certification_is_valid) {
    OT_FEATURE("sign-16");
    ot::test::TempDir dir;
    const fs::path path = certified(dir.path(), L"p2.pdf", 2);
    {
        auto doc = Document::openOrThrow(path);
        const ot::sign::DocMdp mdp = ot::sign::readDocMdp(*doc);
        EXPECT_EQ(mdp.permission, MdpPermission::FormFilling);
        EXPECT_EQ(mdp.field, "Certification");
        EXPECT_GT(mdp.signatureObject, 0);
    }
    const DocumentVerification v = verify(path);
    const SignatureInfo& s = certification(v);
    EXPECT_TRUE(s.intact);
    EXPECT_TRUE(s.coversWholeFile);
    EXPECT_EQ(s.docMdp, MdpPermission::FormFilling);
    EXPECT_FALSE(s.changes.modified);
    EXPECT_EQ(s.verdict, Verdict::Valid);
    EXPECT_EQ(v.status, DocumentStatus::Certified);
    EXPECT_EQ(v.docMdp, MdpPermission::FormFilling);
}

TEST(changes, p2_filled_field_is_allowed) {
    OT_FEATURE("sign-16");
    OT_FEATURE("sign-25");
    ot::test::TempDir dir;
    const fs::path path = certified(dir.path(), L"p2-поле.pdf", 2);
    ot::test::editIncrementally(path, [](fz_context* ctx, pdf_document*, pdf_page* page) {
        ot::test::setTextField(ctx, page, "name", "Petr Ivanov");
    });
    const DocumentVerification v = verify(path);
    const SignatureInfo& s = certification(v);
    SCOPED_TRACE(changesText(s));
    EXPECT_TRUE(s.intact);
    EXPECT_FALSE(s.coversWholeFile);
    EXPECT_TRUE(s.changes.modified);
    EXPECT_EQ(s.changes.level, ChangeLevel::FormFilling);
    EXPECT_TRUE(s.changes.allowed);
    EXPECT_TRUE(hasChange(s, ChangeKind::FormField, true));
    const auto it = std::find_if(s.changes.changes.begin(), s.changes.changes.end(),
                                 [](const ot::sign::Change& c) { return c.kind == ChangeKind::FormField; });
    ASSERT_NE(it, s.changes.changes.end());
    EXPECT_EQ(it->field, "name");
    EXPECT_TRUE(s.has(Issue::AllowedChanges));
    EXPECT_FALSE(s.has(Issue::ModifiedAfterSigning));
    EXPECT_EQ(s.verdict, Verdict::ValidWithChanges);
    EXPECT_EQ(v.status, DocumentStatus::Certified);
}

TEST(changes, p2_added_annotation_is_invalid) {
    ot::test::TempDir dir;
    const fs::path path = certified(dir.path(), L"p2-записка.pdf", 2);
    ot::test::editIncrementally(
        path, [](fz_context* ctx, pdf_document*, pdf_page* page) { ot::test::addTextNote(ctx, page); });
    const DocumentVerification v = verify(path);
    const SignatureInfo& s = certification(v);
    SCOPED_TRACE(changesText(s));
    EXPECT_TRUE(s.intact);
    EXPECT_EQ(s.changes.level, ChangeLevel::Annotations);
    EXPECT_FALSE(s.changes.allowed);
    EXPECT_TRUE(hasChange(s, ChangeKind::Annotation, false));
    EXPECT_TRUE(s.has(Issue::DisallowedChanges));
    EXPECT_EQ(s.verdict, Verdict::Invalid);
    EXPECT_EQ(v.status, DocumentStatus::Invalid);
}

TEST(changes, p3_added_annotation_is_allowed) {
    ot::test::TempDir dir;
    const fs::path path = certified(dir.path(), L"p3-записка.pdf", 3);
    ot::test::editIncrementally(
        path, [](fz_context* ctx, pdf_document*, pdf_page* page) { ot::test::addTextNote(ctx, page); });
    const DocumentVerification v = verify(path);
    const SignatureInfo& s = certification(v);
    SCOPED_TRACE(changesText(s));
    EXPECT_EQ(s.docMdp, MdpPermission::Annotations);
    EXPECT_TRUE(s.changes.allowed);
    EXPECT_TRUE(hasChange(s, ChangeKind::Annotation, true));
    EXPECT_EQ(s.verdict, Verdict::ValidWithChanges);
}

TEST(changes, p1_forbids_changes_but_not_document_timestamp) {
    ot::test::TempDir dir;
    const fs::path path = certified(dir.path(), L"p1-метка.pdf", 1);
    const ot::test::LocalTsa tsa = ot::test::localTsa();
    {
        auto doc = Document::openOrThrow(path);
        (void)ot::sign::addDocumentTimestamp(*doc, tsa.client);
    }
    DocumentVerification v = verify(path);
    {
        const SignatureInfo& s = certification(v);
        SCOPED_TRACE(changesText(s));
        EXPECT_TRUE(s.changes.modified);
        EXPECT_EQ(s.changes.level, ChangeLevel::LtvOnly);
        EXPECT_TRUE(s.changes.allowed);
        EXPECT_TRUE(hasChange(s, ChangeKind::DocTimestamp, true));
        EXPECT_FALSE(s.has(Issue::AllowedChanges)) << "метки документа и /DSS — не «изменения»";
        EXPECT_EQ(s.verdict, Verdict::Valid);
    }
    ot::test::editIncrementally(path, [](fz_context* ctx, pdf_document*, pdf_page* page) {
        ot::test::setTextField(ctx, page, "name", "Petr Ivanov");
    });
    v = verify(path);
    const SignatureInfo& s = certification(v);
    SCOPED_TRACE(changesText(s));
    EXPECT_FALSE(s.changes.allowed);
    EXPECT_TRUE(s.has(Issue::DisallowedChanges) || s.has(Issue::LockedFieldChanged));
    EXPECT_EQ(s.verdict, Verdict::Invalid);
}

TEST(changes, locked_field_change_is_invalid) {
    ot::test::TempDir dir;
    const fs::path path = ot::test::makeFormFile(dir.path(), false);
    {
        ot::sign::SignRequest r;
        r.identity = ot::test::pkiIdentity();
        r.lock = true;  // FieldMDP /All — все поля только для чтения
        auto doc = Document::openOrThrow(path);
        (void)ot::sign::signDocument(*doc, r);
    }
    ot::test::editIncrementally(path, [](fz_context* ctx, pdf_document*, pdf_page* page) {
        ot::test::setTextField(ctx, page, "Имя", "Пётр");
    });
    const DocumentVerification v = verify(path);
    ASSERT_EQ(v.signatures.size(), 1u);
    const SignatureInfo& s = v.signatures[0];
    SCOPED_TRACE(changesText(s));
    EXPECT_TRUE(hasChange(s, ChangeKind::LockedField, false));
    EXPECT_TRUE(s.has(Issue::LockedFieldChanged));
    EXPECT_EQ(s.verdict, Verdict::Invalid);
}

TEST(changes, added_page_is_invalid_and_second_signature_is_allowed) {
    ot::test::TempDir dir;
    const fs::path page = ot::test::unsignedCopy(dir.path(), L"страница.pdf");
    {
        ot::sign::SignRequest r;
        r.identity = ot::test::pkiIdentity();
        auto doc = Document::openOrThrow(page);
        (void)ot::sign::signDocument(*doc, r);
    }
    const fs::path second = ot::test::copyFile(page, dir.path() / L"вторая.pdf");
    ot::test::editIncrementally(page, [](fz_context* ctx, pdf_document* pdf, pdf_page*) {
        pdf_obj* resources = pdf_new_dict(ctx, pdf, 1);
        fz_buffer* contents = fz_new_buffer(ctx, 1);
        pdf_obj* p = pdf_add_page(ctx, pdf, fz_make_rect(0, 0, 595, 842), 0, resources, contents);
        pdf_insert_page(ctx, pdf, -1, p);
        pdf_drop_obj(ctx, p);
        fz_drop_buffer(ctx, contents);
        pdf_drop_obj(ctx, resources);
    });
    DocumentVerification v = verify(page);
    ASSERT_EQ(v.signatures.size(), 1u);
    SCOPED_TRACE(changesText(v.signatures[0]));
    EXPECT_EQ(v.signatures[0].changes.level, ChangeLevel::Other);
    EXPECT_TRUE(hasChange(v.signatures[0], ChangeKind::Page, false));
    EXPECT_EQ(v.signatures[0].verdict, Verdict::Invalid);

    {
        ot::sign::SignRequest r;
        r.identity = ot::test::pkiIdentity("signer-ec.pfx");
        auto doc = Document::openOrThrow(second);
        (void)ot::sign::signDocument(*doc, r);
    }
    v = verify(second);
    ASSERT_EQ(v.signatures.size(), 2u);
    SCOPED_TRACE(changesText(v.signatures[0]));
    EXPECT_EQ(v.signatures[0].changes.level, ChangeLevel::FormFilling);
    EXPECT_TRUE(hasChange(v.signatures[0], ChangeKind::Signature, true));
    EXPECT_EQ(v.signatures[0].verdict, Verdict::ValidWithChanges);
    EXPECT_EQ(v.signatures[1].verdict, Verdict::Valid);
    EXPECT_EQ(v.status, DocumentStatus::Valid);
}

TEST(changes, field_lock_rules) {
    using ot::sign::FieldLock;
    const std::vector<FieldLock> all = {{FieldLock::Action::All, {}}};
    EXPECT_TRUE(ot::sign::isFieldLocked(all, "x"));
    const std::vector<FieldLock> include = {{FieldLock::Action::Include, {"a", "b.c"}}};
    EXPECT_TRUE(ot::sign::isFieldLocked(include, "b.c"));
    EXPECT_FALSE(ot::sign::isFieldLocked(include, "b"));
    const std::vector<FieldLock> exclude = {{FieldLock::Action::Exclude, {"a"}}};
    EXPECT_FALSE(ot::sign::isFieldLocked(exclude, "a"));
    EXPECT_TRUE(ot::sign::isFieldLocked(exclude, "z"));
    EXPECT_FALSE(ot::sign::isFieldLocked({}, "z"));
    EXPECT_EQ(ot::sign::permittedLevel(MdpPermission::NoChanges), ChangeLevel::LtvOnly);
    EXPECT_EQ(ot::sign::permittedLevel(MdpPermission::FormFilling), ChangeLevel::FormFilling);
    EXPECT_EQ(ot::sign::permittedLevel(MdpPermission::Annotations), ChangeLevel::Annotations);
    EXPECT_EQ(ot::sign::permittedLevel(MdpPermission::None), ChangeLevel::Annotations);
}

TEST(changes, dss_files_by_expectations) {
    const json& table = ot::test::verifierExpectations()["changes"];
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
        std::size_t i = 0;
        for (const json& w : want) {
            const SignatureInfo* s = nullptr;
            if (w.contains("field")) {
                s = ot::test::findSignature(v, w["field"].get<std::string>());
            } else if (w.contains("kind")) {
                for (const SignatureInfo& x : v.signatures) {
                    if (ot::sign::signatureKindName(x.kind) == w["kind"].get<std::string>()) {
                        s = &x;
                        break;
                    }
                }
            } else if (i < v.signatures.size()) {
                s = &v.signatures[i];
            }
            ++i;
            ASSERT_NE(s, nullptr) << w.dump();
            SCOPED_TRACE(changesText(*s));
            ot::test::expectSignature(*s, w, name);
        }
    }
    if (checked == 0) {
        GTEST_SKIP() << "нет корпуса dss";
    }
}
