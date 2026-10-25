// unit.fdf.* — обмен комментариями FDF (план 5.4; core/annot/fdf.h; comment-27, security-36): 14 создаваемых
// типов с ответом и статусом → FDF («%FDF-1.2», /FDF /Annots, /F, /Page) → импорт в пустую копию даёт те же
// аннотации (тип, /Rect, цвет, текст, автор, /NM, ответ с /IRT, вложение байт в байт) одним шагом истории;
// повторный импорт обновляет по /NM, а не удваивает; из 10-страничного в 2-страничный — 2 страницы и отчёт о
// 8 пропущенных; FDF с /F на другой документ — отказ.
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/annot/annots.h"
#include "core/annot/comments.h"
#include "core/annot/defaults.h"
#include "core/annot/fdf.h"
#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/doc/saver.h"
#include "core/links/attachments.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;
using ot::Document;
using ot::PointF;
using ot::QuadF;
using ot::annot::AnnotColor;
using ot::annot::AnnotInfo;
using ot::annot::AnnotProps;
using ot::annot::AnnotRef;
using ot::annot::AnnotType;

namespace {

std::unique_ptr<Document> blank(int pages = 2) {
    return Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), pages));
}

constexpr AnnotType kCreatable[] = {
    AnnotType::Text,      AnnotType::FreeText, AnnotType::Line,           AnnotType::Square,
    AnnotType::Circle,    AnnotType::Polygon,  AnnotType::PolyLine,       AnnotType::Highlight,
    AnnotType::Underline, AnnotType::Squiggly, AnnotType::StrikeOut,      AnnotType::Stamp,
    AnnotType::Caret,     AnnotType::Ink,      AnnotType::FileAttachment,
};

AnnotProps propsFor(AnnotType type, int i) {
    AnnotProps p = ot::annot::toolDefaults(type);
    p.contents = "Текст " + std::string(ot::annot::annotTypeName(type));
    p.author = i % 2 == 0 ? "Анна" : "Борис";
    p.color = AnnotColor::rgb(0.1f * static_cast<float>(i % 10), 0.4f, 0.8f);
    const float y = 60.0f + static_cast<float>(i) * 45.0f;
    switch (type) {
        case AnnotType::Line:
            p.lineStart = {60, y};
            p.lineEnd = {200, y + 20};
            break;
        case AnnotType::Polygon:
        case AnnotType::PolyLine:
            p.vertices = {{60, y}, {160, y}, {110, y + 30}};
            break;
        case AnnotType::Highlight:
        case AnnotType::Underline:
        case AnnotType::Squiggly:
        case AnnotType::StrikeOut:
            p.quads = {QuadF{{60, y}, {220, y}, {60, y + 14}, {220, y + 14}}};
            break;
        case AnnotType::Ink:
            p.ink = {{{60, y}, {90, y + 20}, {140, y + 5}}};
            break;
        case AnnotType::FileAttachment:
            p.rect = {60, y, 80, y + 20};
            p.fileName = "заметка.txt";
            p.fileData = "Привет, вложение!\n";
            break;
        case AnnotType::FreeText:
            p.rect = {60, y, 260, y + 30};
            p.color = AnnotColor{};
            break;
        default:
            p.rect = {60, y, 100, y + 30};
            break;
    }
    return p;
}

std::map<std::string, AnnotInfo> byNm(Document& doc, int page) {
    std::map<std::string, AnnotInfo> out;
    for (const AnnotInfo& a : ot::annot::listAnnots(doc, page)) {
        if (!a.ref.nm.empty()) {
            out[a.ref.nm] = a;
        }
    }
    return out;
}

void expectNear(const ot::RectF& a, const ot::RectF& b, float eps = 0.6f) {
    EXPECT_NEAR(a.x0, b.x0, eps);
    EXPECT_NEAR(a.y0, b.y0, eps);
    EXPECT_NEAR(a.x1, b.x1, eps);
    EXPECT_NEAR(a.y1, b.y1, eps);
}

// Удалить все аннотации страниц (пустая копия того же документа).
void clearAll(Document& doc) {
    for (int p = 0; p < doc.pageCount(); ++p) {
        std::vector<AnnotRef> refs;
        for (const AnnotInfo& a : ot::annot::listAnnots(doc, p)) {
            if (a.type != AnnotType::Popup) {
                refs.push_back(a.ref);
            }
        }
        if (!refs.empty()) {
            ot::annot::remove(doc, refs);
        }
    }
}

}  // namespace

TEST(fdf, round_trip_of_creatable_types) {
    OT_FEATURE("comment-27");
    auto src = blank(1);
    std::vector<AnnotRef> created;
    int i = 0;
    for (const AnnotType t : kCreatable) {
        created.push_back(ot::annot::create(*src, 0, propsFor(t, i++)));
    }
    const AnnotRef reply = ot::annot::addReply(*src, created[0], {.author = "Вера", .contents = "Ответ"});
    ot::annot::setStatus(*src, created[0], ot::annot::kReviewModel, "Accepted", "Вера");
    const auto before = byNm(*src, 0);

    const std::string fdf = ot::annot::exportFdf(*src, {}, "Документ.pdf");
    ASSERT_GT(fdf.size(), 100u);
    EXPECT_EQ(fdf.substr(0, 8), "%FDF-1.2");
    const ot::annot::ExchangeInfo info = ot::annot::probeFdf(fdf);
    EXPECT_EQ(info.file, "Документ.pdf");
    EXPECT_EQ(info.annots, static_cast<int>(std::size(kCreatable)) + 2);
    EXPECT_EQ(info.maxPage, 0);

    // Пустая копия того же документа.
    auto dst = blank(1);
    const ot::annot::ImportReport report = ot::annot::importFdf(*dst, fdf, "документ.PDF");
    EXPECT_EQ(report.created, static_cast<int>(std::size(kCreatable)) + 2);
    EXPECT_EQ(report.updated, 0);
    EXPECT_EQ(report.skipped, 0);
    EXPECT_EQ(dst->undoStack().undoName(), "Import comments");
    const auto after = byNm(*dst, 0);
    for (const AnnotRef& ref : created) {
        SCOPED_TRACE(ref.nm);
        ASSERT_TRUE(after.contains(ref.nm));
        const AnnotInfo& a = after.at(ref.nm);
        const AnnotInfo& b = before.at(ref.nm);
        EXPECT_EQ(a.type, b.type);
        expectNear(a.rect, b.rect);
        EXPECT_EQ(a.color, b.color);
        EXPECT_EQ(a.contents, b.contents);
        EXPECT_EQ(a.author, b.author);
        EXPECT_TRUE(a.hasAppearance);
    }
    // Ответ и статус — на перенесённый корень.
    const ot::annot::CommentTree tree = ot::annot::buildComments(*dst);
    const auto root = std::find_if(tree.roots.begin(), tree.roots.end(),
                                   [&](const ot::annot::Comment& c) { return c.ref.nm == created[0].nm; });
    ASSERT_NE(root, tree.roots.end());
    ASSERT_EQ(root->replies.size(), 1u);
    EXPECT_EQ(root->replies[0].ref.nm, reply.nm);
    EXPECT_EQ(root->replies[0].contents, "Ответ");
    EXPECT_EQ(root->review(), ot::annot::ReviewState::Accepted);
    // Вложение — байт в байт.
    const auto files = ot::attachments::list(*dst);
    const auto attached = std::find_if(files.begin(), files.end(),
                                       [](const ot::attachments::Attachment& f) { return f.page >= 0; });
    ASSERT_NE(attached, files.end());
    EXPECT_EQ(ot::attachments::contents(*dst, *attached), "Привет, вложение!\n");
    // Одна операция: undo убирает всё.
    dst->undoStack().undo();
    EXPECT_TRUE(byNm(*dst, 0).empty());
}

TEST(fdf, reimport_updates_by_name) {
    OT_FEATURE("comment-27");
    auto doc = blank(1);
    const AnnotRef a = ot::annot::create(*doc, 0, propsFor(AnnotType::Square, 1));
    const AnnotRef b = ot::annot::create(*doc, 0, propsFor(AnnotType::Text, 2));
    const std::string fdf = ot::annot::exportFdf(*doc, {}, "a.pdf");
    // Правка после экспорта; импорт возвращает текст по /NM, не удваивая.
    ot::annot::AnnotPatch patch;
    patch.contents = "изменено";
    ot::annot::update(*doc, a, patch);
    const ot::annot::ImportReport report = ot::annot::importFdf(*doc, fdf, "a.pdf");
    EXPECT_EQ(report.updated, 2);
    EXPECT_EQ(report.created, 0);
    const auto list = byNm(*doc, 0);
    EXPECT_EQ(list.size(), 2u);
    EXPECT_EQ(list.at(a.nm).contents, propsFor(AnnotType::Square, 1).contents);
    EXPECT_TRUE(list.contains(b.nm));
    // Окно записки у обновлённой — одно.
    int popups = 0;
    for (const AnnotInfo& info : ot::annot::listAnnots(*doc, 0)) {
        popups += info.type == AnnotType::Popup ? 1 : 0;
    }
    EXPECT_EQ(popups, 1);
}

TEST(fdf, pages_out_of_range_are_skipped_with_report) {
    OT_FEATURE("comment-27");
    auto ten = blank(10);
    for (int p = 0; p < 10; ++p) {
        AnnotProps n = propsFor(AnnotType::Text, p);
        n.contents = "страница " + std::to_string(p + 1);
        ot::annot::create(*ten, p, n);
    }
    const std::string fdf = ot::annot::exportFdf(*ten, {}, "ten.pdf");
    EXPECT_EQ(ot::annot::probeFdf(fdf).maxPage, 9);
    auto two = blank(2);
    const ot::annot::ImportReport report = ot::annot::importFdf(*two, fdf, "ten.pdf");
    EXPECT_EQ(report.created, 2);
    EXPECT_EQ(report.skipped, 8);
    EXPECT_EQ(report.skippedPages, (std::vector<int>{2, 3, 4, 5, 6, 7, 8, 9}));
    EXPECT_EQ(ot::annot::listAnnots(*two, 0).size(), 2u);  // записка и её окно
    EXPECT_EQ(ot::annot::listAnnots(*two, 1).size(), 2u);
}

TEST(fdf, foreign_document_is_refused) {
    OT_FEATURE("security-36");
    auto doc = blank(1);
    ot::annot::create(*doc, 0, propsFor(AnnotType::Square, 0));
    const std::string fdf = ot::annot::exportFdf(*doc, {}, "C:\\Папка\\Договор.pdf");
    EXPECT_TRUE(ot::annot::sameDocumentName("C:\\Папка\\Договор.pdf", "договор.PDF"));
    EXPECT_TRUE(ot::annot::sameDocumentName("", "что угодно.pdf"));
    EXPECT_FALSE(ot::annot::sameDocumentName("Договор.pdf", "Счёт.pdf"));
    auto other = blank(1);
    try {
        ot::annot::importFdf(*other, fdf, "Счёт.pdf");
        FAIL() << "foreign FDF imported";
    } catch (const ot::annot::ExchangeError& e) {
        EXPECT_EQ(e.kind(), ot::annot::ExchangeError::Kind::ForeignDocument);
        EXPECT_EQ(e.file(), "C:\\Папка\\Договор.pdf");
    }
    EXPECT_TRUE(ot::annot::listAnnots(*other, 0).empty());
    EXPECT_EQ(other->undoStack().size(), 0);
    // Не FDF.
    EXPECT_THROW(ot::annot::probeFdf("это не fdf"), ot::annot::ExchangeError);
}

TEST(fdf, selected_export_takes_branches) {
    OT_FEATURE("comment-27");
    auto doc = blank(1);
    const AnnotRef a = ot::annot::create(*doc, 0, propsFor(AnnotType::Square, 0));
    ot::annot::create(*doc, 0, propsFor(AnnotType::Circle, 1));
    ot::annot::addReply(*doc, a, {.author = "Вера", .contents = "к квадрату"});
    const std::string fdf = ot::annot::exportFdf(*doc, {a}, "a.pdf");
    EXPECT_EQ(ot::annot::probeFdf(fdf).annots, 2);  // квадрат и его ответ, без овала
}
