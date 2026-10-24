// unit.sig_appearance — внешний вид подписи (план 4.2, п. 3; эталон 3.8 «Диалог подписания» — внешний вид;
// sign-13): строки текста по флажкам (имя, DN, причина, место, дата, ярлыки — переводы даёт интерфейс),
// готовые режимы MuPDF (флаги PDF_SIGNATURE_SHOW_*) ↔ конструктор, дата как в Acrobat; снимок рендера виджета
// подписанного файла (144 dpi) против эталона tests/fixtures/sig/appearance/*.png — имя слева и текст,
// картинка PNG с альфой и текст, «только изображение», только текст, логотип фоном; предпросмотр совпадает с
// виджетом; картинки нет — SignError BadAppearance до правки документа. Эталоны пересоздаёт
// OTTISK_UPDATE_SIG_APPEARANCE=1 (смотреть PNG глазами).
#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <regex>
#include <string>
#include <vector>

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/handles.h"
#include "core/render/bitmap.h"
#include "core/render/image_compare.h"
#include "core/sign/appearance.h"
#include "core/sign/signer.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "support/sign_support.h"
#include "support/test_pdf.h"

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

namespace fs = std::filesystem;
using ot::Bitmap;
using ot::Document;
using ot::sign::AppearanceData;
using ot::sign::AppearanceGraphic;
using ot::sign::SignatureAppearance;
using ot::sign::SignError;
using ot::sign::SignRequest;
using ot::sign::SignStatus;

namespace {

AppearanceData sampleData() {
    AppearanceData d;
    d.name = "АкроБатя тест";
    d.dn = "CN=АкроБатя тест, O=АкроБатя, C=RU";
    d.reason = "Согласовано";
    d.location = "Москва";
    d.time = 1793088000;
    return d;
}

ot::sign::AppearanceLabels russianLabels() {
    ot::sign::AppearanceLabels l;
    l.signedBy = "Подписано цифровой подписью:";
    l.dn = "DN:";
    l.reason = "Причина:";
    l.location = "Место:";
    l.date = "Дата:";
    return l;
}

// PNG w × h с альфой: синяя «роспись» (pen = true) или оранжевый круг (логотип) на прозрачном фоне.
fs::path makePng(const fs::path& path, int w, int h, bool pen) {
    ot::MuContext& mu = ot::MuContext::global();
    fz_context* ctx = mu.get();
    ot::mu::PixmapHandle pix(ctx);
    ot::mu::BufferHandle buf(ctx);
    ot::mu::call(ctx, [&] {
        pix.reset(fz_new_pixmap(ctx, fz_device_rgb(ctx), w, h, nullptr, 1));
        fz_clear_pixmap(ctx, pix);
        unsigned char* p = fz_pixmap_samples(ctx, pix);
        const int stride = static_cast<int>(fz_pixmap_stride(ctx, pix));
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const float fx = static_cast<float>(x) / static_cast<float>(w);
                const float fy = static_cast<float>(y) / static_cast<float>(h);
                int a = 0;
                if (pen) {
                    const float curve =
                        0.5f + 0.3f * static_cast<float>(std::sin(fx * 12.0f)) * (1.0f - fx * 0.5f);
                    const float d = std::fabs(fy - curve);
                    a = d < 0.06f ? 255 : (d < 0.09f ? 128 : 0);
                } else {
                    const float dx = fx - 0.5f;
                    const float dy = fy - 0.5f;
                    a = dx * dx + dy * dy < 0.2f ? 255 : 0;
                }
                unsigned char* px = p + y * stride + x * 4;
                // Премультиплицированный цвет.
                px[0] = static_cast<unsigned char>((pen ? 30 : 240) * a / 255);
                px[1] = static_cast<unsigned char>((pen ? 60 : 120) * a / 255);
                px[2] = static_cast<unsigned char>((pen ? 200 : 20) * a / 255);
                px[3] = static_cast<unsigned char>(a);
            }
        }
        buf.reset(fz_new_buffer_from_pixmap_as_png(ctx, pix, fz_default_color_params));
    });
    unsigned char* data = nullptr;
    std::size_t len = 0;
    ot::mu::call(ctx, [&] { len = fz_buffer_storage(ctx, buf, &data); });
    ot::paths::writeFile(path, std::string(reinterpret_cast<const char*>(data), len));
    return path;
}

fs::path refDir() { return ot::test::sourceDir() / L"tests/fixtures/sig/appearance"; }

constexpr float kDpi = 144.0f;
const ot::RectF kRect{72, 400, 312, 472};  // 240 × 72 pt на пустом месте страницы

// Подписать копию фикстуры с видом look и вернуть растр рамки виджета.
Bitmap signedWidget(const fs::path& dir, const std::wstring& name, const SignatureAppearance& look) {
    const fs::path path = ot::test::unsignedCopy(dir, name + L".pdf");
    auto doc = Document::openOrThrow(path);
    SignRequest r;
    r.identity = ot::test::pkiIdentity("signer-rsa.pfx");
    r.reason = "Согласовано";
    r.location = "Москва";
    r.placement.rect = kRect;
    r.signingTime = 1793088000;
    r.appearance = look;
    (void)ot::sign::signDocument(*doc, r);
    ot::RenderOptions options;
    options.dpi = kDpi;
    const Bitmap page = doc->renderPage(0, options);
    return ot::cropBitmap(page, ot::pixelRect(kRect, kDpi));
}

long long inkPixels(const Bitmap& b) {
    long long ink = 0;
    for (int y = 0; y < b.height; ++y) {
        const std::uint8_t* row = b.row(y);
        for (int x = 0; x < b.width * b.components; x += b.components) {
            if (row[x] < 200) {
                ++ink;
            }
        }
    }
    return ink;
}

void expectMatchesReference(const Bitmap& actual, const std::wstring& name) {
    ot::MuContext& mu = ot::MuContext::global();
    const fs::path ref = refDir() / (name + L".png");
    const char* update = std::getenv("OTTISK_UPDATE_SIG_APPEARANCE");  // NOLINT(concurrency-mt-unsafe)
    if (update != nullptr && std::string(update) == "1") {
        fs::create_directories(refDir());
        ot::savePng(mu, actual, ref);
        return;
    }
    ASSERT_TRUE(fs::exists(ref)) << "нет эталона " << ot::paths::toUtf8(ref)
                                 << " (OTTISK_UPDATE_SIG_APPEARANCE=1 пересоздаёт)";
    const Bitmap expected = ot::loadPng(mu, ref);
    const ot::CompareResult cmp = ot::compareBitmaps(actual, expected);
    ot::FuzzyThreshold t;
    t.maxDelta = 24;  // сглаживание контуров между версиями растеризатора
    t.mse = 1.0;
    t.windowedMse = 60.0;
    t.ssim = 0.97;
    EXPECT_TRUE(cmp.within(t)) << ot::paths::toUtf8(name) << ": " << cmp.toJson();
}

}  // namespace

TEST(sig_appearance, lines_follow_flags_and_labels) {
    const AppearanceData d = sampleData();
    SignatureAppearance look;
    const std::string date = ot::sign::formatSignatureDate(d.time);
    EXPECT_EQ(ot::sign::appearanceLines(look, d),
              (std::vector<std::string>{"Digitally signed by АкроБатя тест", "Reason: Согласовано",
                                        "Location: Москва", "Date: " + date}));
    look.labels = russianLabels();
    look.text.dn = true;
    EXPECT_EQ(ot::sign::appearanceLines(look, d),
              (std::vector<std::string>{"Подписано цифровой подписью: АкроБатя тест",
                                        "DN: CN=АкроБатя тест, O=АкроБатя, C=RU", "Причина: Согласовано",
                                        "Место: Москва", "Дата: " + date}));
    look.text.labels = false;
    look.text.dn = false;
    look.text.date = false;
    EXPECT_EQ(ot::sign::appearanceLines(look, d),
              (std::vector<std::string>{"АкроБатя тест", "Согласовано", "Москва"}));
    AppearanceData empty = d;
    empty.reason.clear();
    empty.time = 0;
    look.text.date = true;
    EXPECT_EQ(ot::sign::appearanceLines(look, empty), (std::vector<std::string>{"АкроБатя тест", "Москва"}));
    look.imageOnly = true;
    EXPECT_TRUE(ot::sign::appearanceLines(look, d).empty()) << "«только изображение» — без текста";
}

TEST(sig_appearance, mupdf_flags_map_to_builder) {
    const int all = PDF_SIGNATURE_DEFAULT_APPEARANCE;
    const SignatureAppearance look = SignatureAppearance::fromFlags(all);
    EXPECT_EQ(look.graphic, AppearanceGraphic::Name);
    EXPECT_TRUE(look.text.name);
    EXPECT_TRUE(look.text.dn);
    EXPECT_TRUE(look.text.date);
    EXPECT_TRUE(look.text.labels);
    EXPECT_EQ(look.flags(), all & ~PDF_SIGNATURE_SHOW_LOGO) << "логотип MuPDF не рисуем — свой PNG";
    const SignatureAppearance bare = SignatureAppearance::fromFlags(PDF_SIGNATURE_SHOW_DATE);
    EXPECT_EQ(bare.graphic, AppearanceGraphic::None);
    EXPECT_FALSE(bare.text.name);
    EXPECT_TRUE(bare.text.date);
    EXPECT_EQ(bare.flags(), PDF_SIGNATURE_SHOW_DATE);
}

TEST(sig_appearance, date_like_acrobat) {
    const std::string date = ot::sign::formatSignatureDate(1793088000);
    EXPECT_TRUE(
        std::regex_match(date, std::regex(R"(\d{4}\.\d{2}\.\d{2} \d{2}:\d{2}:\d{2} [+-]\d{2}'\d{2}')")))
        << date;
}

TEST(sig_appearance, widget_snapshots_match_references) {
    OT_FEATURE("sign-13");
    ot::test::TempDir dir;
    const fs::path pen = makePng(dir.path() / L"роспись.png", 300, 120, true);
    const fs::path logo = makePng(dir.path() / L"логотип.png", 120, 120, false);

    SignatureAppearance nameText;  // имя слева, текст справа (без даты — эталон не зависит от пояса)
    nameText.labels = russianLabels();
    nameText.text.date = false;

    SignatureAppearance imageText = nameText;
    imageText.graphic = AppearanceGraphic::Image;
    imageText.image = pen;

    SignatureAppearance imageOnly;
    imageOnly.imageOnly = true;
    imageOnly.image = pen;

    SignatureAppearance textOnly = nameText;
    textOnly.graphic = AppearanceGraphic::None;
    textOnly.text.dn = true;

    SignatureAppearance withLogo = nameText;
    withLogo.logo = logo;

    const std::vector<std::pair<std::wstring, SignatureAppearance>> cases = {{L"name-text", nameText},
                                                                             {L"image-text", imageText},
                                                                             {L"image-only", imageOnly},
                                                                             {L"text-only", textOnly},
                                                                             {L"logo", withLogo}};
    for (const auto& [name, look] : cases) {
        const Bitmap widget = signedWidget(dir.path(), name, look);
        ASSERT_FALSE(widget.empty());
        EXPECT_GT(inkPixels(widget), 200) << ot::paths::toUtf8(name) << ": рамка пустая";
        expectMatchesReference(widget, name);
    }
}

TEST(sig_appearance, preview_matches_signed_widget) {
    ot::test::TempDir dir;
    SignatureAppearance look;
    look.labels = russianLabels();
    look.text.date = false;
    const Bitmap widget = signedWidget(dir.path(), L"предпросмотр", look);
    AppearanceData d = sampleData();
    d.name = "АкроБатя тест";
    const Bitmap preview = ot::sign::previewAppearance(look, d, kRect.width(), kRect.height(), kDpi);
    ASSERT_EQ(preview.width, widget.width);
    ASSERT_EQ(preview.height, widget.height);
    const ot::CompareResult cmp = ot::compareBitmaps(preview, widget);
    EXPECT_GE(cmp.ssim, 0.95) << cmp.toJson();
}

TEST(sig_appearance, missing_image_is_refused_before_editing) {
    ot::test::TempDir dir;
    const fs::path path = ot::test::unsignedCopy(dir.path(), L"нет-картинки.pdf");
    const std::string before = ot::paths::readFile(path);
    auto doc = Document::openOrThrow(path);
    SignRequest r;
    r.identity = ot::test::pkiIdentity("signer-rsa.pfx");
    r.placement.rect = kRect;
    r.appearance.graphic = AppearanceGraphic::Image;
    r.appearance.image = dir.path() / L"нет.png";
    try {
        (void)ot::sign::signDocument(*doc, r);
        ADD_FAILURE() << "картинки нет";
    } catch (const SignError& e) {
        EXPECT_EQ(e.status(), SignStatus::BadAppearance);
    }
    EXPECT_EQ(ot::paths::readFile(path), before);
    EXPECT_FALSE(doc->isSigned());
}
