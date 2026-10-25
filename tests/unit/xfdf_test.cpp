// unit.xfdf.* — обмен комментариями XFDF (план 5.4; core/annot/xfdf.h + services/xfdf_xml.h; comment-27):
// матрица 28 подтипов annots-all.pdf (+ ответ, статус, /RC) → XML (пространство имён Adobe, <f href>,
// атрибуты по типам) → разбор → импорт в пустую копию документа даёт те же аннотации (тип, /Rect, цвет,
// текст, автор, /NM, ответы, окно записки, вложение байт в байт) для 26 элементов — окно /Popup идёт
// ребёнком записки, поле формы — не комментарий; XFDF Acrobat (coords, inklist, contents-richtext,
// hex-данные) читается; <f href> на другой документ — отказ; не XFDF — ошибка разбора.
#include <gtest/gtest.h>

#include <QByteArray>
#include <QString>

#include <algorithm>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "core/annot/annots.h"
#include "core/annot/comments.h"
#include "core/annot/defaults.h"
#include "core/annot/xfdf.h"
#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/links/attachments.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "services/xfdf_xml.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;
using ot::Document;
using ot::annot::AnnotInfo;
using ot::annot::AnnotRef;
using ot::annot::AnnotType;

namespace {

fs::path matrixPath() { return ot::test::sourceDir() / "tests" / "fixtures" / "annots" / "annots-all.pdf"; }

std::map<std::string, AnnotInfo> byNm(Document& doc, int page) {
    std::map<std::string, AnnotInfo> out;
    for (const AnnotInfo& a : ot::annot::listAnnots(doc, page)) {
        if (!a.ref.nm.empty()) {
            out[a.ref.nm] = a;
        }
    }
    return out;
}

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

std::string xmlText(const QByteArray& xml) {
    return std::string(xml.constData(), static_cast<std::size_t>(xml.size()));
}

}  // namespace

TEST(xfdf, matrix_round_trip_into_empty_copy) {
    OT_FEATURE("comment-27");
    auto src = Document::openOrThrow(matrixPath());
    const auto before = byNm(*src, 0);
    ASSERT_EQ(before.size(), 28u);
    const AnnotRef text = before.at("ottisk-01-text").ref;
    const AnnotRef reply = ot::annot::addReply(*src, text,
                                               {.author = "Анна",
                                                .contents = "Ответ жирным",
                                                .richContents = ot::annot::richContentsFrom(
                                                    {{.text = "Ответ "}, {.text = "жирным", .bold = true}})});
    ot::annot::setStatus(*src, text, ot::annot::kReviewModel, "Completed", "Анна");

    const ot::annot::XfdfDocument model = ot::annot::exportXfdf(*src, {}, "annots-all.pdf");
    // 28 подтипов: окно записки — ребёнок, поле формы — не комментарий; + ответ и статус.
    EXPECT_EQ(model.annots.size(), 26u + 2u);
    const QByteArray xml = ot::xfdf::write(model);
    const std::string s = xmlText(xml);
    EXPECT_NE(s.find("xmlns=\"http://ns.adobe.com/xfdf/\""), std::string::npos);
    EXPECT_NE(s.find("<f href=\"annots-all.pdf\""), std::string::npos);
    EXPECT_NE(s.find("<highlight "), std::string::npos);
    EXPECT_NE(s.find("coords=\""), std::string::npos);
    EXPECT_NE(s.find("<inklist>"), std::string::npos);
    EXPECT_NE(s.find("<gesture>"), std::string::npos);
    EXPECT_NE(s.find("<vertices>"), std::string::npos);
    EXPECT_NE(s.find("<popup "), std::string::npos);
    EXPECT_NE(s.find("<contents-richtext><body"), std::string::npos);
    EXPECT_NE(s.find("inreplyto=\"ottisk-01-text\""), std::string::npos);
    EXPECT_NE(s.find("statemodel=\"Review\""), std::string::npos);
    EXPECT_NE(s.find("encoding=\"base64\""), std::string::npos);
    EXPECT_EQ(s.find("<widget"), std::string::npos);

    const ot::annot::XfdfDocument parsed = ot::xfdf::read(xml);
    EXPECT_EQ(parsed.file, "annots-all.pdf");
    ASSERT_EQ(parsed.annots.size(), model.annots.size());
    EXPECT_EQ(ot::annot::probeXfdf(parsed).annots, static_cast<int>(model.annots.size()));

    // Пустая копия того же документа.
    auto dst = Document::openOrThrow(matrixPath());
    clearAll(*dst);
    ASSERT_EQ(ot::annot::listAnnots(*dst, 0).size(), 0u);
    const ot::annot::ImportReport report = ot::annot::importXfdf(*dst, parsed, "annots-all.pdf");
    EXPECT_EQ(report.created, static_cast<int>(model.annots.size()));
    EXPECT_EQ(dst->undoStack().undoName(), "Import comments");
    const auto after = byNm(*dst, 0);
    for (const auto& [nm, b] : before) {
        SCOPED_TRACE(nm + " " + b.subtype);
        if (b.type == AnnotType::Widget) {
            EXPECT_FALSE(after.contains(nm));  // данные форм — не XFDF аннотаций
            continue;
        }
        if (b.type == AnnotType::Popup) {
            continue;  // окно — у записки (ниже)
        }
        ASSERT_TRUE(after.contains(nm));
        const AnnotInfo& a = after.at(nm);
        EXPECT_EQ(a.type, b.type);
        EXPECT_EQ(a.subtype, b.subtype);
        // /Rect — тот же (рамка видимого: у значков NoZoom MuPDF берёт /BBox вида — сверяем /Rect через props
        // только у типов без вида; у прочих — рамку с допуском).
        EXPECT_NEAR(a.rect.x0, b.rect.x0, 2.0f);
        EXPECT_NEAR(a.rect.y0, b.rect.y0, 2.0f);
        EXPECT_NEAR(a.rect.x1, b.rect.x1, 2.0f);
        EXPECT_NEAR(a.rect.y1, b.rect.y1, 2.0f);
        EXPECT_EQ(a.color.toRgb(), b.color.toRgb());
        EXPECT_EQ(a.contents, b.contents);
        EXPECT_EQ(a.author, b.author);
        EXPECT_EQ(a.flags, b.flags);
    }
    // Окно записки — у записки, ответ с /RC и статус — на месте.
    EXPECT_NE(after.at("ottisk-01-text").popup, 0);
    const ot::annot::CommentTree tree = ot::annot::buildComments(*dst);
    const auto note = std::find_if(tree.roots.begin(), tree.roots.end(),
                                   [](const ot::annot::Comment& c) { return c.ref.nm == "ottisk-01-text"; });
    ASSERT_NE(note, tree.roots.end());
    ASSERT_EQ(note->replies.size(), 1u);
    EXPECT_EQ(note->replies[0].ref.nm, reply.nm);
    EXPECT_EQ(note->replies[0].author, "Анна");
    const auto spans = ot::annot::parseRichContents(note->replies[0].richContents);
    ASSERT_EQ(spans.size(), 2u);
    EXPECT_TRUE(spans[1].bold);
    EXPECT_EQ(note->review(), ot::annot::ReviewState::Completed);
    // Вложение — байт в байт.
    const auto files = ot::attachments::list(*dst);
    const auto attached = std::find_if(files.begin(), files.end(),
                                       [](const ot::attachments::Attachment& f) { return f.page == 0; });
    ASSERT_NE(attached, files.end());
    const auto srcFiles = ot::attachments::list(*src);
    const auto srcAttached = std::find_if(srcFiles.begin(), srcFiles.end(),
                                          [](const ot::attachments::Attachment& f) { return f.page == 0; });
    ASSERT_NE(srcAttached, srcFiles.end());
    EXPECT_EQ(attached->fileName, srcAttached->fileName);
    EXPECT_EQ(ot::attachments::contents(*dst, *attached), ot::attachments::contents(*src, *srcAttached));
    // Повторный импорт — обновление по /NM.
    const ot::annot::ImportReport again = ot::annot::importXfdf(*dst, parsed, "annots-all.pdf");
    EXPECT_EQ(again.created, 0);
    EXPECT_EQ(again.updated, static_cast<int>(model.annots.size()));
    EXPECT_EQ(byNm(*dst, 0).size(), after.size());
}

TEST(xfdf, reads_acrobat_xfdf) {
    OT_FEATURE("comment-27");
    const QByteArray xml = QByteArrayLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<xfdf xmlns=\"http://ns.adobe.com/xfdf/\" xml:space=\"preserve\"><annots>"
        "<highlight color=\"#FFFF00\" creationdate=\"D:20260101120000+03'00'\" flags=\"print\" "
        "date=\"D:20260102120000+03'00'\" name=\"hl-1\" page=\"0\" rect=\"100,700,300,714\" "
        "subject=\"Highlight\" title=\"Acrobat User\" "
        "coords=\"100,714,300,714,100,700,300,700\"><contents>Выделено</contents></highlight>"
        "<ink color=\"#0000FF\" flags=\"print\" name=\"ink-1\" page=\"0\" rect=\"50,500,150,560\" "
        "width=\"2\" "
        "title=\"Acrobat User\"><inklist><gesture>60,510;100,550;140,520</gesture></inklist></ink>"
        "<text color=\"#FFFF00\" flags=\"print,nozoom,norotate\" name=\"note-1\" page=\"0\" "
        "rect=\"400,700,420,720\" title=\"Acrobat User\" icon=\"Comment\">"
        "<contents-richtext><body xmlns=\"http://www.w3.org/1999/xhtml\" "
        "xmlns:xfa=\"http://www.xfa.org/schema/xfa-data/1.0/\" xfa:APIVersion=\"Acrobat:21.1.0\" "
        "xfa:spec=\"2.0.2\"><p dir=\"ltr\"><span style=\"font-weight:bold\">Важно</span> прочитать</p></body>"
        "</contents-richtext><popup flags=\"print,nozoom,norotate\" open=\"yes\" page=\"0\" "
        "rect=\"430,600,610,720\"/></text>"
        "<text name=\"rep-1\" page=\"0\" rect=\"400,700,420,720\" title=\"Другой\" inreplyto=\"note-1\" "
        "replyType=\"reply\" flags=\"print,nozoom,norotate\"><contents>Согласен</contents></text>"
        "<fileattachment name=\"fa-1\" page=\"0\" rect=\"30,30,50,50\" file=\"a.txt\" "
        "mimetype=\"text/plain\">"
        "<data MODE=\"RAW\" encoding=\"hex\" length=\"5\">48656C6C6F</data></fileattachment>"
        "</annots><f href=\"doc.pdf\"/></xfdf>");
    const ot::annot::XfdfDocument parsed = ot::xfdf::read(xml);
    ASSERT_EQ(parsed.annots.size(), 5u);
    EXPECT_EQ(parsed.file, "doc.pdf");
    EXPECT_EQ(parsed.annots[1].ink.size(), 1u);
    EXPECT_EQ(parsed.annots[1].ink[0].size(), 3u);
    EXPECT_TRUE(parsed.annots[2].hasPopup);
    EXPECT_NE(parsed.annots[2].richText.find("font-weight:bold"), std::string::npos);
    EXPECT_EQ(parsed.annots[4].fileData, "Hello");

    auto doc = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 1));
    const ot::annot::ImportReport report = ot::annot::importXfdf(*doc, parsed, "DOC.pdf");
    EXPECT_EQ(report.created, 5);
    const auto list = byNm(*doc, 0);
    ASSERT_TRUE(list.contains("hl-1"));
    EXPECT_EQ(list.at("hl-1").type, AnnotType::Highlight);
    EXPECT_EQ(list.at("hl-1").contents, "Выделено");
    EXPECT_EQ(list.at("hl-1").color.toRgb(), 0xFFFF00u);
    EXPECT_TRUE(list.at("hl-1").hasAppearance);
    const auto props = ot::annot::readProps(*doc, list.at("hl-1").ref);
    ASSERT_TRUE(props.has_value());
    ASSERT_EQ(props->quads.size(), 1u);
    EXPECT_EQ(list.at("ink-1").type, AnnotType::Ink);
    const auto ink = ot::annot::readProps(*doc, list.at("ink-1").ref);
    ASSERT_TRUE(ink.has_value());
    ASSERT_EQ(ink->ink.size(), 1u);
    EXPECT_EQ(ink->ink[0].size(), 3u);
    EXPECT_NEAR(ink->borderWidth, 2.0f, 0.01f);
    // Простой текст из /RC, окно записки, ответ.
    EXPECT_EQ(list.at("note-1").contents, "Важно прочитать");
    EXPECT_NE(list.at("note-1").popup, 0);
    EXPECT_TRUE(list.at("note-1").open);
    const ot::annot::CommentTree tree = ot::annot::buildComments(*doc);
    const auto note = std::find_if(tree.roots.begin(), tree.roots.end(),
                                   [](const ot::annot::Comment& c) { return c.ref.nm == "note-1"; });
    ASSERT_NE(note, tree.roots.end());
    ASSERT_EQ(note->replies.size(), 1u);
    EXPECT_EQ(note->replies[0].contents, "Согласен");
    const auto files = ot::attachments::list(*doc);
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files[0].fileName, "a.txt");
    EXPECT_EQ(ot::attachments::contents(*doc, files[0]), "Hello");
}

TEST(xfdf, foreign_href_refused_and_garbage_rejected) {
    OT_FEATURE("security-36");
    auto doc = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 1));
    ot::annot::XfdfDocument x;
    x.file = "Договор.pdf";
    ot::annot::XfdfAnnot a;
    a.element = "square";
    a.set("page", "0");
    a.set("rect", "10,10,50,50");
    a.set("name", "sq");
    x.annots.push_back(a);
    try {
        ot::annot::importXfdf(*doc, x, "Счёт.pdf");
        FAIL() << "foreign XFDF imported";
    } catch (const ot::annot::ExchangeError& e) {
        EXPECT_EQ(e.kind(), ot::annot::ExchangeError::Kind::ForeignDocument);
    }
    EXPECT_TRUE(ot::annot::listAnnots(*doc, 0).empty());
    EXPECT_THROW(ot::xfdf::read(QByteArrayLiteral("<html><body/></html>")), ot::annot::ExchangeError);
    EXPECT_THROW(ot::xfdf::read(QByteArrayLiteral("not xml at all <")), ot::annot::ExchangeError);
    EXPECT_EQ(ot::annot::parseNumbers("1,2.5; 3 -4"), (std::vector<float>{1.0f, 2.5f, 3.0f, -4.0f}));
    EXPECT_EQ(ot::annot::formatNumbers({1.0f, 2.5f, 3.25f}), "1,2.5,3.25");
    EXPECT_EQ(ot::annot::xfdfElement(AnnotType::ThreeD), "threed");
    EXPECT_EQ(ot::annot::xfdfType("freetext"), AnnotType::FreeText);
    EXPECT_EQ(ot::annot::xfdfElement(AnnotType::Widget), "");
}

TEST(xfdf, freetext_cyrillic_and_page_skip) {
    OT_FEATURE("comment-27");
    auto src = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 3));
    ot::annot::AnnotProps ft = ot::annot::toolDefaults(AnnotType::FreeText);
    ft.rect = {60, 60, 300, 100};
    ft.contents = "Привет, мир";
    ft.author = "Анна";
    ft.fontName = "Segoe UI";
    const AnnotRef ref = ot::annot::create(*src, 2, ft);
    ot::annot::AnnotProps n = ot::annot::toolDefaults(AnnotType::Text);
    n.rect = {60, 200, 80, 220};
    n.contents = "на первой";
    ot::annot::create(*src, 0, n);
    const QByteArray xml = ot::xfdf::write(ot::annot::exportXfdf(*src, {}, "x.pdf"));
    auto dst = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 1));
    const ot::annot::ImportReport report = ot::annot::importXfdf(*dst, ot::xfdf::read(xml), "x.pdf");
    EXPECT_EQ(report.created, 1);
    EXPECT_EQ(report.skipped, 1);
    EXPECT_EQ(report.skippedPages, (std::vector<int>{2}));
    // Тот же документ — поле на месте, текст кириллицей, свой шрифт /DA.
    auto same = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 3));
    ot::annot::importXfdf(*same, ot::xfdf::read(xml), "x.pdf");
    const auto list = byNm(*same, 2);
    ASSERT_TRUE(list.contains(ref.nm));
    EXPECT_EQ(list.at(ref.nm).contents, "Привет, мир");
    EXPECT_TRUE(list.at(ref.nm).hasAppearance);
    const auto props = ot::annot::readProps(*same, list.at(ref.nm).ref);
    ASSERT_TRUE(props.has_value());
    EXPECT_EQ(props->fontName, "Segoe UI");
}
