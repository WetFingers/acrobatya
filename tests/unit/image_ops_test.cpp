// unit.image_ops — картинки «Редактировать PDF» в ядре (план 3.4, п. 3; эталон 3.9 «Модель»: cm-матрица,
// W n перед Do, pdf_add_image; edit-06, edit-07, create-32). Ожидания — tests/fixtures/edit/images.json
// (генератор tools/gen/make_image_fixtures.py): две картинки — JPEG в группе q cm Do Q и Flate одиночным Do.
// Перемещение, масштаб, поворот на 90°, отражение — новая матрица и рамка; обрезка — обтравка в единицах
// картинки, видимая рамка — обрезанная; замена — новый ресурс OtIm1 с размером новой картинки на том же
// месте; добавление — третья картинка в рамке (CTM в конце страницы не единичная — группа всё равно на
// месте); удаление; экспорт — JPEG как есть (DCT) и PNG; undo каждой правки — потоки и рамки как были; рендер
// вне прежней и новой рамок — pixels == 0. rewriter_compat-подобный image_compat.DISABLED_write пишет файлы
// для compat.images (qpdf, pdfcpu strict, mutool, pypdfium2).
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/doc/saver.h"
#include "core/edit/content_model.h"
#include "core/edit/images.h"
#include "core/render/image_compare.h"
#include "core/render/renderer.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "services/document_session.h"
#include "services/edit_ops.h"
#include "support/qt_app.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;
using ot::Document;
using ot::PointF;
using ot::RectF;
using ot::edit::ImageOps;
using ot::edit::PageImage;

namespace {

fs::path editFixture(const std::string& name) {
    return ot::test::sourceDir() / "tests" / "fixtures" / "edit" / name;
}

nlohmann::json expected() {
    std::ifstream in(editFixture("images.json"));
    return nlohmann::json::parse(in);
}

bool near(float a, float b, float eps = 0.05f) { return std::fabs(a - b) <= eps; }

void expectRect(const RectF& r, const std::vector<float>& e, float eps = 0.05f) {
    EXPECT_TRUE(near(r.x0, e[0], eps) && near(r.y0, e[1], eps) && near(r.x1, e[2], eps) &&
                near(r.y1, e[3], eps))
        << "[" << r.x0 << " " << r.y0 << " " << r.x1 << " " << r.y1 << "] против [" << e[0] << " " << e[1]
        << " " << e[2] << " " << e[3] << "]";
}

std::string fileBytes(const fs::path& p) { return ot::paths::readFile(p); }

std::string logoPng() {
    return fileBytes(ot::test::sourceDir() / "tests" / "fixtures" / "images" / "logo.png");
}

// Содержимое потоков страницы после декодирования — для сравнения до/после undo.
std::string pageStreams(const Document& doc, int page = 0) {
    const ot::edit::ContentModel m = ot::edit::ContentModel::build(doc, page);
    std::string out;
    for (std::size_t s = 0; s < m.streams().size(); ++s) {
        if (!m.streams()[s].form) {
            out += m.streams()[s].bytes + "\n--\n";
        }
    }
    return out;
}

}  // namespace

TEST(image_ops, lists_images_with_bbox_and_group) {
    OT_FEATURE("edit-06");
    auto doc = Document::openOrThrow(editFixture("images.pdf"));
    const auto e = expected();
    const std::vector<PageImage> images = ImageOps(*doc).list(0);
    ASSERT_EQ(images.size(), e["images"].size());
    for (std::size_t i = 0; i < images.size(); ++i) {
        const auto& x = e["images"][i];
        SCOPED_TRACE(x["name"].get<std::string>());
        EXPECT_EQ(images[i].index, static_cast<int>(i));
        EXPECT_EQ(images[i].name, x["name"].get<std::string>());
        EXPECT_EQ(images[i].filter, x["filter"].get<std::string>());
        EXPECT_EQ(images[i].width, x["width"].get<int>());
        EXPECT_EQ(images[i].height, x["height"].get<int>());
        EXPECT_EQ(images[i].group, x["group"].get<bool>());
        expectRect(images[i].bbox, x["bbox"].get<std::vector<float>>());
        EXPECT_FALSE(images[i].clipped());
    }
    // Под точкой — картинка; мимо — нет.
    EXPECT_EQ(ot::edit::imageAt(images, PointF{100, 200}), 0);
    EXPECT_EQ(ot::edit::imageAt(images, PointF{400, 250}), 1);
    EXPECT_EQ(ot::edit::imageAt(images, PointF{50, 50}), -1);
}

TEST(image_ops, move_scale_and_undo) {
    auto doc = Document::openOrThrow(editFixture("images.pdf"));
    ImageOps ops(*doc);
    const std::string before = pageStreams(*doc);
    const ot::Renderer renderer(*doc);
    const ot::Bitmap original = renderer.renderPage(0, 72);
    ops.move(0, 0, PointF{30, 40});
    expectRect(ops.list(0)[0].bbox, {102, 208, 294, 352});
    expectRect(ops.list(0)[1].bbox, {320, 192, 520, 292});  // соседняя — на месте
    EXPECT_TRUE(ops.list(0)[0].group);
    // Рендер вне прежней и новой рамок не изменился.
    const ot::Bitmap moved = renderer.renderPage(0, 72);
    EXPECT_EQ(ot::diffPixelsOutside(original, moved,
                                    {ot::pixelRect(RectF{72, 168, 264, 312}, 72, 2),
                                     ot::pixelRect(RectF{102, 208, 294, 352}, 72, 2)}),
              0);
    EXPECT_GT(ot::diffPixelsOutside(original, moved, {}), 0);
    ASSERT_TRUE(doc->undoStack().canUndo());
    EXPECT_EQ(doc->undoStack().undoName(), "Move image");
    doc->undoStack().undo();
    EXPECT_EQ(pageStreams(*doc), before);
    expectRect(ops.list(0)[0].bbox, {72, 168, 264, 312});

    // Одиночный Do становится группой; масштаб — в рамку.
    ops.setRect(0, 1, RectF{300, 400, 400, 450});
    const PageImage im2 = ops.list(0)[1];
    EXPECT_TRUE(im2.group);
    expectRect(im2.bbox, {300, 400, 400, 450});
    expectRect(ops.list(0)[0].bbox, {72, 168, 264, 312});
}

TEST(image_ops, rotate_flip_crop) {
    auto doc = Document::openOrThrow(editFixture("images.pdf"));
    ImageOps ops(*doc);
    ops.rotate(0, 0, 90);
    PageImage im = ops.list(0)[0];
    // 192 × 144 вокруг центра (168, 240) → 144 × 192.
    expectRect(im.bbox, {96, 144, 240, 336});
    // По часовой: левый верхний угол картинки (ul) уходит вправо вверх.
    EXPECT_TRUE(near(im.quad.ul.x, 240) && near(im.quad.ul.y, 144)) << im.quad.ul.x << " " << im.quad.ul.y;
    doc->undoStack().undo();
    ops.flip(0, 0, true);
    im = ops.list(0)[0];
    expectRect(im.bbox, {72, 168, 264, 312});
    EXPECT_TRUE(near(im.quad.ul.x, 264)) << im.quad.ul.x;  // слева направо
    doc->undoStack().undo();
    ops.rotate(0, 0, 30);
    im = ops.list(0)[0];
    EXPECT_GT(im.bbox.width(), 192.0f);
    doc->undoStack().undo();

    OT_FEATURE("edit-07");
    ops.crop(0, 0, RectF{100, 180, 200, 300});
    im = ops.list(0)[0];
    EXPECT_TRUE(im.clipped());
    expectRect(im.bbox, {100, 180, 200, 300});
    EXPECT_NE(pageStreams(*doc).find("re W n"), std::string::npos);
    // Обрезанная картинка двигается вместе с обтравкой.
    ops.move(0, 0, PointF{10, 0});
    expectRect(ops.list(0)[0].bbox, {110, 180, 210, 300});
    // Обрезка по всей картинке — снять.
    ops.crop(0, 0, RectF{0, 0, 612, 792});
    EXPECT_FALSE(ops.list(0)[0].clipped());
    EXPECT_EQ(pageStreams(*doc).find("re W n"), std::string::npos);
    EXPECT_THROW(ops.crop(0, 0, RectF{500, 600, 510, 610}), std::invalid_argument);
}

TEST(image_ops, replace_add_remove) {
    auto doc = Document::openOrThrow(editFixture("images.pdf"));
    ImageOps ops(*doc);
    const std::string png = logoPng();
    const ot::edit::ImageData probe = ot::edit::probeImage(png);
    ASSERT_GT(probe.width, 0);
    ops.replace(0, 1, png);
    PageImage im = ops.list(0)[1];
    EXPECT_EQ(im.name, "OtIm1");
    EXPECT_EQ(im.width, probe.width);
    EXPECT_EQ(im.height, probe.height);
    expectRect(im.bbox, {320, 192, 520, 292});
    EXPECT_EQ(doc->undoStack().undoName(), "Replace image");

    const int added = ops.add(0, png, RectF{100, 600, 200, 700});
    EXPECT_EQ(added, 2);
    std::vector<PageImage> all = ops.list(0);
    ASSERT_EQ(all.size(), 3U);
    EXPECT_EQ(all[2].name, "OtIm2");
    expectRect(all[2].bbox, {100, 600, 200, 700});
    // Картинка стоит прямо: левый верхний угол — сверху слева.
    EXPECT_TRUE(near(all[2].quad.ul.x, 100) && near(all[2].quad.ul.y, 600));
    // Прежние на месте (CTM в конце страницы была не единичной — «q» в начале, «Q» перед группой).
    expectRect(all[0].bbox, {72, 168, 264, 312});

    ops.remove(0, 0);
    all = ops.list(0);
    ASSERT_EQ(all.size(), 2U);
    EXPECT_EQ(all[0].name, "OtIm1");
    doc->undoStack().undo();
    EXPECT_EQ(ops.list(0).size(), 3U);
}

TEST(image_ops, export_jpeg_as_is_and_png) {
    OT_FEATURE("create-32");
    auto doc = Document::openOrThrow(editFixture("images.pdf"));
    ImageOps ops(*doc);
    const ot::edit::ImageData jpg = ops.exportImage(0, 0);
    EXPECT_EQ(jpg.format, "jpg");
    ASSERT_GE(jpg.bytes.size(), 4U);
    EXPECT_EQ(static_cast<unsigned char>(jpg.bytes[0]), 0xFF);
    EXPECT_EQ(static_cast<unsigned char>(jpg.bytes[1]), 0xD8);
    EXPECT_EQ(ot::edit::probeImage(jpg.bytes).width, 64);
    const ot::edit::ImageData png = ops.exportImage(0, 1);
    EXPECT_EQ(png.format, "png");
    ASSERT_GE(png.bytes.size(), 8U);
    EXPECT_EQ(png.bytes.substr(1, 3), "PNG");
    EXPECT_EQ(ot::edit::probeImage(png.bytes).width, 32);
    EXPECT_EQ(ot::edit::probeImage("not an image").width, 0);
}

TEST(image_ops, signed_document_refuses) {
    auto doc = Document::openOrThrow(ot::test::sourceDir() / "tests" / "fixtures" / "sig" / "signed-bb.pdf");
    ImageOps ops(*doc);
    EXPECT_THROW(ops.add(0, logoPng(), RectF{10, 10, 50, 50}), std::runtime_error);
}

// Файлы для ctest compat.images (tools/compat_check.py): каждая правка — полная запись.
TEST(image_compat, DISABLED_write) {
    const char* outEnv = std::getenv("OTTISK_COMPAT_OUT");  // NOLINT(concurrency-mt-unsafe)
    ASSERT_NE(outEnv, nullptr) << "нужен OTTISK_COMPAT_OUT";
    const fs::path dir = ot::paths::fromUtf8(outEnv) / L"images";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir);
    auto write = [&](const char* name, const std::function<void(ImageOps&)>& edit) {
        auto doc = Document::openOrThrow(editFixture("images.pdf"));
        ImageOps ops(*doc);
        edit(ops);
        ot::SaveOptions options;
        options.mode = ot::SaveMode::Full;
        const ot::SaveReport saved = ot::Saver::save(*doc, dir / ot::paths::fromUtf8(name), options);
        ASSERT_TRUE(saved.ok()) << name << ": " << saved.message;
    };
    write("moved.pdf", [](ImageOps& o) { o.move(0, 0, PointF{30, 40}); });
    write("scaled.pdf", [](ImageOps& o) { o.setRect(0, 1, RectF{300, 400, 400, 450}); });
    write("rotated.pdf", [](ImageOps& o) { o.rotate(0, 0, 90); });
    write("flipped.pdf", [](ImageOps& o) { o.flip(0, 1, false); });
    write("cropped.pdf", [](ImageOps& o) { o.crop(0, 0, RectF{100, 180, 200, 300}); });
    write("replaced.pdf", [](ImageOps& o) { o.replace(0, 1, logoPng()); });
    write("added.pdf", [](ImageOps& o) { o.add(0, logoPng(), RectF{100, 600, 200, 700}); });
    write("removed.pdf", [](ImageOps& o) { o.remove(0, 0); });
}

TEST(image_ops, session_page_content_lists_images) {
    ot::test::qtApp();
    ot::DocumentSession session;
    ASSERT_TRUE(session.open(QString::fromStdWString(editFixture("images.pdf").wstring())));
    const ot::editing::PageContent content = ot::editing::pageContent(session, 0).result();
    EXPECT_EQ(content.images.size(), 2U);
    EXPECT_FALSE(content.blocks.empty());
}
