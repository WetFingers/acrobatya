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

namespace {

std::shared_ptr<const SigningKey> identity(const char* pfx) {
    const std::string bytes = ot::test::readPki(pfx);
    return std::make_shared<SigningKey>(
        ot::sign::loadPkcs12(std::vector<std::uint8_t>(bytes.begin(), bytes.end()), "acrobatya"));
}

fs::path copyFile(const fs::path& from, const fs::path& to) {
    ot::paths::writeFile(to, ot::paths::readFile(from));
    return to;
}

fs::path unsignedCopy(const fs::path& dir, const std::wstring& name) {
    return copyFile(ot::test::sourceDir() / L"tests/fixtures/sig/unsigned.pdf", dir / name);
}

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

// Длина DER по заголовку (/Contents дополнен нулями до резерва).
std::size_t derLength(const Bytes& der) {
    if (der.size() < 2 || der[0] != 0x30) {
        return 0;
    }
    if ((der[1] & 0x80) == 0) {
        return 2 + der[1];
    }
    const int n = der[1] & 0x7f;
    std::size_t len = 0;
    for (int i = 0; i < n; ++i) {
        len = (len << 8) | der[2 + static_cast<std::size_t>(i)];
    }
    return 2 + static_cast<std::size_t>(n) + len;
}

struct SigView {
    std::string field, type, filter, subFilter, reason, location, contact, name, m;
    std::string propBuildApp;
    std::vector<ot::sign::ByteRange> ranges;
    Bytes contents;  // DER без дополнения
    std::size_t reserved = 0;
    bool reference = false;
    std::string transformAction;  // /Reference[0] /TransformParams /Action
    std::string lockAction;       // /Lock /Action поля
    int objnum = 0;
    int ff = 0;
    float rect[4] = {0, 0, 0, 0};
};

struct DocView {
    std::vector<SigView> sigs;
    int esicLevel = -1;  // /Extensions /ESIC /ExtensionLevel, −1 — нет
    std::string esicBase;
    std::vector<std::pair<std::string, int>> otherFields;  // имя, Ff
};

std::string textOf(fz_context* ctx, pdf_obj* obj) {
    const char* s = nullptr;
    ot::mu::call(ctx, [&] { s = pdf_to_text_string(ctx, obj); });
    return s != nullptr ? s : "";
}

std::string nameOf(fz_context* ctx, pdf_obj* obj) {
    const char* s = nullptr;
    ot::mu::call(ctx, [&] { s = pdf_to_name(ctx, obj); });
    return s != nullptr ? s : "";
}

DocView readView(const fs::path& file) {
    DocView view;
    auto doc = Document::openOrThrow(file);
    fz_context* ctx = doc->context().get();
    pdf_document* pdf = doc->pdf();
    pdf_obj* root = nullptr;
    pdf_obj* fields = nullptr;
    int n = 0;
    ot::mu::call(ctx, [&] {
        root = pdf_dict_get(ctx, pdf_trailer(ctx, pdf), PDF_NAME(Root));
        fields = pdf_dict_getp(ctx, root, "AcroForm/Fields");
        n = pdf_array_len(ctx, fields);
    });
    pdf_obj* esic = nullptr;
    ot::mu::call(ctx, [&] { esic = pdf_dict_getp(ctx, root, "Extensions/ESIC"); });
    if (esic != nullptr) {
        ot::mu::call(ctx,
                     [&] { view.esicLevel = pdf_to_int(ctx, pdf_dict_gets(ctx, esic, "ExtensionLevel")); });
        pdf_obj* base = nullptr;
        ot::mu::call(ctx, [&] { base = pdf_dict_gets(ctx, esic, "BaseVersion"); });
        view.esicBase = nameOf(ctx, base);
    }
    for (int i = 0; i < n; ++i) {
        pdf_obj* f = nullptr;
        pdf_obj* ft = nullptr;
        pdf_obj* v = nullptr;
        int ff = 0;
        int objnum = 0;
        ot::mu::call(ctx, [&] {
            f = pdf_array_get(ctx, fields, i);
            ft = pdf_dict_get_inheritable(ctx, f, PDF_NAME(FT));
            v = pdf_dict_get(ctx, f, PDF_NAME(V));
            ff = pdf_dict_get_inheritable_int(ctx, f, PDF_NAME(Ff));
            objnum = pdf_to_num(ctx, f);
        });
        pdf_obj* t = nullptr;
        ot::mu::call(ctx, [&] { t = pdf_dict_get(ctx, f, PDF_NAME(T)); });
        const std::string field = textOf(ctx, t);
        if (nameOf(ctx, ft) != "Sig" || v == nullptr) {
            view.otherFields.emplace_back(field, ff);
            continue;
        }
        SigView s;
        s.field = field;
        s.ff = ff;
        s.objnum = objnum;
        pdf_obj *type = nullptr, *filter = nullptr, *sub = nullptr, *reason = nullptr, *location = nullptr,
                *contact = nullptr, *name = nullptr, *m = nullptr, *br = nullptr, *contents = nullptr,
                *ref = nullptr, *action = nullptr, *lock = nullptr, *app = nullptr;
        int brLen = 0;
        fz_rect rect = fz_empty_rect;
        ot::mu::call(ctx, [&] {
            type = pdf_dict_get(ctx, v, PDF_NAME(Type));
            filter = pdf_dict_get(ctx, v, PDF_NAME(Filter));
            sub = pdf_dict_get(ctx, v, PDF_NAME(SubFilter));
            reason = pdf_dict_get(ctx, v, PDF_NAME(Reason));
            location = pdf_dict_get(ctx, v, PDF_NAME(Location));
            contact = pdf_dict_get(ctx, v, PDF_NAME(ContactInfo));
            name = pdf_dict_get(ctx, v, PDF_NAME(Name));
            m = pdf_dict_get(ctx, v, PDF_NAME(M));
            br = pdf_dict_get(ctx, v, PDF_NAME(ByteRange));
            brLen = pdf_array_len(ctx, br);
            contents = pdf_dict_get(ctx, v, PDF_NAME(Contents));
            ref = pdf_dict_get(ctx, v, PDF_NAME(Reference));
            action = pdf_dict_getp(ctx, pdf_array_get(ctx, ref, 0), "TransformParams/Action");
            lock = pdf_dict_getp(ctx, f, "Lock/Action");
            app = pdf_dict_getp(ctx, v, "Prop_Build/App/Name");
            rect = pdf_dict_get_rect(ctx, f, PDF_NAME(Rect));
        });
        s.type = nameOf(ctx, type);
        s.filter = nameOf(ctx, filter);
        s.subFilter = nameOf(ctx, sub);
        s.reason = textOf(ctx, reason);
        s.location = textOf(ctx, location);
        s.contact = textOf(ctx, contact);
        s.name = textOf(ctx, name);
        s.m = textOf(ctx, m);
        s.reference = ref != nullptr;
        s.transformAction = nameOf(ctx, action);
        s.lockAction = nameOf(ctx, lock);
        s.propBuildApp = nameOf(ctx, app);
        s.rect[0] = rect.x0;
        s.rect[1] = rect.y0;
        s.rect[2] = rect.x1;
        s.rect[3] = rect.y1;
        for (int k = 0; k + 1 < brLen; k += 2) {
            std::int64_t off = 0;
            std::int64_t len = 0;
            ot::mu::call(ctx, [&] {
                off = pdf_array_get_int(ctx, br, k);
                len = pdf_array_get_int(ctx, br, k + 1);
            });
            s.ranges.push_back({off, len});
        }
        const char* buf = nullptr;
        std::size_t len = 0;
        ot::mu::call(ctx, [&] {
            buf = pdf_to_str_buf(ctx, contents);
            len = pdf_to_str_len(ctx, contents);
        });
        s.reserved = len;
        Bytes all(reinterpret_cast<const std::uint8_t*>(buf),
                  reinterpret_cast<const std::uint8_t*>(buf) + len);
        all.resize(std::min(all.size(), derLength(all)));
        s.contents = std::move(all);
        view.sigs.push_back(std::move(s));
    }
    return view;
}

// Подпись цела: CMS_verify с CAdES и цепочкой до корня PKI по /ByteRange файла.
void expectValid(const fs::path& file, const SigView& s) {
    ASSERT_FALSE(s.contents.empty());
    ASSERT_EQ(s.ranges.size(), 2u);
    EXPECT_EQ(s.ranges[0].offset, 0);
    const auto store = ot::test::pkiRootStore();
    auto bio = ot::sign::ossl::openRangeBio(file, s.ranges);
    std::string error;
    EXPECT_TRUE(ot::sign::cmsVerifyDetached(s.contents, bio.get(), store.get(), &error)) << error;
    const ot::sign::CmsInfo info = ot::sign::parseCms(s.contents);
    EXPECT_FALSE(info.signingTime.has_value());
    EXPECT_TRUE(info.signingCertificateV2.has_value());
    EXPECT_EQ(info.messageDigest, ot::sign::ossl::sha256Ranges(file, s.ranges));
}

bool coversFile(const fs::path& file, const SigView& s) {
    return s.ranges.size() == 2 &&
           static_cast<std::uintmax_t>(s.ranges[1].offset + s.ranges[1].length) == fs::file_size(file);
}

// Документ с текстовым полем «Имя» и (empty) пустым полем подписи «Подпись заказчика» без /Lock.
fs::path makeFormFile(const fs::path& dir, bool withSigField) {
    auto doc = Document::createBlank(1);
    fz_context* ctx = doc->context().get();
    pdf_document* pdf = doc->pdf();
    {
        ot::Journal::Operation op(*doc, "Make form");
        ot::mu::PageHandle page(ctx);
        ot::mu::call(ctx, [&] { page.reset(fz_load_page(ctx, &pdf->super, 0)); });
        ot::mu::call(ctx, [&] {
            pdf_page* pp = pdf_page_from_fz_page(ctx, page);
            pdf_obj* root = pdf_dict_get(ctx, pdf_trailer(ctx, pdf), PDF_NAME(Root));
            pdf_obj* form = pdf_dict_put_dict(ctx, root, PDF_NAME(AcroForm), 2);
            pdf_obj* fields = pdf_dict_put_array(ctx, form, PDF_NAME(Fields), 2);
            pdf_obj* tx = pdf_add_new_dict(ctx, pdf, 8);
            pdf_dict_put(ctx, tx, PDF_NAME(Type), PDF_NAME(Annot));
            pdf_dict_put(ctx, tx, PDF_NAME(Subtype), PDF_NAME(Widget));
            pdf_dict_put(ctx, tx, PDF_NAME(FT), PDF_NAME(Tx));
            pdf_dict_put_text_string(ctx, tx, PDF_NAME(T), "Имя");
            pdf_dict_put_text_string(ctx, tx, PDF_NAME(DA), "/Helv 0 Tf 0 g");
            pdf_dict_put_rect(ctx, tx, PDF_NAME(Rect), fz_make_rect(72, 700, 272, 720));
            pdf_dict_put(ctx, tx, PDF_NAME(P), pp->obj);
            pdf_obj* annots = pdf_dict_put_array(ctx, pp->obj, PDF_NAME(Annots), 2);
            pdf_array_push(ctx, annots, tx);
            pdf_array_push(ctx, fields, tx);
            pdf_drop_obj(ctx, tx);
        });
        if (withSigField) {
            ot::mu::call(ctx, [&] {
                pdf_page* pp = pdf_page_from_fz_page(ctx, page);
                char name[] = "Подпись заказчика";
                pdf_annot* w = pdf_create_signature_widget(ctx, pp, name);
                pdf_set_annot_rect(ctx, w, fz_make_rect(300, 600, 500, 660));
                pdf_dict_del(ctx, pdf_annot_obj(ctx, w), PDF_NAME(Lock));
                pdf_drop_annot(ctx, w);
            });
        }
        op.commit();
    }
    const fs::path path = dir / (withSigField ? L"форма-с-полем.pdf" : L"форма.pdf");
    ot::paths::writeFile(path, doc->savePdfToMemory());
    return path;
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
