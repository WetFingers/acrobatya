// unit.annots_min.* — минимальное ядро аннотаций (план 2.2, core/annot/annots.h): список по /Annots с 28
// подтипами матрицы tests/fixtures/annots/annots-all.pdf (рамки — /Rect в координатах страницы MuPDF, с
// /Rotate), хит-тест (верхняя, без Popup и скрытых), setRect и remove как операции журнала (undo возвращает
// /Rect и /Annots), те же правки через DocumentSession (services/annot_ops.h: шаг истории, pageChanged).
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <QObject>
#include <QString>

#include <algorithm>
#include <fstream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/annot/annots.h"
#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/error.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "services/annot_ops.h"
#include "services/document_session.h"
#include "support/qt_app.h"
#include "support/test_edits.h"
#include "support/test_pdf.h"

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

namespace fs = std::filesystem;
using ot::Document;
using ot::Journal;
using ot::MuError;
using ot::PointF;
using ot::RectF;
using ot::annot::AnnotInfo;
using ot::annot::AnnotRef;
using ot::annot::AnnotType;

namespace {

fs::path fixture(const char* name) { return ot::test::sourceDir() / "tests" / "fixtures" / "annots" / name; }

nlohmann::json expected() {
    std::ifstream in(fixture("annots.json"), std::ios::binary);
    return nlohmann::json::parse(in);
}

std::unique_ptr<Document> openAll() { return Document::openOrThrow(fixture("annots-all.pdf")); }

// /Rect объекта как в файле (пункты PDF, начало — левый нижний угол).
RectF rawRect(Document& doc, int objnum) {
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = doc.pdf();
    fz_rect r{};
    ot::mu::call(ctx, [&] {
        pdf_obj* obj = pdf_load_object(ctx, pdf, objnum);
        r = pdf_dict_get_rect(ctx, obj, PDF_NAME(Rect));
        pdf_drop_obj(ctx, obj);
    });
    return {r.x0, r.y0, r.x1, r.y1};
}

int annotsLength(Document& doc, int page) {
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = doc.pdf();
    int n = 0;
    ot::mu::call(ctx, [&] {
        pdf_obj* obj = pdf_lookup_page_obj(ctx, pdf, page);
        n = pdf_array_len(ctx, pdf_dict_get(ctx, obj, PDF_NAME(Annots)));
    });
    return n;
}

void expectRectNear(const RectF& a, const RectF& b, float eps = 0.02f) {
    EXPECT_NEAR(a.x0, b.x0, eps);
    EXPECT_NEAR(a.y0, b.y0, eps);
    EXPECT_NEAR(a.x1, b.x1, eps);
    EXPECT_NEAR(a.y1, b.y1, eps);
}

const AnnotInfo& bySubtype(const std::vector<AnnotInfo>& list, AnnotType type) {
    const auto it =
        std::find_if(list.begin(), list.end(), [&](const AnnotInfo& a) { return a.type == type; });
    if (it == list.end()) {
        throw std::runtime_error("no annotation of this type");
    }
    return *it;
}

PointF center(const RectF& r) { return {(r.x0 + r.x1) / 2, (r.y0 + r.y1) / 2}; }

}  // namespace

TEST(annots_min, type_names_round_trip) {
    std::set<std::string> names;
    for (int i = 0; i < ot::annot::kAnnotTypeCount; ++i) {
        const auto type = static_cast<AnnotType>(i);
        const std::string name(ot::annot::annotTypeName(type));
        ASSERT_FALSE(name.empty()) << i;
        EXPECT_EQ(ot::annot::annotTypeFromName(name), type) << name;
        names.insert(name);
    }
    EXPECT_EQ(names.size(), 28u);
    EXPECT_EQ(ot::annot::annotTypeName(AnnotType::ThreeD), "3D");
    EXPECT_EQ(ot::annot::annotTypeFromName("Nonsense"), AnnotType::Unknown);
    EXPECT_EQ(ot::annot::annotTypeName(AnnotType::Unknown), "");
}

// 28 записей по порядку /Annots, у каждой свой подтип, номер объекта и рамка = /Rect, перевёрнутый по y.
TEST(annots_min, list_annots_all_gives_28_subtypes_with_rects) {
    OT_FEATURE("comment-08");
    const nlohmann::json want = expected();
    auto doc = openAll();
    const float height = doc->pageBox(0).height();
    const std::vector<AnnotInfo> list = ot::annot::listAnnots(*doc, 0);
    ASSERT_EQ(list.size(), 28u);

    std::set<int> objnums;
    std::set<std::string> subtypes;
    for (std::size_t i = 0; i < list.size(); ++i) {
        const AnnotInfo& a = list[i];
        EXPECT_EQ(a.ref.page, 0);
        EXPECT_TRUE(a.ref.valid());
        objnums.insert(a.ref.objnum);
        EXPECT_EQ(a.subtype, want["order_in_annots"][i].get<std::string>()) << i;
        EXPECT_NE(a.type, AnnotType::Unknown) << a.subtype;
        EXPECT_EQ(ot::annot::annotTypeName(a.type), a.subtype);
        subtypes.insert(a.subtype);
    }
    EXPECT_EQ(objnums.size(), 28u);
    EXPECT_EQ(subtypes.size(), 28u);

    // Рамки — /Rect из генератора в координатах MuPDF (y вниз от верха страницы).
    for (const auto& item : want["annots"]) {
        const std::string subtype = item["subtype"].get<std::string>();
        const auto& r = item["rect"];
        const RectF pdfRect{r[0].get<float>(), r[1].get<float>(), r[2].get<float>(), r[3].get<float>()};
        const AnnotInfo& a = bySubtype(list, ot::annot::annotTypeFromName(subtype));
        SCOPED_TRACE(subtype);
        const RectF flipped{pdfRect.x0, height - pdfRect.y1, pdfRect.x1, height - pdfRect.y0};
        if ((a.flags & ot::annot::kAnnotNoZoom) != 0) {
            // NoZoom (записка, вложение, звук): на экране — /BBox внешнего вида от левого верхнего угла
            // /Rect.
            EXPECT_NEAR(a.rect.x0, flipped.x0, 0.02f);
            EXPECT_NEAR(a.rect.y0, flipped.y0, 0.02f);
            EXPECT_LE(a.rect.width(), flipped.width() + 0.02f);
            EXPECT_LE(a.rect.height(), flipped.height() + 0.02f);
        } else {
            expectRectNear(a.rect, flipped);
        }
    }
}

TEST(annots_min, find_annot_by_ref) {
    auto doc = openAll();
    const auto list = ot::annot::listAnnots(*doc, 0);
    const AnnotInfo& square = bySubtype(list, AnnotType::Square);
    const auto found = ot::annot::findAnnot(*doc, square.ref);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->type, AnnotType::Square);
    EXPECT_FALSE(ot::annot::findAnnot(*doc, {0, 99999}).has_value());
    EXPECT_FALSE(ot::annot::findAnnot(*doc, {0, 0}).has_value());
}

// Верхняя под точкой; Popup не попадает (MuPDF его не рисует); мимо всех — ничего.
TEST(annots_min, annot_at_hits_topmost_visible) {
    auto doc = openAll();
    const auto list = ot::annot::listAnnots(*doc, 0);
    for (const AnnotType type : {AnnotType::Square, AnnotType::Link, AnnotType::Widget, AnnotType::Stamp}) {
        const AnnotInfo& a = bySubtype(list, type);
        const auto hit = ot::annot::annotAt(*doc, 0, center(a.rect));
        ASSERT_TRUE(hit.has_value()) << a.subtype;
        EXPECT_EQ(hit->ref, a.ref) << a.subtype;
    }
    const AnnotInfo& popup = bySubtype(list, AnnotType::Popup);
    const auto underPopup = ot::annot::annotAt(*doc, 0, center(popup.rect));
    EXPECT_TRUE(!underPopup.has_value() || underPopup->type != AnnotType::Popup);

    // Между ячейками матрицы (поле слева) — ничего, с допуском — ближайшая.
    const AnnotInfo& square = bySubtype(list, AnnotType::Square);
    const PointF left{square.rect.x0 - 3, center(square.rect).y};
    EXPECT_FALSE(ot::annot::annotAt(*doc, 0, left).has_value());
    const auto near = ot::annot::annotAt(*doc, 0, left, 4);
    ASSERT_TRUE(near.has_value());
    EXPECT_EQ(near->ref, square.ref);
}

// /Rotate 90: рамка поворачивается вместе со страницей (x ↔ y у страницы с началом MediaBox в нуле), хит-тест
// по повёрнутой рамке.
TEST(annots_min, rotate_90_maps_rects_and_hit_test) {
    auto doc = openAll();
    const auto before = ot::annot::listAnnots(*doc, 0);
    const AnnotInfo& square = bySubtype(before, AnnotType::Square);
    const RectF raw = rawRect(*doc, square.ref.objnum);
    {
        Journal::Operation op(*doc, "Rotate");
        ot::test::setRotate(*doc, 0, 90);
        op.invalidate(0);
        op.commit();
    }
    const auto after = ot::annot::listAnnots(*doc, 0);
    const AnnotInfo& rotated = bySubtype(after, AnnotType::Square);
    expectRectNear(rotated.rect, {raw.y0, raw.x0, raw.y1, raw.x1});
    const auto hit = ot::annot::annotAt(*doc, 0, center(rotated.rect));
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->ref, square.ref);
    // В прежнем (не повёрнутом) месте квадрата его уже нет.
    const auto old = ot::annot::annotAt(*doc, 0, center(square.rect));
    EXPECT_TRUE(!old.has_value() || old->ref != square.ref);
}

// Не кратный 90 /Rotate MuPDF округляет так же, как раскладка: 100 → 90.
TEST(annots_min, rotate_not_multiple_of_90_is_rounded) {
    auto doc = openAll();
    const AnnotInfo square = bySubtype(ot::annot::listAnnots(*doc, 0), AnnotType::Square);
    const RectF raw = rawRect(*doc, square.ref.objnum);
    {
        Journal::Operation op(*doc, "Rotate");
        ot::test::setRotate(*doc, 0, 100);
        op.commit();
    }
    EXPECT_EQ(doc->pageRotation(0), 90);
    const AnnotInfo rotated = bySubtype(ot::annot::listAnnots(*doc, 0), AnnotType::Square);
    expectRectNear(rotated.rect, {raw.y0, raw.x0, raw.y1, raw.x1});
}

// setRect → /Rect новый; undo → прежний байт в байт (по значению), redo → снова новый.
TEST(annots_min, set_rect_then_undo_restores_rect) {
    auto doc = openAll();
    const auto list = ot::annot::listAnnots(*doc, 0);
    for (const AnnotType type : {AnnotType::Square, AnnotType::Link, AnnotType::Popup, AnnotType::Stamp}) {
        const AnnotInfo a = bySubtype(list, type);
        SCOPED_TRACE(a.subtype);
        const RectF raw = rawRect(*doc, a.ref.objnum);
        const RectF moved{a.rect.x0 + 10, a.rect.y0 + 20, a.rect.x1 + 10, a.rect.y1 + 20};
        ot::annot::setRect(*doc, a.ref, moved);
        expectRectNear(ot::annot::findAnnot(*doc, a.ref)->rect, moved);
        // y вниз на 20 — в PDF на 20 вниз от верха, то есть /Rect y меньше на 20.
        expectRectNear(rawRect(*doc, a.ref.objnum), {raw.x0 + 10, raw.y0 - 20, raw.x1 + 10, raw.y1 - 20});
        EXPECT_EQ(doc->undoStack().undoName(), "Set annotation rectangle");
        const ot::TouchedPages touched = doc->undoStack().undo();
        EXPECT_TRUE(touched.all ||
                    std::find(touched.pages.begin(), touched.pages.end(), 0) != touched.pages.end());
        const RectF back = rawRect(*doc, a.ref.objnum);
        EXPECT_EQ(back.x0, raw.x0);
        EXPECT_EQ(back.y0, raw.y0);
        EXPECT_EQ(back.x1, raw.x1);
        EXPECT_EQ(back.y1, raw.y1);
        expectRectNear(ot::annot::findAnnot(*doc, a.ref)->rect, a.rect);
        doc->undoStack().redo();
        expectRectNear(rawRect(*doc, a.ref.objnum), {raw.x0 + 10, raw.y0 - 20, raw.x1 + 10, raw.y1 - 20});
        doc->undoStack().undo();
    }
}

TEST(annots_min, set_rect_on_rotated_page_uses_page_space) {
    auto doc = openAll();
    {
        Journal::Operation op(*doc, "Rotate");
        ot::test::setRotate(*doc, 0, 90);
        op.commit();
    }
    const AnnotInfo square = bySubtype(ot::annot::listAnnots(*doc, 0), AnnotType::Square);
    const RectF raw = rawRect(*doc, square.ref.objnum);
    // Вправо на 15 в повёрнутом виде — это +15 по y PDF (x вида = y PDF).
    ot::annot::setRect(*doc, square.ref,
                       {square.rect.x0 + 15, square.rect.y0, square.rect.x1 + 15, square.rect.y1});
    expectRectNear(rawRect(*doc, square.ref.objnum), {raw.x0, raw.y0 + 15, raw.x1, raw.y1 + 15});
}

TEST(annots_min, set_rect_errors) {
    auto doc = openAll();
    const AnnotInfo square = bySubtype(ot::annot::listAnnots(*doc, 0), AnnotType::Square);
    EXPECT_THROW(ot::annot::setRect(*doc, {0, 99999}, {0, 0, 10, 10}), MuError);
    EXPECT_THROW(ot::annot::setRect(*doc, square.ref, {10, 10, 10, 20}), MuError);  // пустая
    EXPECT_FALSE(doc->undoStack().canUndo());
    expectRectNear(ot::annot::findAnnot(*doc, square.ref)->rect, square.rect);
}

// remove: обычная, Link, Popup, Widget; Text — вместе со своим Popup; undo возвращает /Annots целиком.
TEST(annots_min, remove_then_undo_restores_annots) {
    auto doc = openAll();
    const auto list = ot::annot::listAnnots(*doc, 0);
    for (const AnnotType type : {AnnotType::Square, AnnotType::Link, AnnotType::Popup, AnnotType::Widget}) {
        const AnnotInfo a = bySubtype(list, type);
        SCOPED_TRACE(a.subtype);
        ot::annot::remove(*doc, a.ref);
        const auto left = ot::annot::listAnnots(*doc, 0);
        EXPECT_EQ(left.size(), 27u);
        EXPECT_FALSE(ot::annot::findAnnot(*doc, a.ref).has_value());
        EXPECT_EQ(doc->undoStack().undoName(), "Delete annotation");
        doc->undoStack().undo();
        EXPECT_EQ(ot::annot::listAnnots(*doc, 0).size(), 28u);
        EXPECT_TRUE(ot::annot::findAnnot(*doc, a.ref).has_value());
    }
    // Записка уходит вместе со своим окном (pdf_delete_annot).
    ot::annot::remove(*doc, bySubtype(list, AnnotType::Text).ref);
    EXPECT_EQ(annotsLength(*doc, 0), 26);
    doc->undoStack().undo();
    EXPECT_EQ(annotsLength(*doc, 0), 28);
}

TEST(annots_min, remove_several_is_one_step_and_atomic) {
    auto doc = openAll();
    const auto list = ot::annot::listAnnots(*doc, 0);
    const AnnotRef square = bySubtype(list, AnnotType::Square).ref;
    const AnnotRef circle = bySubtype(list, AnnotType::Circle).ref;
    ot::annot::remove(*doc, std::vector<AnnotRef>{square, circle});
    EXPECT_EQ(ot::annot::listAnnots(*doc, 0).size(), 26u);
    EXPECT_EQ(doc->undoStack().size(), 1);
    doc->undoStack().undo();
    EXPECT_EQ(ot::annot::listAnnots(*doc, 0).size(), 28u);

    // Одной нет — ничего не удалено, шага нет.
    const int steps = doc->undoStack().size();
    EXPECT_THROW(ot::annot::remove(*doc, std::vector<AnnotRef>{square, {0, 99999}}), MuError);
    EXPECT_EQ(ot::annot::listAnnots(*doc, 0).size(), 28u);
    EXPECT_EQ(doc->undoStack().position(), 0);
    EXPECT_LE(doc->undoStack().size(), steps);
}

TEST(annots_min, page_without_annots_and_bad_page) {
    auto doc = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 2));
    EXPECT_TRUE(ot::annot::listAnnots(*doc, 1).empty());
    EXPECT_FALSE(ot::annot::annotAt(*doc, 1, {100, 100}).has_value());
    EXPECT_THROW(ot::annot::listAnnots(*doc, 5), MuError);
}

TEST(annots_min, new_note_is_listed) {
    auto doc = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 1));
    ot::test::addNote(*doc, 0, "hello");
    // MuPDF создаёт записку вместе с окном (Popup) — в /Annots обе.
    const auto list = ot::annot::listAnnots(*doc, 0);
    ASSERT_GE(list.size(), 1u);
    const AnnotInfo& note = bySubtype(list, AnnotType::Text);
    const auto hit = ot::annot::annotAt(*doc, 0, center(note.rect));
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->ref, note.ref);
}

// Через DocumentSession: список задачей, setRect и remove — шаги истории сессии, pageChanged(0), undo сессии.
TEST(annots_min, session_edits_are_undoable_steps) {
    ot::test::qtApp();
    ot::DocumentSession session;
    ASSERT_TRUE(session.open(QString::fromStdWString(fixture("annots-all.pdf").wstring())));
    const std::vector<AnnotInfo> list = ot::annot::list(session, 0).result();
    ASSERT_EQ(list.size(), 28u);
    const AnnotInfo square = bySubtype(list, AnnotType::Square);

    std::vector<int> pages;
    QObject::connect(&session, &ot::DocumentSession::pageChanged, &session,
                     [&](int p) { pages.push_back(p); });
    const quint64 revision = session.revision();
    const RectF moved{square.rect.x0 + 5, square.rect.y0 + 5, square.rect.x1 + 5, square.rect.y1 + 5};
    ot::annot::setRect(session, square.ref, moved).waitForFinished();
    EXPECT_GT(session.revision(), revision);
    EXPECT_EQ(session.undoState().undoName, QString::fromLatin1(ot::annot::kSetRectStep));
    ASSERT_TRUE(ot::test::waitUntil([&] { return !pages.empty(); }));
    EXPECT_EQ(pages.front(), 0);

    const auto hit = ot::annot::at(session, 0, center(moved)).result();
    ASSERT_TRUE(hit.has_value());
    EXPECT_EQ(hit->ref, square.ref);

    EXPECT_TRUE(session.undo().result());
    const auto restored = ot::annot::list(session, 0).result();
    expectRectNear(bySubtype(restored, AnnotType::Square).rect, square.rect);

    ot::annot::remove(session, {square.ref}).waitForFinished();
    EXPECT_EQ(ot::annot::list(session, 0).result().size(), 27u);
    EXPECT_EQ(session.undoState().undoName, QString::fromLatin1(ot::annot::kDeleteStep));
    EXPECT_TRUE(session.undo().result());
    EXPECT_EQ(ot::annot::list(session, 0).result().size(), 28u);

    // Ошибка правки — исключение в QFuture, история не меняется.
    auto bad = ot::annot::setRect(session, {0, 99999}, moved);
    EXPECT_THROW(bad.waitForFinished(), MuError);
    session.close();
}
