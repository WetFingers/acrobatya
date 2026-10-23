// unit.selection.*, unit.clipboard_io.* — выделение и копирование (эталон 3.2, план 1.4): поток от точки до
// точки (две строки → текст с переводом строки), привязка к словам и строкам (двойной и тройной щелчок),
// прямоугольник (столбцы, Alt+рамка), «Выделить всё», диапазон из совпадения поиска; текст и HTML (жирный и
// курсив по шрифтам); буфер обмена — text/plain + text/html, снимок области — PNG при заданном dpi.
// Канарейка: выделение MuPDF 1.28.5 не заходит в struct-блоки — на странице с SEGMENT ничего не выделяет
// (поэтому выделение своё).
#include <gtest/gtest.h>

#include <QBuffer>
#include <QFuture>
#include <QImage>
#include <QImageReader>
#include <QMimeData>

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>

#include <nlohmann/json.hpp>

#include "core/doc/document.h"
#include "core/mu/context.h"
#include "core/text/search.h"
#include "core/text/selection.h"
#include "core/text/stext.h"
#include "core/util/features.h"
#include "services/clipboard_io.h"
#include "services/document_session.h"
#include "support/qt_app.h"
#include "support/test_pdf.h"

using ot::Document;
using ot::PointF;
using ot::SnapMode;
using ot::TextPage;
using ot::TextSelection;

namespace fs = std::filesystem;

namespace {

fs::path textFixture(const char* name) {
    return ot::test::sourceDir() / "tests" / "fixtures" / "text" / name;
}

struct Line {
    float x0, x1, baseline, size;
};

Line lineOf(const char* name) {
    std::ifstream in(textFixture("text.json"), std::ios::binary);
    const auto meta = nlohmann::json::parse(in);
    for (const auto& l : meta["files"]["search.pdf"]["lines"]) {
        if (l["name"] == name) {
            return {l["x0"].get<float>(), l["x1"].get<float>(), l["baseline"].get<float>(),
                    l["size"].get<float>()};
        }
    }
    ADD_FAILURE() << "no line " << name;
    return {};
}

// Середина строки по высоте (над базовой линией на треть кегля).
PointF leftOf(const Line& l) { return {l.x0 + 0.5f, l.baseline - l.size / 3}; }
PointF rightOf(const Line& l) { return {l.x1 - 0.5f, l.baseline - l.size / 3}; }

class SelectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        doc_ = Document::openOrThrow(textFixture("search.pdf"));
        page_ = TextPage::build(*doc_, 0);
    }
    std::unique_ptr<Document> doc_;
    std::shared_ptr<const TextPage> page_;
};

}  // namespace

TEST_F(SelectionTest, two_lines_give_text_with_line_break) {
    OT_FEATURE("view-33");
    const Line l1 = lineOf("select1");
    const Line l2 = lineOf("select2");
    const TextSelection s = ot::selectStream(*page_, leftOf(l1), rightOf(l2));
    ASSERT_FALSE(s.empty());
    EXPECT_EQ(ot::toPlainText(*page_, s), "Первая строка для выделения.\nВторая строка для выделения.");
    ASSERT_EQ(s.quads.size(), 2u);  // по кваду на строку
    EXPECT_NEAR(s.quads[0].bounds().x0, l1.x0, 0.5f);
    EXPECT_NEAR(s.quads[1].bounds().x1, l2.x1, 0.5f);
    // Обратный порядок точек — то же выделение.
    const TextSelection r = ot::selectStream(*page_, rightOf(l2), leftOf(l1));
    EXPECT_EQ(r.chars, s.chars);
    // Одна точка без привязки — пусто.
    EXPECT_TRUE(ot::selectStream(*page_, leftOf(l1), leftOf(l1)).empty());
}

TEST_F(SelectionTest, partial_line_from_the_middle) {
    const Line l1 = lineOf("select1");
    const auto words = page_->words();
    const ot::TextWord* w = nullptr;
    for (const auto& x : words) {
        if (x.text == "строка" && x.bbox.y1 > l1.baseline - 1 && x.bbox.y1 < l1.baseline + 5) {
            w = &x;
        }
    }
    ASSERT_NE(w, nullptr);
    // От левого края «строка» до правого края строки.
    const TextSelection s = ot::selectStream(*page_, {w->bbox.x0 + 0.1f, l1.baseline - 3}, rightOf(l1));
    EXPECT_EQ(ot::toPlainText(*page_, s), "строка для выделения.");
}

TEST_F(SelectionTest, snap_to_words_and_lines) {
    OT_FEATURE("view-33");
    const Line l1 = lineOf("select1");
    const auto words = page_->words();
    const ot::TextWord* w = nullptr;
    for (const auto& x : words) {
        if (x.text == "выделения." && x.bbox.y1 < l1.baseline + 5) {
            w = &x;
            break;
        }
    }
    ASSERT_NE(w, nullptr);
    const PointF mid{(w->bbox.x0 + w->bbox.x1) / 2, (w->bbox.y0 + w->bbox.y1) / 2};
    const TextSelection word = ot::selectStream(*page_, mid, mid, SnapMode::Words);
    EXPECT_EQ(ot::toPlainText(*page_, word), "выделения");
    const TextSelection line = ot::selectStream(*page_, mid, mid, SnapMode::Lines);
    EXPECT_EQ(ot::toPlainText(*page_, line), "Первая строка для выделения.");
    // Протянуть словами через две строки: от середины «Первая» до середины «Вторая».
    const Line l2 = lineOf("select2");
    const TextSelection across = ot::selectStream(*page_, {l1.x0 + 10, l1.baseline - 3},
                                                  {l2.x0 + 10, l2.baseline - 3}, SnapMode::Words);
    EXPECT_EQ(ot::toPlainText(*page_, across), "Первая строка для выделения.\nВторая");
}

TEST_F(SelectionTest, rectangle_selects_columns) {
    OT_FEATURE("view-33");
    const Line l1 = lineOf("select1");
    const Line l2 = lineOf("select2");
    // Ширина «Первая строка» — до середины последней буквы «строка» во второй строке.
    const auto words = page_->words();
    float right = 0;
    for (const auto& x : words) {
        if (x.text == "строка" && x.bbox.y1 > l2.baseline - 1 && x.bbox.y1 < l2.baseline + 5) {
            right = x.bbox.x1 - 1.0f;
        }
    }
    ASSERT_GT(right, 0);
    const ot::RectF area{l1.x0 - 2, l1.baseline - l1.size, right, l2.baseline + 2};
    const TextSelection s = ot::selectRectangle(*page_, area);
    EXPECT_TRUE(s.rectangle);
    EXPECT_EQ(ot::toPlainText(*page_, s), "Первая строка\nВторая строка");
    EXPECT_EQ(s.quads.size(), 2u);
    EXPECT_TRUE(ot::selectRectangle(*page_, {500, 800, 550, 820}).empty());
}

TEST_F(SelectionTest, select_all_and_html_styles) {
    OT_FEATURE("view-33");
    const TextSelection all = ot::selectAll(*page_);
    EXPECT_EQ(all.chars.size(), page_->chars().size());
    const std::string plain = ot::toPlainText(*page_, all);
    EXPECT_NE(plain.find("Оттиск: поиск и выделение текста"), std::string::npos);
    EXPECT_NE(plain.find("Красная строка"), std::string::npos);

    const std::string html = ot::toHtml(*page_, all);
    EXPECT_EQ(html.rfind("<html>", 0), 0u) << html.substr(0, 80);
    EXPECT_NE(html.find("<meta charset=\"utf-8\">"), std::string::npos);
    EXPECT_NE(html.find("<b>Жирная строка</b>"), std::string::npos) << html;
    EXPECT_NE(html.find("<i>Курсивная строка</i>"), std::string::npos) << html;
    EXPECT_NE(html.find("<b>Оттиск: поиск и выделение текста</b>"), std::string::npos);
    EXPECT_EQ(html.find("<b>Первая"), std::string::npos);  // обычный шрифт — без тегов

    // Экранирование и только выделенное.
    const Line l1 = lineOf("select1");
    const TextSelection one = ot::selectStream(*page_, leftOf(l1), rightOf(l1));
    const std::string h1 = ot::toHtml(*page_, one);
    EXPECT_NE(h1.find("Первая строка для выделения."), std::string::npos);
    EXPECT_EQ(h1.find("Вторая"), std::string::npos);
    EXPECT_EQ(ot::htmlEscape("a<b & \"c\">"), "a&lt;b &amp; &quot;c&quot;&gt;");
}

TEST_F(SelectionTest, range_from_search_hit) {
    const auto lean = TextPage::build(*doc_, 0, ot::SearchSession::textOptions());
    const auto hits = ot::searchPage(*lean, "оттиска");
    ASSERT_EQ(hits.size(), 1u);
    const TextSelection s = ot::selectRange(*lean, hits[0].firstChar, hits[0].lastChar);
    EXPECT_EQ(ot::toPlainText(*lean, s), "оттиска");
    ASSERT_EQ(s.quads.size(), 1u);
    EXPECT_NEAR(s.quads[0].bounds().x0, hits[0].quads[0].quad.bounds().x0, 0.01f);
    EXPECT_TRUE(ot::selectRange(*lean, 5, 2).empty());
}

TEST_F(SelectionTest, mupdf_selection_ignores_struct_blocks) {
    // Канарейка MuPDF 1.28.5: fz_copy_selection/fz_highlight_selection/fz_copy_rectangle обходят только
    // верхний уровень блоков, а SEGMENT кладёт текст в struct-узлы «Split» — выделять нечего. Когда MuPDF это
    // починит, тест покраснеет: тогда можно вернуться к функциям MuPDF (core/text/selection.h).
    const Line l1 = lineOf("select1");
    const Line l2 = lineOf("select2");
    std::unique_ptr<ot::MuContext> temp;
    fz_context* ctx = page_->contextForThisThread(temp);
    const fz_point a{leftOf(l1).x, leftOf(l1).y};
    const fz_point b{rightOf(l2).x, rightOf(l2).y};
    char* segmented = fz_copy_selection(ctx, page_->stext(), a, b, 0);
    EXPECT_STREQ(segmented, "");
    fz_free(ctx, segmented);
    const auto lean = TextPage::build(*doc_, 0, ot::SearchSession::textOptions());
    char* flat = fz_copy_selection(ctx, lean->stext(), a, b, 0);
    EXPECT_STREQ(flat, "Первая строка для выделения.\nВторая строка для выделения.");
    fz_free(ctx, flat);
    // Своё выделение одинаково на обоих слоях.
    EXPECT_EQ(ot::toPlainText(*lean, ot::selectStream(*lean, leftOf(l1), rightOf(l2))),
              ot::toPlainText(*page_, ot::selectStream(*page_, leftOf(l1), rightOf(l2))));
}

TEST(clipboard_io, text_mime_has_plain_and_html) {
    OT_FEATURE("view-33");
    ot::test::qtApp();
    auto doc = Document::openOrThrow(textFixture("search.pdf"));
    const auto page = TextPage::build(*doc, 0);
    const TextSelection all = ot::selectAll(*page);
    const auto mime = ot::ClipboardIo::textMime(*page, all);
    ASSERT_TRUE(mime);
    EXPECT_TRUE(mime->hasText());
    EXPECT_TRUE(mime->hasHtml());
    EXPECT_TRUE(mime->text().contains(QStringLiteral("Жирная строка")));
    EXPECT_TRUE(mime->html().contains(QStringLiteral("<b>Жирная строка</b>")));
    EXPECT_EQ(mime->text(), QString::fromStdString(ot::toPlainText(*page, all)));
    // Без QGuiApplication буфера обмена нет — честный отказ, не падение.
    EXPECT_FALSE(ot::ClipboardIo::put(ot::ClipboardIo::textMime(*page, all)));
}

TEST(clipboard_io, snapshot_area_as_png_at_dpi) {
    OT_FEATURE("view-33");
    ot::test::qtApp();
    ot::DocumentSession session;
    ASSERT_TRUE(session.open(QString::fromStdWString(textFixture("search.pdf").wstring())));
    // Область заголовка 216 × 36 pt (3 × 0,5 дюйма) при 300 dpi → 900 × 150 px.
    const QRectF area(72, 60, 216, 36);
    const QImage shot = ot::ClipboardIo::snapshot(session, 0, area, ot::ClipboardIo::kHighDpi).result();
    ASSERT_FALSE(shot.isNull());
    EXPECT_EQ(shot.size(), QSize(900, 150));
    EXPECT_NEAR(shot.dotsPerMeterX(), 300 / 0.0254, 1.0);
    int dark = 0;
    for (int y = 0; y < shot.height(); ++y) {
        for (int x = 0; x < shot.width(); ++x) {
            dark += qGray(shot.pixel(x, y)) < 128 ? 1 : 0;
        }
    }
    EXPECT_GT(dark, 1000);  // заголовок 20 pt жирным
    const QImage screen = ot::ClipboardIo::snapshot(session, 0, area, 96).result();
    EXPECT_EQ(screen.size(), QSize(288, 48));

    const auto mime = ot::ClipboardIo::imageMime(shot);
    ASSERT_TRUE(mime->hasFormat(QStringLiteral("image/png")));
    QByteArray png = mime->data(QStringLiteral("image/png"));
    EXPECT_TRUE(png.startsWith("\x89PNG"));
    QBuffer buf(&png);
    QImageReader reader(&buf, "png");
    const QImage back = reader.read();
    EXPECT_EQ(back.size(), shot.size());
    EXPECT_TRUE(mime->hasImage());
    session.close();
}

// Выделение через страницы (Ctrl+C в виде, 2.2b): текст страниц через «\n», HTML — один документ с
// фрагментами страниц подряд.
TEST(clipboard_io, text_mime_of_several_pages) {
    ot::test::qtApp();
    auto doc = Document::openOrThrow(textFixture("search.pdf"));
    const auto first = TextPage::build(*doc, 0);
    const auto second = TextPage::build(*doc, 1);
    const TextSelection a = ot::selectAll(*first);
    const TextSelection b = ot::selectAll(*second);
    const TextSelection none;
    const auto mime =
        ot::ClipboardIo::textMime({{first.get(), &a}, {second.get(), &none}, {second.get(), &b}});
    ASSERT_TRUE(mime);
    EXPECT_EQ(mime->text(),
              QString::fromStdString(ot::toPlainText(*first, a) + "\n" + ot::toPlainText(*second, b)));
    const QString html = mime->html();
    EXPECT_TRUE(html.startsWith(QStringLiteral("<html><head><meta charset=\"utf-8\"></head><body>")));
    EXPECT_EQ(html.count(QStringLiteral("<body>")), 1);
    EXPECT_TRUE(html.contains(QString::fromStdString(ot::toHtmlFragment(*first, a))));
    EXPECT_TRUE(html.contains(QString::fromStdString(ot::toHtmlFragment(*second, b))));
    EXPECT_EQ(QString::fromStdString(ot::toHtml(*first, a)),
              QString::fromStdString(ot::htmlDocument(ot::toHtmlFragment(*first, a))));
    EXPECT_FALSE(ot::ClipboardIo::textMime({{first.get(), &none}})->hasHtml());
}
