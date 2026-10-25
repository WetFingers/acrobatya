// unit.comments_model.* — модель комментариев 5.4 (core/annot/comments.h, services/read_state.h): дерево из
// annots-all.pdf + 3 ответа (один — на ответ) + 2 статуса строится верно и переживает инкрементальную запись;
// ответы и статусы скрыты на странице и не выбираются; Popup и не-комментарии в дерево не входят, ответ без
// родителя — корень; группа «Заменить текст» — одна запись; ветка удаляется целиком; сортировка по каждому
// ключу и фильтр по автору/типу/статусу/цвету/поиску меняют список и скрытые на странице; /RC туда и обратно;
// «прочитано» переживает перезапуск службы.
#include <gtest/gtest.h>

#include <QString>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/annot/annots.h"
#include "core/annot/comments.h"
#include "core/annot/defaults.h"
#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/doc/saver.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "services/read_state.h"
#include "services/state_db.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;
using ot::Document;
using ot::RectF;
using ot::annot::AnnotColor;
using ot::annot::AnnotProps;
using ot::annot::AnnotRef;
using ot::annot::AnnotType;
using ot::annot::Comment;
using ot::annot::CommentFilter;
using ot::annot::CommentSort;
using ot::annot::CommentTree;
using ot::annot::ReviewState;
using ot::annot::RichSpan;

namespace {

fs::path fixture(const char* name) { return ot::test::sourceDir() / "tests" / "fixtures" / "annots" / name; }

fs::path copyAll(const ot::test::TempDir& dir) {
    const fs::path path = dir.path() / L"комментарии.pdf";
    fs::copy_file(fixture("annots-all.pdf"), path);
    return path;
}

fs::path blankFile(const ot::test::TempDir& dir, const char* name, int pages = 2) {
    const fs::path path = dir.path() / name;
    ot::paths::writeFile(path, ot::test::makePdf(ot::MuContext::global(), pages));
    return path;
}

const Comment* rootByNm(const CommentTree& tree, const std::string& nm) {
    for (const Comment& c : tree.roots) {
        if (c.ref.nm == nm) {
            return &c;
        }
    }
    return nullptr;
}

AnnotProps note(float x, float y, const std::string& author, const std::string& text, std::int64_t when) {
    AnnotProps p = ot::annot::toolDefaults(AnnotType::Text);
    p.rect = {x, y, x + 20, y + 20};
    p.author = author;
    p.contents = text;
    p.created = when;
    p.modified = when;
    return p;
}

AnnotProps square(float x, float y, const std::string& author, const std::string& text, std::int64_t when,
                  AnnotColor color) {
    AnnotProps p = ot::annot::toolDefaults(AnnotType::Square);
    p.rect = {x, y, x + 60, y + 40};
    p.author = author;
    p.contents = text;
    p.created = when;
    p.modified = when;
    p.color = color;
    return p;
}

std::vector<std::string> contentsOf(const std::vector<const Comment*>& list) {
    std::vector<std::string> out;
    for (const Comment* c : list) {
        out.push_back(c->contents);
    }
    return out;
}

// Документ с пятью комментариями разных авторов, дат, типов, страниц и статусов.
struct Sample {
    ot::test::TempDir dir;
    std::unique_ptr<Document> doc;
    AnnotRef a, b, c, d, e;

    Sample() {
        doc = Document::openOrThrow(blankFile(dir, "sample.pdf", 2));
        const AnnotColor red = AnnotColor::rgb(1, 0, 0);
        const AnnotColor blue = AnnotColor::rgb(0, 0, 1);
        a = ot::annot::create(*doc, 1, note(50, 50, "Борис", "альфа", 1'700'000'300));
        b = ot::annot::create(*doc, 0, square(100, 300, "Анна", "Бета Проверка", 1'700'000'100, red));
        c = ot::annot::create(*doc, 0, note(50, 500, "Вера", "гамма", 1'700'000'200));
        d = ot::annot::create(*doc, 1, square(200, 300, "Анна", "дельта", 1'700'000'500, blue));
        e = ot::annot::create(*doc, 0, note(300, 50, "Борис", "эпсилон", 1'700'000'400));
        ot::annot::setStatus(*doc, b, ot::annot::kReviewModel, "Accepted", "Вера");
        ot::annot::setStatus(*doc, d, ot::annot::kReviewModel, "Rejected", "Борис");
        ot::annot::setStatus(*doc, e, ot::annot::kReviewModel, "Completed", "Анна");
        ot::annot::setStatus(*doc, c, ot::annot::kMarkedModel, "Marked", "Анна");
    }
};

}  // namespace

TEST(comments_model, tree_from_matrix_with_replies_and_statuses) {
    OT_FEATURE("comment-20");
    OT_FEATURE("comment-21");
    ot::test::TempDir dir;
    const fs::path path = copyAll(dir);
    std::vector<std::string> replyNms;
    {
        auto doc = Document::openOrThrow(path);
        const CommentTree before = ot::annot::buildComments(*doc);
        // 28 подтипов: комментарии — 14 создаваемых, Stamp-вложение уже среди них, Redact; без Popup, Link,
        // Widget, Sound и служебных.
        ASSERT_EQ(before.roots.size(), 16u);
        EXPECT_EQ(before.count(), 16);
        for (const Comment& c : before.roots) {
            EXPECT_TRUE(ot::annot::isComment(c.type)) << c.subtype;
            EXPECT_NE(c.type, AnnotType::Popup);
            EXPECT_TRUE(c.replies.empty());
        }
        const Comment* text = rootByNm(before, "ottisk-01-text");
        const Comment* sq = rootByNm(before, "ottisk-05-square");
        ASSERT_NE(text, nullptr);
        ASSERT_NE(sq, nullptr);
        EXPECT_EQ(text->contents, "Заметка с всплывающим окном в ячейке 15");

        const AnnotRef r1 = ot::annot::addReply(*doc, text->ref, {.author = "Анна", .contents = "Согласна"});
        EXPECT_EQ(doc->undoStack().undoName(), "Add reply");
        const AnnotRef r2 = ot::annot::addReply(
            *doc, r1,
            {.author = "Борис",
             .contents = "@Анна спасибо",
             .richContents =
                 ot::annot::richContentsFrom({{.text = "@Анна ", .bold = true}, {.text = "спасибо"}})});
        const AnnotRef r3 = ot::annot::addReply(*doc, sq->ref, {.author = "Вера", .contents = "Квадрат"});
        ot::annot::setStatus(*doc, text->ref, ot::annot::kReviewModel, "Accepted", "Анна");
        EXPECT_EQ(doc->undoStack().undoName(), "Set status");
        ot::annot::setStatus(*doc, sq->ref, ot::annot::kMarkedModel, "Marked", "Вера");
        EXPECT_EQ(doc->undoStack().undoName(), "Set check mark");
        replyNms = {r1.nm, r2.nm, r3.nm};

        // Ответы и статусы скрыты на странице, окна записки у них нет, на странице не выбираются.
        for (const auto& info : ot::annot::listAnnots(*doc, 0)) {
            if (info.inReplyTo != 0 && info.replyType != "Group") {
                EXPECT_TRUE(info.hiddenOnScreen()) << info.ref.nm;
                EXPECT_EQ(info.popup, 0) << info.ref.nm;
                EXPECT_TRUE(ot::annot::isServiceAnnot(info));
            }
        }
        const auto hit = ot::annot::annotAt(*doc, 0, {text->rect.x0 + 5, text->rect.y0 + 5});
        ASSERT_TRUE(hit.has_value());
        EXPECT_EQ(hit->ref.nm, "ottisk-01-text");

        const ot::SaveReport report = ot::Saver::save(*doc, path, {.mode = ot::SaveMode::Incremental});
        ASSERT_TRUE(report.ok()) << report.message;
    }
    auto doc = Document::openOrThrow(path);
    const CommentTree tree = ot::annot::buildComments(*doc);
    ASSERT_EQ(tree.roots.size(), 16u);
    EXPECT_EQ(tree.count(), 19);
    const Comment* text = rootByNm(tree, "ottisk-01-text");
    const Comment* sq = rootByNm(tree, "ottisk-05-square");
    ASSERT_NE(text, nullptr);
    ASSERT_NE(sq, nullptr);
    ASSERT_EQ(text->replies.size(), 1u);
    EXPECT_EQ(text->replyCount(), 2);
    const Comment& first = text->replies[0];
    EXPECT_EQ(first.ref.nm, replyNms[0]);
    EXPECT_EQ(first.author, "Анна");
    EXPECT_EQ(first.contents, "Согласна");
    EXPECT_EQ(first.depth, 1);
    ASSERT_EQ(first.replies.size(), 1u);
    const Comment& second = first.replies[0];
    EXPECT_EQ(second.ref.nm, replyNms[1]);
    EXPECT_EQ(second.depth, 2);
    EXPECT_EQ(second.contents, "@Анна спасибо");
    const std::vector<RichSpan> rich = ot::annot::parseRichContents(second.richContents);
    ASSERT_EQ(rich.size(), 2u);
    EXPECT_TRUE(rich[0].bold);
    EXPECT_EQ(rich[0].text, "@Анна ");
    EXPECT_FALSE(rich[1].bold);

    EXPECT_EQ(text->review(), ReviewState::Accepted);
    EXPECT_EQ(text->reviewAuthor(), "Анна");
    EXPECT_FALSE(text->marked());
    ASSERT_EQ(text->history.size(), 1u);
    EXPECT_EQ(text->history[0].model, "Review");
    EXPECT_EQ(text->history[0].state, "Accepted");

    ASSERT_EQ(sq->replies.size(), 1u);
    EXPECT_EQ(sq->replies[0].ref.nm, replyNms[2]);
    EXPECT_TRUE(sq->marked());
    EXPECT_EQ(sq->review(), ReviewState::None);

    // Ветка записки: она сама, два ответа и статус.
    const std::vector<AnnotRef> branch = tree.branch(text->ref);
    EXPECT_EQ(branch.size(), 4u);
    EXPECT_EQ(tree.rootOf(second.ref), text);
    EXPECT_EQ(tree.find(second.ref)->contents, "@Анна спасибо");
    EXPECT_EQ(tree.rootOf(text->history[0].ref), text);

    // Удаление ветки — всё вместе одним шагом.
    ot::annot::remove(*doc, branch);
    const CommentTree after = ot::annot::buildComments(*doc);
    EXPECT_EQ(after.roots.size(), 15u);
    EXPECT_EQ(rootByNm(after, "ottisk-01-text"), nullptr);
    EXPECT_EQ(after.count(), 16);
    doc->undoStack().undo();
    EXPECT_EQ(ot::annot::buildComments(*doc).count(), 19);
}

TEST(comments_model, orphans_groups_and_popups) {
    OT_FEATURE("comment-20");
    ot::test::TempDir dir;
    auto doc = Document::openOrThrow(blankFile(dir, "group.pdf", 1));
    // «Заменить текст»: Caret + StrikeOut одной группой — одна запись.
    AnnotProps caret = ot::annot::toolDefaults(AnnotType::Caret);
    caret.rect = {100, 100, 110, 112};
    caret.contents = "новый текст";
    AnnotProps strike = ot::annot::toolDefaults(AnnotType::StrikeOut);
    strike.quads = {{{60, 100}, {100, 100}, {60, 112}, {100, 112}}};
    const auto group = ot::annot::createGroup(*doc, 0, {caret, strike}, "Replace text");
    ASSERT_EQ(group.size(), 2u);
    const AnnotRef parent = ot::annot::create(*doc, 0, note(300, 300, "Анна", "родитель", 0));
    const AnnotRef reply = ot::annot::addReply(*doc, parent, {.author = "Борис", .contents = "сирота"});
    CommentTree tree = ot::annot::buildComments(*doc);
    ASSERT_EQ(tree.roots.size(), 2u);
    EXPECT_EQ(tree.roots[0].type, AnnotType::Caret);
    ASSERT_EQ(tree.roots[0].group.size(), 1u);
    EXPECT_EQ(tree.roots[0].group[0], group[1]);
    EXPECT_EQ(tree.branch(group[0]).size(), 2u);
    // Родитель удалён в обход ветки (чужой файл) — ответ становится корнем, а окно записки без родителя
    // в дерево не входит.
    ot::annot::remove(*doc, std::vector<AnnotRef>{parent});
    tree = ot::annot::buildComments(*doc);
    ASSERT_EQ(tree.roots.size(), 2u);
    EXPECT_EQ(tree.roots[1].ref, reply);
    EXPECT_EQ(tree.roots[1].depth, 0);
    EXPECT_EQ(tree.roots[1].contents, "сирота");
    for (const Comment& c : tree.roots) {
        EXPECT_NE(c.type, AnnotType::Popup);
    }
    // Статус без модели: Marked/Unmarked — модель Marked, прочее — Review.
    ot::annot::setStatus(*doc, group[0], ot::annot::kReviewModel, "Cancelled", "Вера");
    tree = ot::annot::buildComments(*doc);
    EXPECT_EQ(tree.roots[0].review(), ReviewState::Cancelled);
    ot::annot::setStatus(*doc, group[0], ot::annot::kReviewModel, "None", "Вера");
    tree = ot::annot::buildComments(*doc);
    EXPECT_EQ(tree.roots[0].review(), ReviewState::None);
    EXPECT_EQ(tree.roots[0].history.size(), 2u);
    EXPECT_THROW(ot::annot::setStatus(*doc, group[0], "Bogus", "X", "Вера"), std::exception);
    EXPECT_THROW(ot::annot::addReply(*doc, {0, 99999, {}}, {.contents = "нет"}), std::exception);
}

TEST(comments_model, sort_by_each_key) {
    OT_FEATURE("comment-19");
    Sample s;
    const CommentTree tree = ot::annot::buildComments(*s.doc);
    ASSERT_EQ(tree.roots.size(), 5u);
    const CommentFilter all;
    EXPECT_EQ(contentsOf(ot::annot::arrange(tree, CommentSort::Page, all)),
              (std::vector<std::string>{"Бета Проверка", "гамма", "эпсилон", "альфа", "дельта"}));
    EXPECT_EQ(contentsOf(ot::annot::arrange(tree, CommentSort::Author, all)),
              (std::vector<std::string>{"Бета Проверка", "дельта", "эпсилон", "альфа", "гамма"}));
    EXPECT_EQ(contentsOf(ot::annot::arrange(tree, CommentSort::Date, all)),
              (std::vector<std::string>{"Бета Проверка", "гамма", "альфа", "эпсилон", "дельта"}));
    EXPECT_EQ(contentsOf(ot::annot::arrange(tree, CommentSort::Date, all, true)),
              (std::vector<std::string>{"дельта", "эпсилон", "альфа", "гамма", "Бета Проверка"}));
    // Тип — по порядку подтипов (Text раньше Square), внутри — по странице.
    EXPECT_EQ(contentsOf(ot::annot::arrange(tree, CommentSort::Type, all)),
              (std::vector<std::string>{"гамма", "эпсилон", "альфа", "Бета Проверка", "дельта"}));
    // Статус — по порядку ReviewState: без статуса, принят, отклонён, отменён, завершён.
    EXPECT_EQ(contentsOf(ot::annot::arrange(tree, CommentSort::Status, all)),
              (std::vector<std::string>{"гамма", "альфа", "Бета Проверка", "дельта", "эпсилон"}));
}

TEST(comments_model, filter_changes_list_and_page_overlay) {
    OT_FEATURE("comment-19");
    Sample s;
    const CommentTree tree = ot::annot::buildComments(*s.doc);
    CommentFilter f;
    EXPECT_FALSE(f.active());
    EXPECT_TRUE(ot::annot::hiddenByFilter(tree, f).empty());

    f.authors = {"Анна"};
    EXPECT_TRUE(f.active());
    EXPECT_EQ(contentsOf(ot::annot::arrange(tree, CommentSort::Page, f)),
              (std::vector<std::string>{"Бета Проверка", "дельта"}));
    auto hidden = ot::annot::hiddenByFilter(tree, f);
    ASSERT_EQ(hidden.size(), 2u);
    EXPECT_EQ(hidden[0], (std::vector<int>{s.c.objnum, s.e.objnum}));
    EXPECT_EQ(hidden[1], (std::vector<int>{s.a.objnum}));

    f = {};
    f.types = 1u << static_cast<int>(AnnotType::Text);
    EXPECT_EQ(contentsOf(ot::annot::arrange(tree, CommentSort::Page, f)),
              (std::vector<std::string>{"гамма", "эпсилон", "альфа"}));
    hidden = ot::annot::hiddenByFilter(tree, f);
    EXPECT_EQ(hidden[0], (std::vector<int>{s.b.objnum}));
    EXPECT_EQ(hidden[1], (std::vector<int>{s.d.objnum}));

    f = {};
    f.states = {ReviewState::Accepted, ReviewState::Completed};
    EXPECT_EQ(contentsOf(ot::annot::arrange(tree, CommentSort::Page, f)),
              (std::vector<std::string>{"Бета Проверка", "эпсилон"}));
    f.states = {ReviewState::None};
    EXPECT_EQ(contentsOf(ot::annot::arrange(tree, CommentSort::Page, f)),
              (std::vector<std::string>{"гамма", "альфа"}));

    f = {};
    f.onlyMine = true;
    f.me = "Борис";
    EXPECT_EQ(contentsOf(ot::annot::arrange(tree, CommentSort::Page, f)),
              (std::vector<std::string>{"эпсилон", "альфа"}));

    f = {};
    f.colors = {0xFF0000};
    EXPECT_EQ(contentsOf(ot::annot::arrange(tree, CommentSort::Page, f)),
              (std::vector<std::string>{"Бета Проверка"}));

    f = {};
    f.markedOnly = true;
    EXPECT_EQ(contentsOf(ot::annot::arrange(tree, CommentSort::Page, f)),
              (std::vector<std::string>{"гамма"}));

    // Поиск по /Contents без учёта регистра (кириллица) — и по ответам.
    f = {};
    f.search = "ПРОВЕР";
    EXPECT_EQ(contentsOf(ot::annot::arrange(tree, CommentSort::Page, f)),
              (std::vector<std::string>{"Бета Проверка"}));
    ot::annot::addReply(*s.doc, s.d, {.author = "Вера", .contents = "Проверено"});
    const CommentTree withReply = ot::annot::buildComments(*s.doc);
    EXPECT_EQ(contentsOf(ot::annot::arrange(withReply, CommentSort::Page, f)),
              (std::vector<std::string>{"Бета Проверка", "дельта"}));

    // Непрочитанные — по предикату.
    f = {};
    f.unreadOnly = true;
    const auto unread = [](const Comment& c) { return c.author == "Вера"; };
    EXPECT_EQ(contentsOf(ot::annot::arrange(tree, CommentSort::Page, f, false, unread)),
              (std::vector<std::string>{"гамма"}));
    EXPECT_EQ(ot::annot::foldCase("ПРИВЁТ World Ё"), "привёт world ё");
}

TEST(comments_model, rich_contents_round_trip) {
    OT_FEATURE("comment-20");
    const std::vector<RichSpan> spans = {
        {.text = "Обычный "},
        {.text = "жирный", .bold = true},
        {.text = " и "},
        {.text = "курсив", .italic = true, .underline = true, .color = 0x1E64C8},
        {.text = "\nВторой абзац <&> \"кавычки\""},
    };
    const std::string xhtml = ot::annot::richContentsFrom(spans);
    EXPECT_NE(xhtml.find("xmlns=\"http://www.w3.org/1999/xhtml\""), std::string::npos);
    EXPECT_NE(xhtml.find("font-weight:bold"), std::string::npos);
    EXPECT_NE(xhtml.find("&lt;&amp;&gt;"), std::string::npos);
    const std::vector<RichSpan> back = ot::annot::parseRichContents(xhtml);
    EXPECT_EQ(back, spans);
    EXPECT_EQ(ot::annot::plainText(back), "Обычный жирный и курсив\nВторой абзац <&> \"кавычки\"");

    // Как пишет Acrobat: <p> по абзацам, <b>/<i>, сущности, цвет в style, лишние пробелы между тегами.
    const std::string acrobat =
        "<?xml version=\"1.0\"?><body xmlns=\"http://www.w3.org/1999/xhtml\" "
        "xmlns:xfa=\"http://www.xfa.org/schema/xfa-data/1.0/\" xfa:APIVersion=\"Acrobat:21.1.0\" "
        "xfa:spec=\"2.0.2\" style=\"font-size:10.0pt;text-align:left;color:#000000\">\n"
        "<p dir=\"ltr\"><span style=\"font-family:Helvetica\">Один <b>два</b></span></p>\n"
        "<p dir=\"ltr\"><span style=\"color:#FF0000\">три&#160;&#x441;</span></p></body>";
    const std::vector<RichSpan> parsed = ot::annot::parseRichContents(acrobat);
    EXPECT_EQ(ot::annot::plainText(parsed), "Один два\nтри\u00A0с");
    const auto bold = std::find_if(parsed.begin(), parsed.end(), [](const RichSpan& r) { return r.bold; });
    ASSERT_NE(bold, parsed.end());
    EXPECT_EQ(bold->text, "два");
    EXPECT_EQ(parsed.back().color, 0xFF0000u);
    // Не XHTML — текст как есть.
    EXPECT_EQ(ot::annot::plainText(ot::annot::parseRichContents("просто текст")), "просто текст");
}

TEST(comments_model, read_state_survives_restart) {
    OT_FEATURE("comment-18");
    ot::test::TempDir dir;
    const QString dbPath = QString::fromStdWString((dir.path() / L"данные" / L"state.sqlite").wstring());
    const QString key = ot::comments::ReadState::documentKey("abc", {});
    EXPECT_EQ(key, QStringLiteral("abc"));
    const QString byPath = ot::comments::ReadState::documentKey({}, QStringLiteral("C:/Док/A.pdf"));
    EXPECT_EQ(byPath, ot::comments::ReadState::documentKey({}, QStringLiteral("c:/док/a.pdf")));
    EXPECT_EQ(byPath.size(), 64);

    Comment mine;
    mine.ref = {0, 10, "nm-mine"};
    mine.author = "Анна";
    Comment theirs;
    theirs.ref = {0, 11, "nm-theirs"};
    theirs.author = "Борис";
    Comment noName;
    noName.ref = {1, 12, {}};
    noName.author = "Борис";
    EXPECT_EQ(ot::comments::ReadState::commentKey(noName.ref), QStringLiteral("#1:12"));
    {
        ot::StateDb db(dbPath);
        ASSERT_TRUE(db.isOpen()) << db.errorString().toStdString();
        EXPECT_GE(db.schemaVersion(), 7);
        ot::comments::ReadState state(db);
        state.setDocument(key);
        EXPECT_TRUE(state.isRead(mine, "Анна"));  // своё — прочитано
        EXPECT_FALSE(state.isRead(theirs, "Анна"));
        EXPECT_FALSE(state.stored(theirs.ref).has_value());
        state.setRead(theirs.ref, true);
        state.setRead(std::vector<AnnotRef>{mine.ref, noName.ref}, false);
        EXPECT_TRUE(state.isRead(theirs, "Анна"));
        EXPECT_FALSE(state.isRead(mine, "Анна"));
    }
    ot::StateDb db(dbPath);
    ASSERT_TRUE(db.isOpen());
    ot::comments::ReadState state(db);
    state.setDocument(key);
    EXPECT_TRUE(state.isRead(theirs, "Анна"));
    EXPECT_FALSE(state.isRead(mine, "Анна"));
    EXPECT_FALSE(state.isRead(noName, "Анна"));
    ASSERT_TRUE(state.stored(mine.ref).has_value());
    // Другой документ — свои отметки.
    state.setDocument(QStringLiteral("other"));
    EXPECT_FALSE(state.isRead(theirs, "Анна"));
}

TEST(comments_model, document_key_from_trailer_id) {
    ot::test::TempDir dir;
    auto doc = Document::openOrThrow(fixture("annots-all.pdf"));
    const std::string key = ot::annot::commentsDocumentKey(*doc);
    // Матрица этапа 0 записана pikepdf — /ID есть.
    EXPECT_EQ(key.size(), 64u);
    auto again = Document::openOrThrow(copyAll(dir));
    EXPECT_EQ(ot::annot::commentsDocumentKey(*again), key);
}
