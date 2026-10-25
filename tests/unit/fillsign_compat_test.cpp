// Артефакты compat.fillsign (план 5.3, п. 3; эталон 3.5, 9 «Fill & Sign ставит текст на линию и
// подпись-штамп, плоская копия проходит qpdf»; forms-07, sign-01): fillsign_compat.DISABLED_write пишет
// <OTTISK_COMPAT_OUT>/fillsign — fillsign.pdf (плоская анкета: текст на линиях, гребёнка, отметки ✓ ✕ и
// подпись-штамп с PNG-альфой, записано инкрементом) и fillsign-flat.pdf (плоская копия: Saver::saveCopy с
// flattenAnnotations — аннотаций нет, текст и подпись в содержимом страницы); tools/compat_check.py проверяет
// оба (qpdf --check, pdfcpu strict, mutool clean, pypdfium2). Здесь же — без внешних программ: в плоской
// копии нет /Annots, исходник с аннотациями цел.
#include <gtest/gtest.h>

#include <QImage>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "core/annot/annots.h"
#include "core/annot/fillsign.h"
#include "core/annot/stamps.h"
#include "core/doc/document.h"
#include "core/doc/saver.h"
#include "core/text/stext.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "services/signature_image.h"
#include "support/qt_app.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;
namespace fillsign = ot::fillsign;
using ot::annot::AnnotColor;
using ot::annot::AnnotProps;

namespace {

constexpr float kPageHeight = 841.8898f;
ot::PointF pt(float x, float pdfY) { return {x, kPageHeight - pdfY}; }

fs::path anketa() { return ot::test::sourceDir() / "tests" / "fixtures" / "forms" / "flat" / "anketa.pdf"; }

// Росчерк подписи: синусоида и подчёркивание на прозрачном (PNG с альфой).
std::string signaturePng() {
    QImage image(360, 90, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    for (int x = 10; x < 350; ++x) {
        const int y = 45 + static_cast<int>(std::lround(22 * std::sin(x / 18.0)));
        for (int dy = -2; dy <= 2; ++dy) {
            image.setPixelColor(x, y + dy, QColor(20, 40, 150));
        }
        if (x > 40 && x < 300) {
            image.setPixelColor(x, 78, QColor(20, 40, 150));
            image.setPixelColor(x, 79, QColor(20, 40, 150));
        }
    }
    const QByteArray png = fillsign::toPng(image);
    return {png.constData(), static_cast<std::size_t>(png.size())};
}

// Анкета с текстом, гребёнкой, отметками и подписью (в операциях журнала документа doc).
void fill(ot::Document& doc) {
    ot::TextOptions options;
    options.collectVectors = true;
    const auto text = ot::TextPage::build(doc, 0, options);
    const fillsign::PageGeometry g = fillsign::geometryOf(text->vectors(), text->lines());
    const AnnotColor ink = AnnotColor::gray(0);
    ot::annot::create(doc, 0, fillsign::textProps(fillsign::placeText(g, pt(200, 724)), "Иванов", ink, 595));
    ot::annot::create(doc, 0, fillsign::textProps(fillsign::placeText(g, pt(200, 692)), "Иван", ink, 595));
    fillsign::createAll(doc, 0, fillsign::combProps(fillsign::placeText(g, pt(190, 612)), U"01021990", ink));
    fillsign::createAll(doc, 0,
                        fillsign::combProps(fillsign::placeText(g, pt(190, 562)), U"770123456789", ink));
    ot::annot::create(doc, 0,
                      fillsign::markProps(fillsign::Mark::Check, fillsign::placeMark(g, pt(78, 474)), ink));
    ot::annot::create(doc, 0,
                      fillsign::markProps(fillsign::Mark::Cross, fillsign::placeMark(g, pt(78, 450)), ink));
    AnnotProps sig;
    sig.type = ot::annot::AnnotType::Stamp;
    sig.rect = {150, kPageHeight - 404, 290, kPageHeight - 369};
    sig.subject = "Signature";
    ot::annot::createImageStamp(doc, 0, signaturePng(), sig, "FillSignSignature");
}

}  // namespace

TEST(fillsign_compat, flat_copy_has_no_annotations) {
    OT_FEATURE("forms-07");
    OT_FEATURE("sign-01");
    ot::test::qtApp();
    ot::test::TempDir dir;
    const fs::path source = dir.path() / "anketa.pdf";
    fs::copy_file(anketa(), source);
    auto doc = ot::Document::openOrThrow(source);
    fill(*doc);
    const std::size_t annots = ot::annot::listAnnots(*doc, 0).size();
    EXPECT_EQ(annots, 2u + 8u + 12u + 2u + 1u);
    // Подпись — PNG с альфой в /Stamp.
    const std::string png = ot::annot::stampImage(*doc, ot::annot::listAnnots(*doc, 0).back().ref);
    QImage image;
    ASSERT_TRUE(image.loadFromData(reinterpret_cast<const uchar*>(png.data()), static_cast<int>(png.size())));
    EXPECT_TRUE(image.hasAlphaChannel());
    EXPECT_EQ(qAlpha(image.pixel(0, 0)), 0);
    EXPECT_GE(fillsign::inkComponents(image), 1);

    const fs::path flat = dir.path() / "flat.pdf";
    ot::SaveOptions options;
    options.flattenAnnotations = true;
    const ot::SaveReport report = ot::Saver::saveCopy(*doc, flat, options);
    ASSERT_TRUE(report.ok()) << report.message;
    EXPECT_EQ(ot::annot::listAnnots(*doc, 0).size(), annots);  // документ — со своими аннотациями
    auto copy = ot::Document::openOrThrow(flat);
    EXPECT_TRUE(ot::annot::listAnnots(*copy, 0).empty());
    // Текст стал частью страницы.
    const auto text = ot::TextPage::build(*copy, 0);
    EXPECT_NE(text->text().find("Иванов"), std::string::npos);
}

TEST(fillsign_compat, DISABLED_write) {
    const char* out = std::getenv("OTTISK_COMPAT_OUT");
    if (out == nullptr || *out == '\0') {
        GTEST_SKIP() << "OTTISK_COMPAT_OUT не задан";
    }
    ot::test::qtApp();
    const fs::path dir = fs::path(ot::paths::fromUtf8(out)) / "fillsign";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path filled = dir / "fillsign.pdf";
    fs::copy_file(anketa(), filled);
    {
        auto doc = ot::Document::openOrThrow(filled);
        fill(*doc);
        ot::SaveOptions flat;
        flat.flattenAnnotations = true;
        const ot::SaveReport copy = ot::Saver::saveCopy(*doc, dir / "fillsign-flat.pdf", flat);
        ASSERT_TRUE(copy.ok()) << copy.message;
        const ot::SaveReport report = ot::Saver::save(*doc, filled, {.mode = ot::SaveMode::Incremental});
        ASSERT_TRUE(report.ok()) << report.message;
    }
    EXPECT_TRUE(fs::exists(dir / "fillsign-flat.pdf"));
}
