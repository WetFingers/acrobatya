// unit.signer — подписание PAdES B-B (план 4.2, п. 1; эталон 3.8 «Механика», 4.4, 4.6): .pfx RSA и ECDSA
// P-256 дают подпись, которую CMS_verify (с CAdES и цепочкой до корня PKI) признаёт по /ByteRange
// записанного файла; запись инкрементальная (исходный файл — префикс), qpdf --check = 0; /Contents — ровно
// 16 384 байта резерва; словарь /V: /Filter /Adobe.PPKLite, /SubFilter /ETSI.CAdES.detached (или
// /adbe.pkcs7.detached по «совместимости»), /Reason, /Location, /ContactInfo, /Name, /M, /Prop_Build (и через
// qpdf --json); каталог — /Extensions /ESIC уровня 2 только для CAdES; в CMS нет signing-time, есть ESS
// signing-certificate-v2 и message-digest = SHA-256 /ByteRange. Существующее пустое поле /Sig подписывается
// на месте (forms-17); невидимая подпись — /Rect [0 0 0 0]; две подписи подряд — обе целы; «Заблокировать» —
// поля формы ReadOnly и FieldMDP /All (sign-15); несохранённые правки записываются до подписи; ошибки — до
// правки документа. sign_compat.DISABLED_write_b пишет файлы для compat.sign.b (pyHanko INTACT, UNTOUCHED).
#include <gtest/gtest.h>

#include <QFuture>
#include <QString>

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/doc/revisions.h"
#include "core/doc/saver.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/handles.h"
#include "core/optimize/qpdf_tool.h"
#include "core/sign/cms.h"
#include "core/sign/ossl.h"
#include "core/sign/pkcs12.h"
#include "core/sign/signer.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "services/document_session.h"
#include "support/pki.h"
#include "support/qt_app.h"
#include "support/sign_support.h"
#include "support/test_edits.h"
#include "support/test_pdf.h"

extern "C" {
#include <mupdf/pdf.h>
}

namespace fs = std::filesystem;
using ot::Document;
using ot::sign::Bytes;
using ot::sign::SignError;
using ot::sign::SigningKey;
using ot::sign::SignRequest;
using ot::sign::SignResult;
using ot::sign::SignStatus;
using ot::sign::SubFilter;

using ot::test::copyFile;
using ot::test::coversFile;
using ot::test::DocView;
using ot::test::makeFormFile;
using ot::test::SigView;
using ot::test::unsignedCopy;

namespace {

std::shared_ptr<const SigningKey> identity(const char* pfx) { return ot::test::pkiIdentity(pfx); }
DocView readView(const fs::path& file) { return ot::test::readSignatures(file); }
void expectValid(const fs::path& file, const SigView& s) { ot::test::expectCmsValid(file, s); }

SignRequest request(const char* pfx = "signer-rsa.pfx") {
    SignRequest r;
    r.identity = identity(pfx);
    r.reason = "Согласовано";
    r.location = "Москва";
    r.contact = "test@acrobatya.test";
    r.placement.page = 0;
    r.placement.rect = ot::RectF{72, 72, 272, 132};
    r.signingTime = 1793088000;  // 2026-10-27T08:00:00Z
    return r;
}

nlohmann::json qpdfObject(const nlohmann::json& qpdf, int objnum) {
    const std::string key = "obj:" + std::to_string(objnum) + " 0 R";
    for (const auto& part : qpdf.at("qpdf")) {
        if (part.contains(key)) {
            return part.at(key).at("value");
        }
    }
    return {};
}

}  // namespace

TEST(signer, rsa_bb_dictionary_cms_and_incremental) {
    OT_FEATURE("sign-12");
    OT_FEATURE("sign-14");
    OT_FEATURE("sign-18");
    ot::test::TempDir dir;
    const fs::path path = unsignedCopy(dir.path(), L"договор.pdf");
    const std::string before = ot::paths::readFile(path);
    {
        auto doc = Document::openOrThrow(path);
        const SignResult r = ot::sign::signDocument(*doc, request());
        EXPECT_TRUE(r.save.ok()) << r.save.message;
        EXPECT_EQ(r.save.mode, ot::SaveMode::Incremental);
        EXPECT_EQ(r.field, "Signature1");
        EXPECT_TRUE(r.visible);
        EXPECT_GT(r.contentsSize, 1000u);
        EXPECT_TRUE(doc->isSigned());  // документ переоткрыт из подписанного файла
    }
    const std::string after = ot::paths::readFile(path);
    ASSERT_GT(after.size(), before.size());
    EXPECT_EQ(after.compare(0, before.size(), before), 0) << "инкремент: исходный файл — префикс";

    const DocView view = readView(path);
    ASSERT_EQ(view.sigs.size(), 1u);
    const SigView& s = view.sigs[0];
    EXPECT_EQ(s.field, "Signature1");
    EXPECT_EQ(s.type, "Sig");
    EXPECT_EQ(s.filter, "Adobe.PPKLite");
    EXPECT_EQ(s.subFilter, "ETSI.CAdES.detached");
    EXPECT_EQ(s.reason, "Согласовано");
    EXPECT_EQ(s.location, "Москва");
    EXPECT_EQ(s.contact, "test@acrobatya.test");
    EXPECT_EQ(s.name, "АкроБатя тест");
    EXPECT_EQ(s.m, "D:20261027080000Z");
    EXPECT_EQ(s.propBuildApp, "AcroBatya");
    EXPECT_FALSE(s.reference) << "без /Lock — без FieldMDP";
    EXPECT_EQ(s.lockAction, "");
    EXPECT_EQ(s.reserved, ot::sign::PdfSigner::kMaxContentsSize);
    EXPECT_EQ(view.esicLevel, 2);
    EXPECT_EQ(view.esicBase, "1.7");
    EXPECT_TRUE(coversFile(path, s));
    expectValid(path, s);

    const ot::QpdfTool qpdf;
    if (!qpdf.available()) {
        GTEST_SKIP() << "нет qpdf.exe";
    }
    const ot::ToolResult check = qpdf.check(path);
    EXPECT_EQ(check.exitCode, 0) << check.output;
    const ot::ToolResult json = qpdf.json(path);
    ASSERT_EQ(json.exitCode, 0) << json.output;
    const auto parsed = nlohmann::json::parse(json.output);
    const auto field = qpdfObject(parsed, s.objnum);
    ASSERT_TRUE(field.contains("/V")) << field.dump();
    const std::string vref = field.at("/V").get<std::string>();  // «12 0 R»
    const auto v = qpdfObject(parsed, std::stoi(vref));
    EXPECT_EQ(v.at("/SubFilter"), "/ETSI.CAdES.detached");
    EXPECT_EQ(v.at("/Filter"), "/Adobe.PPKLite");
    EXPECT_EQ(v.at("/Reason"), "u:Согласовано");
    EXPECT_EQ(v.at("/Location"), "u:Москва");
}

TEST(signer, ecdsa_p256_bb) {
    OT_FEATURE("sign-12");
    ot::test::TempDir dir;
    const fs::path path = unsignedCopy(dir.path(), L"ec.pdf");
    {
        auto doc = Document::openOrThrow(path);
        (void)ot::sign::signDocument(*doc, request("signer-ec.pfx"));
    }
    const DocView view = readView(path);
    ASSERT_EQ(view.sigs.size(), 1u);
    expectValid(path, view.sigs[0]);
    const ot::sign::CmsInfo info = ot::sign::parseCms(view.sigs[0].contents);
    EXPECT_EQ(info.signatureAlgorithm, std::string(ot::sign::oid::kEcdsaWithSha256));
}

TEST(signer, pkcs7_compatibility_subfilter_has_no_esic) {
    ot::test::TempDir dir;
    const fs::path path = unsignedCopy(dir.path(), L"pkcs7.pdf");
    SignRequest r = request();
    r.subFilter = SubFilter::Pkcs7Detached;
    {
        auto doc = Document::openOrThrow(path);
        (void)ot::sign::signDocument(*doc, r);
    }
    const DocView view = readView(path);
    ASSERT_EQ(view.sigs.size(), 1u);
    EXPECT_EQ(view.sigs[0].subFilter, "adbe.pkcs7.detached");
    EXPECT_EQ(view.esicLevel, -1);
    expectValid(path, view.sigs[0]);
}

TEST(signer, existing_empty_signature_field_is_signed_in_place) {
    OT_FEATURE("forms-17");
    ot::test::TempDir dir;
    const fs::path path = makeFormFile(dir.path(), true);
    const DocView before = readView(path);
    ASSERT_TRUE(before.sigs.empty());
    SignRequest r = request();
    r.placement.field = "Подпись заказчика";
    r.placement.page = 5;  // у существующего поля страница и рамка — его
    {
        auto doc = Document::openOrThrow(path);
        const SignResult res = ot::sign::signDocument(*doc, r);
        EXPECT_EQ(res.field, "Подпись заказчика");
        EXPECT_EQ(res.page, 0);
        EXPECT_TRUE(res.visible);
    }
    const DocView view = readView(path);
    ASSERT_EQ(view.sigs.size(), 1u);
    EXPECT_EQ(view.sigs[0].field, "Подпись заказчика");
    EXPECT_EQ(view.otherFields.size(), 1u) << "нового поля нет";
    EXPECT_FLOAT_EQ(view.sigs[0].rect[2] - view.sigs[0].rect[0], 200.0f);
    expectValid(path, view.sigs[0]);
}

TEST(signer, invisible_signature_has_zero_rect) {
    ot::test::TempDir dir;
    const fs::path path = unsignedCopy(dir.path(), L"невидимая.pdf");
    SignRequest r = request();
    r.placement.rect = ot::RectF{};
    {
        auto doc = Document::openOrThrow(path);
        const SignResult res = ot::sign::signDocument(*doc, r);
        EXPECT_FALSE(res.visible);
    }
    const DocView view = readView(path);
    ASSERT_EQ(view.sigs.size(), 1u);
    for (float v : view.sigs[0].rect) {
        EXPECT_EQ(v, 0.0f);
    }
    expectValid(path, view.sigs[0]);
}

TEST(signer, two_signatures_in_a_row_are_both_valid) {
    ot::test::TempDir dir;
    const fs::path path = unsignedCopy(dir.path(), L"две.pdf");
    {
        auto doc = Document::openOrThrow(path);
        (void)ot::sign::signDocument(*doc, request());
        SignRequest second = request("signer-ec.pfx");
        second.placement.rect = ot::RectF{300, 72, 500, 132};
        second.reason = "Утверждаю";
        const SignResult res = ot::sign::signDocument(*doc, second);
        EXPECT_EQ(res.field, "Signature2");
        EXPECT_EQ(ot::revisions::signatures(*doc).size(), 2u);
    }
    const DocView view = readView(path);
    ASSERT_EQ(view.sigs.size(), 2u);
    EXPECT_FALSE(coversFile(path, view.sigs[0]));
    EXPECT_TRUE(coversFile(path, view.sigs[1]));
    expectValid(path, view.sigs[0]);
    expectValid(path, view.sigs[1]);
    auto doc = Document::openOrThrow(path);
    EXPECT_TRUE(ot::revisions::changeHistoryValid(*doc));
}

TEST(signer, lock_makes_form_fields_read_only) {
    OT_FEATURE("sign-15");
    ot::test::TempDir dir;
    const fs::path path = makeFormFile(dir.path(), false);
    SignRequest r = request();
    r.lock = true;
    {
        auto doc = Document::openOrThrow(path);
        (void)ot::sign::signDocument(*doc, r);
    }
    const DocView view = readView(path);
    ASSERT_EQ(view.sigs.size(), 1u);
    ASSERT_EQ(view.otherFields.size(), 1u);
    EXPECT_EQ(view.otherFields[0].first, "Имя");
    EXPECT_NE(view.otherFields[0].second & PDF_FIELD_IS_READ_ONLY, 0) << "текстовое поле заблокировано";
    EXPECT_EQ(view.sigs[0].lockAction, "All");
    EXPECT_TRUE(view.sigs[0].reference);
    EXPECT_EQ(view.sigs[0].transformAction, "All");
    expectValid(path, view.sigs[0]);
}

TEST(signer, unsaved_edits_are_written_before_signing) {
    ot::test::TempDir dir;
    const fs::path path = unsignedCopy(dir.path(), L"правки.pdf");
    {
        auto doc = Document::openOrThrow(path);
        ot::test::addNote(*doc, 0, "Записка до подписи");
        (void)ot::sign::signDocument(*doc, request());
        EXPECT_EQ(doc->undoStack().size(), 0);
    }
    const DocView view = readView(path);
    ASSERT_EQ(view.sigs.size(), 1u);
    EXPECT_TRUE(coversFile(path, view.sigs[0]));
    expectValid(path, view.sigs[0]);
    auto doc = Document::openOrThrow(path);
    EXPECT_EQ(ot::revisions::versionCount(*doc), 3) << "исходник, правки, подпись";
}

TEST(signer, sign_as_writes_target_and_keeps_source) {
    ot::test::TempDir dir;
    const fs::path source = unsignedCopy(dir.path(), L"исходник.pdf");
    const std::string before = ot::paths::readFile(source);
    const fs::path target = dir.path() / L"подписан.pdf";
    {
        auto doc = Document::openOrThrow(source);
        (void)ot::sign::signDocument(*doc, request(), target);
        EXPECT_EQ(doc->path(), target);
    }
    EXPECT_EQ(ot::paths::readFile(source), before);
    const DocView view = readView(target);
    ASSERT_EQ(view.sigs.size(), 1u);
    expectValid(target, view.sigs[0]);
}

TEST(signer, errors_leave_document_untouched) {
    ot::test::TempDir dir;
    const fs::path path = makeFormFile(dir.path(), false);
    const std::string before = ot::paths::readFile(path);
    auto doc = Document::openOrThrow(path);
    SignRequest r = request();
    r.identity.reset();
    try {
        (void)ot::sign::signDocument(*doc, r);
        ADD_FAILURE() << "без удостоверения подписи нет";
    } catch (const SignError& e) {
        EXPECT_EQ(e.status(), SignStatus::NoIdentity);
    }
    r = request();
    r.placement.field = "Имя";
    try {
        (void)ot::sign::signDocument(*doc, r);
        ADD_FAILURE() << "текстовое поле не подписывается";
    } catch (const SignError& e) {
        EXPECT_EQ(e.status(), SignStatus::NotSignatureField);
    }
    r = request();
    r.placement.page = 3;
    try {
        (void)ot::sign::signDocument(*doc, r);
        ADD_FAILURE() << "нет такой страницы";
    } catch (const SignError& e) {
        EXPECT_EQ(e.status(), SignStatus::BadPlacement);
    }
    EXPECT_EQ(doc->undoStack().size(), 0);
    EXPECT_EQ(ot::paths::readFile(path), before);

    // Подписанное поле второй раз не подписывается.
    (void)ot::sign::signDocument(*doc, request());
    r = request();
    r.placement.field = "Signature1";
    try {
        (void)ot::sign::signDocument(*doc, r);
        ADD_FAILURE() << "поле уже подписано";
    } catch (const SignError& e) {
        EXPECT_EQ(e.status(), SignStatus::FieldSigned);
    }
}

// DocumentSession::sign: задача потока документа — подпись, переоткрытие (saved, страница подписи);
// ошибка — SignError в QFuture, документ на месте.
TEST(signer, session_signs_in_document_thread) {
    ot::test::qtApp();
    ot::test::TempDir dir;
    const fs::path path = unsignedCopy(dir.path(), L"сессия.pdf");
    ot::DocumentSession session;
    ASSERT_TRUE(session.open(QString::fromStdWString(path.wstring()))) << session.errorString().toStdString();
    QStringList saved;
    QObject::connect(
        &session, &ot::DocumentSession::saved, &session, [&](const QString& p) { saved << p; },
        Qt::DirectConnection);
    const SignResult r = session.sign(request()).result();
    EXPECT_TRUE(r.save.ok());
    EXPECT_EQ(saved.size(), 1);
    EXPECT_TRUE(session.summary().isSigned);
    SignRequest bad = request();
    bad.placement.field = "Signature1";
    QFuture<SignResult> f = session.sign(bad);
    try {
        (void)f.result();
        ADD_FAILURE() << "поле уже подписано";
    } catch (const SignError& e) {
        EXPECT_EQ(e.status(), SignStatus::FieldSigned);
    }
    EXPECT_TRUE(session.isOpen());
    EXPECT_EQ(readView(path).sigs.size(), 1u);
}

// Подписант: резерв — константа, create_digest не вызывается заранее; результат больше резерва — TooLarge.
TEST(signer, contents_reserve_is_constant_and_enforced) {
    struct Huge : ot::sign::PdfSigner {
        int calls = 0;
        Bytes contents(std::span<const std::uint8_t>) override {
            ++calls;
            return Bytes(kMaxContentsSize + 1, 0x30);
        }
    };
    auto signer = std::make_shared<Huge>();
    const Bytes digest(32, 0);
    try {
        (void)signer->sign(digest);
        ADD_FAILURE() << "больше резерва";
    } catch (const SignError& e) {
        EXPECT_EQ(e.status(), SignStatus::TooLarge);
    }
    EXPECT_EQ(signer->calls, 1);
    EXPECT_EQ(ot::sign::PdfSigner::kMaxContentsSize, 16384u);
}

// compat.sign.b: файлы для tools/compat_check.py --expect-sig INTACT,TRUSTED,UNTOUCHED (pyHanko с корнем
// PKI), qpdf --check, pdfcpu strict, mutool clean, pypdfium2 — в OTTISK_COMPAT_OUT/sign/b; две подписи — в
// sign/b-multi (первая уже не UNTOUCHED).
TEST(sign_compat, DISABLED_write_b) {
    const char* outEnv = std::getenv("OTTISK_COMPAT_OUT");  // NOLINT(concurrency-mt-unsafe)
    ASSERT_NE(outEnv, nullptr) << "нужен OTTISK_COMPAT_OUT";
    const fs::path out = fs::path(ot::paths::fromUtf8(outEnv)) / L"sign";
    const fs::path b = out / L"b";
    const fs::path multi = out / L"b-multi";
    for (const fs::path& d : {b, multi}) {
        fs::remove_all(d);
        fs::create_directories(d);
    }
    const auto sign = [](const fs::path& file, SignRequest r) {
        auto doc = Document::openOrThrow(file);
        (void)ot::sign::signDocument(*doc, r);
    };
    sign(unsignedCopy(b, L"rsa-bb.pdf"), request());
    sign(unsignedCopy(b, L"ec-bb.pdf"), request("signer-ec.pfx"));
    SignRequest pkcs7 = request();
    pkcs7.subFilter = SubFilter::Pkcs7Detached;
    sign(unsignedCopy(b, L"pkcs7-bb.pdf"), pkcs7);
    SignRequest invisible = request();
    invisible.placement.rect = ot::RectF{};
    sign(unsignedCopy(b, L"invisible-bb.pdf"), invisible);
    SignRequest field = request();
    field.placement.field = "Подпись заказчика";
    sign(copyFile(makeFormFile(out, true), b / L"field-bb.pdf"), field);
    SignRequest locked = request();
    locked.lock = true;
    sign(copyFile(makeFormFile(out, false), b / L"locked-bb.pdf"), locked);

    const fs::path two = unsignedCopy(multi, L"two-bb.pdf");
    sign(two, request());
    SignRequest second = request("signer-ec.pfx");
    second.placement.rect = ot::RectF{300, 72, 500, 132};
    sign(two, second);
}
