#include "support/sign_support.h"

#include <gtest/gtest.h>

#include <algorithm>

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/handles.h"
#include "core/sign/cms.h"
#include "core/util/paths.h"
#include "support/pki.h"
#include "support/test_pdf.h"

extern "C" {
#include <mupdf/pdf.h>
}

namespace fs = std::filesystem;
using ot::Document;
using ot::sign::Bytes;
using ot::sign::SigningKey;

namespace ot::test {

std::shared_ptr<const SigningKey> pkiIdentity(const char* pfx) {
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

namespace {

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

}  // namespace

DocView readSignatures(const fs::path& file) {
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
void expectCmsValid(const fs::path& file, const SigView& s) {
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

}  // namespace ot::test
