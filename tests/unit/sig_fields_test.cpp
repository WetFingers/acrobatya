// unit.sig_fields — поля подписи для интерфейса (план 4.6, п. 1; эталон 3.8 «Инструмент»; sign-12 — щелчок
// по пустому полю, forms-17): пустое поле формы «Подпись заказчика» — со страницей и рамкой, не подписано;
// после подписи поля — подписано, после второй подписи с блокировкой — новое поле подписано, оба есть в
// списке; невидимая подпись — без рамки; метка времени документа помечена; пустое поле под точкой —
// только видимое и не только для чтения.
#include <gtest/gtest.h>

#include <filesystem>

#include "core/doc/document.h"
#include "core/sign/doc_timestamp.h"
#include "core/sign/sig_fields.h"
#include "core/sign/signer.h"
#include "core/util/features.h"
#include "support/sign_support.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;
using ot::Document;
using ot::sign::SignatureField;
using ot::sign::SignRequest;

namespace {

const SignatureField* byName(const std::vector<SignatureField>& fields, const std::string& name) {
    for (const SignatureField& f : fields) {
        if (f.name == name) {
            return &f;
        }
    }
    return nullptr;
}

}  // namespace

TEST(sig_fields, empty_field_then_signed) {
    OT_FEATURE("forms-17");
    OT_FEATURE("sign-12");
    ot::test::TempDir dir;
    const fs::path path = ot::test::makeFormFile(dir.path(), true);
    {
        auto doc = Document::openOrThrow(path);
        const std::vector<SignatureField> fields = ot::sign::signatureFields(*doc);
        ASSERT_EQ(fields.size(), 1u);
        const SignatureField& f = fields[0];
        EXPECT_EQ(f.name, "Подпись заказчика");
        EXPECT_EQ(f.page, 0);
        EXPECT_TRUE(f.visible);
        EXPECT_FALSE(f.isSigned);
        EXPECT_FALSE(f.readOnly);
        EXPECT_FALSE(f.docTimestamp);
        EXPECT_GT(f.rect.width(), 10.0f);
        const ot::PointF inside{(f.rect.x0 + f.rect.x1) / 2, (f.rect.y0 + f.rect.y1) / 2};
        EXPECT_EQ(ot::sign::emptyFieldAt(fields, 0, inside), &fields[0]);
        EXPECT_EQ(ot::sign::emptyFieldAt(fields, 0, {f.rect.x0 - 20, f.rect.y0 - 20}), nullptr);
        EXPECT_EQ(ot::sign::emptyFieldAt(fields, 1, inside), nullptr);

        SignRequest r;
        r.identity = ot::test::pkiIdentity();
        r.placement.field = "Подпись заказчика";
        r.level = ot::sign::SignLevel::B;
        (void)ot::sign::signDocument(*doc, r);
    }
    {
        // Вторая подпись — новое поле с блокировкой документа.
        auto doc = Document::openOrThrow(path);
        SignRequest r;
        r.identity = ot::test::pkiIdentity();
        r.placement.page = 0;
        r.placement.rect = {72, 600, 250, 660};
        r.lock = true;
        r.level = ot::sign::SignLevel::B;
        (void)ot::sign::signDocument(*doc, r);
    }
    auto doc = Document::openOrThrow(path);
    const std::vector<SignatureField> fields = ot::sign::signatureFields(*doc);
    ASSERT_EQ(fields.size(), 2u);
    const SignatureField* first = byName(fields, "Подпись заказчика");
    ASSERT_NE(first, nullptr);
    EXPECT_TRUE(first->isSigned);
    const SignatureField* second = byName(fields, "Signature1");
    ASSERT_NE(second, nullptr);
    EXPECT_TRUE(second->isSigned);
    EXPECT_TRUE(second->readOnly);  // /Lock /All — поле подписи тоже только чтение
    EXPECT_NEAR(second->rect.x0, 72.0f, 0.5f);
    EXPECT_NEAR(second->rect.y1, 660.0f, 0.5f);
    // Подписанное поле — не «пустое под точкой».
    const ot::PointF inside{(first->rect.x0 + first->rect.x1) / 2, (first->rect.y0 + first->rect.y1) / 2};
    EXPECT_EQ(ot::sign::emptyFieldAt(fields, first->page, inside), nullptr);
}

TEST(sig_fields, invisible_signature_and_doc_timestamp) {
    ot::test::TempDir dir;
    const fs::path path = ot::test::unsignedCopy(dir.path(), L"invisible.pdf");
    const ot::test::LocalTsa tsa = ot::test::localTsa();
    {
        auto doc = Document::openOrThrow(path);
        SignRequest r;
        r.identity = ot::test::pkiIdentity();
        r.level = ot::sign::SignLevel::B;
        (void)ot::sign::signDocument(*doc, r);  // рамка пуста — невидимая
    }
    {
        auto doc = Document::openOrThrow(path);
        (void)ot::sign::addDocumentTimestamp(*doc, tsa.client);
    }
    auto doc = Document::openOrThrow(path);
    const std::vector<SignatureField> fields = ot::sign::signatureFields(*doc);
    ASSERT_EQ(fields.size(), 2u);
    EXPECT_TRUE(fields[0].isSigned);
    EXPECT_FALSE(fields[0].visible);
    EXPECT_FALSE(fields[0].docTimestamp);
    EXPECT_TRUE(fields[1].isSigned);
    EXPECT_TRUE(fields[1].docTimestamp);
    EXPECT_FALSE(fields[1].visible);
}

TEST(sig_fields, not_pdf_or_no_form_is_empty) {
    ot::test::TempDir dir;
    const fs::path path = ot::test::unsignedCopy(dir.path(), L"plain.pdf");
    auto doc = Document::openOrThrow(path);
    EXPECT_TRUE(ot::sign::signatureFields(*doc).empty());
}
