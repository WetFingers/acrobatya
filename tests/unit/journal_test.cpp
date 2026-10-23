// unit.journal.* — журнал правок и история undo/redo (эталон 4.3): журнал MuPDF включён с открытия, правка —
// Journal::Operation (pdf_begin/end_operation, исключение → pdf_abandon_operation), вложенная операция
// присоединяется к внешней, UndoStack — шаги с именами для меню «Отменить: …», группировка одноимённых
// операций за 1 с (набор текста), страницы шага для сброса display lists, слушатель изменений.
// 10 правок → undo ×10 → объекты после полной записи равны исходнику; redo ×10 — снова правки.
#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/error.h"
#include "core/mu/handles.h"
#include "core/util/features.h"
#include "support/pdf_objects.h"
#include "support/test_edits.h"
#include "support/test_pdf.h"

extern "C" {
#include <mupdf/pdf.h>
}

using namespace std::chrono_literals;
using ot::Document;
using ot::Journal;
using ot::MuError;
using ot::TouchedPages;
using ot::UndoStack;

namespace {

std::unique_ptr<Document> openPdf(int pages = 3) {
    return Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), pages));
}

using ot::test::ctxOf;
using ot::test::pdfOf;
using ot::test::putRootName;
using ot::test::rootName;
using ot::test::setRotate;
using ot::test::tenEdits;
using ot::test::undoSteps;

}  // namespace

TEST(journal, enabled_on_open_edits_outside_operation_are_rejected) {
    auto doc = openPdf();
    ASSERT_TRUE(doc->journaled());
    // Журнал MuPDF включён с открытия: правка без операции — ошибка аргумента, документ не меняется.
    try {
        setRotate(*doc, 0, 90);
        FAIL() << "edit outside an operation must throw";
    } catch (const MuError& e) {
        EXPECT_EQ(e.kind(), ot::ErrorKind::Argument) << e.what();
    }
    EXPECT_EQ(doc->pageRotation(0), 0);
    EXPECT_FALSE(doc->undoStack().canUndo());
}

TEST(journal, operation_with_exception_rolls_back_and_journal_is_empty) {
    OT_FEATURE("edit-13");
    auto doc = openPdf();
    const ot::test::PdfObjects before = ot::test::pdfObjects(*doc);
    try {
        Journal::Operation op(*doc, "Broken edit");
        putRootName(*doc, "PageMode", "UseThumbs");
        setRotate(*doc, 1, 180);
        throw std::runtime_error("edit failed");
    } catch (const std::runtime_error&) {  // NOLINT(bugprone-empty-catch): проверяем откат ниже
    }
    EXPECT_EQ(rootName(*doc, "PageMode"), "");
    EXPECT_EQ(doc->pageRotation(1), 0);
    EXPECT_FALSE(doc->undoStack().canUndo());
    EXPECT_FALSE(doc->undoStack().canRedo());
    EXPECT_EQ(undoSteps(*doc), 0);
    EXPECT_EQ(ot::test::firstDifference(before, ot::test::pdfObjects(*doc)), "");
    // Журнал не застрял в операции: следующая правка идёт обычным шагом.
    {
        Journal::Operation op(*doc, "Good edit");
        setRotate(*doc, 1, 90);
        op.commit();
    }
    EXPECT_EQ(doc->pageRotation(1), 90);
    EXPECT_EQ(doc->undoStack().undoName(), "Good edit");
}

TEST(journal, ten_edits_undo_ten_times_restores_objects_after_full_write) {
    OT_FEATURE("edit-13");
    const std::string bytes = ot::test::makePdf(ot::MuContext::global(), 3);
    auto doc = Document::openOrThrowMemory(bytes);
    UndoStack& undo = doc->undoStack();
    undo.setGroupWindow(0ms);
    const ot::test::PdfObjects original = ot::test::pdfObjects(*doc);
    tenEdits(*doc);
    EXPECT_EQ(undo.size(), 10);
    EXPECT_EQ(undo.position(), 10);
    EXPECT_EQ(doc->pageCount(), 3);  // +1 и −1 страница
    EXPECT_EQ(rootName(*doc, "PageLayout"), "TwoColumnLeft");
    const ot::test::PdfObjects edited = ot::test::pdfObjects(*doc);
    EXPECT_NE(ot::test::firstDifference(original, edited), "");
    auto undoAll = [&] {
        for (int i = 0; i < 10; ++i) {
            ASSERT_TRUE(undo.canUndo()) << i;
            (void)undo.undo();
        }
        EXPECT_FALSE(undo.canUndo());
    };
    undoAll();
    EXPECT_EQ(undo.position(), 0);
    EXPECT_EQ(doc->pageCount(), 3);
    EXPECT_EQ(doc->pageRotation(0), 0);
    EXPECT_EQ(ot::test::firstDifference(original, ot::test::pdfObjects(*doc)), "");
    for (int i = 0; i < 10; ++i) {
        ASSERT_TRUE(undo.canRedo()) << i;
        (void)undo.redo();
    }
    EXPECT_FALSE(undo.canRedo());
    EXPECT_EQ(ot::test::firstDifference(edited, ot::test::pdfObjects(*doc)), "");
    undoAll();
    // Байты после полной записи — те же объекты, что у полной записи исходника (как qpdf --json).
    auto source = Document::openOrThrowMemory(bytes);
    EXPECT_EQ(ot::test::firstDifference(ot::test::pdfObjectsAfterFullWrite(*source),
                                        ot::test::pdfObjectsAfterFullWrite(*doc)),
              "");
    // Запись меняет xref документа — прежний журнал к нему не применим, история сброшена.
    EXPECT_FALSE(undo.canRedo());
    EXPECT_EQ(undo.size(), 0);
}

TEST(journal, undo_redo_names_and_new_edit_drops_redo) {
    OT_FEATURE("comment-08");
    auto doc = openPdf();
    UndoStack& undo = doc->undoStack();
    undo.setGroupWindow(0ms);
    EXPECT_FALSE(undo.canUndo());
    EXPECT_EQ(undo.undoName(), "");
    {
        Journal::Operation op(*doc, "Add note");
        setRotate(*doc, 0, 90);
        op.commit();
    }
    {
        Journal::Operation op(*doc, "Fill field");
        setRotate(*doc, 1, 90);
        op.commit();
    }
    EXPECT_TRUE(undo.canUndo());
    EXPECT_FALSE(undo.canRedo());
    EXPECT_EQ(undo.undoName(), "Fill field");
    (void)undo.undo();
    EXPECT_EQ(undo.undoName(), "Add note");
    EXPECT_EQ(undo.redoName(), "Fill field");
    EXPECT_TRUE(undo.canRedo());
    EXPECT_EQ(doc->pageRotation(1), 0);
    // Новая правка после отмены отбрасывает «Вернуть».
    {
        Journal::Operation op(*doc, "Rotate");
        setRotate(*doc, 2, 270);
        op.commit();
    }
    EXPECT_FALSE(undo.canRedo());
    EXPECT_EQ(undo.redoName(), "");
    EXPECT_EQ(undo.undoName(), "Rotate");
    EXPECT_EQ(undo.size(), 2);
    EXPECT_THROW((void)undo.redo(), MuError);
}

TEST(journal, same_name_within_one_second_groups_into_one_step) {
    OT_FEATURE("forms-01");
    auto doc = openPdf();
    UndoStack& undo = doc->undoStack();
    auto now = UndoStack::Clock::time_point{} + 100s;
    undo.setClock([&] { return now; });
    auto type = [&](int page, int rotate, const char* name = "Typing") {
        Journal::Operation op(*doc, name);
        setRotate(*doc, page, rotate);
        op.invalidate(page);
        op.commit();
    };
    type(0, 90);
    now += 500ms;
    type(1, 90);
    now += 900ms;  // 0,9 с от предыдущей — та же группа (окно считается от последней операции группы)
    type(2, 90);
    now += 1500ms;  // пауза больше секунды — новый шаг
    type(0, 180);
    now += 200ms;
    type(1, 180, "Other");  // другое имя — новый шаг
    EXPECT_EQ(undo.size(), 3);
    EXPECT_EQ(undoSteps(*doc), 5);  // в MuPDF шаги остаются отдельными
    EXPECT_EQ(undo.undoName(), "Other");
    (void)undo.undo();
    EXPECT_EQ(doc->pageRotation(1), 90);
    EXPECT_EQ(undo.undoName(), "Typing");
    const TouchedPages second = undo.undo();
    EXPECT_EQ(second.pages, (std::vector<int>{0}));
    EXPECT_EQ(doc->pageRotation(0), 90);
    const TouchedPages first = undo.undo();  // три операции разом
    EXPECT_EQ(first.pages, (std::vector<int>{0, 1, 2}));
    EXPECT_FALSE(first.all);
    EXPECT_EQ(doc->pageRotation(0), 0);
    EXPECT_EQ(doc->pageRotation(1), 0);
    EXPECT_EQ(doc->pageRotation(2), 0);
    EXPECT_FALSE(undo.canUndo());
    // Вернуть — тоже группой; после undo/redo группа закрыта: новая «Typing» через 0,1 с — свой шаг, а
    // «Вернуть» отброшено.
    (void)undo.redo();
    EXPECT_EQ(doc->pageRotation(2), 90);
    EXPECT_EQ(doc->pageRotation(0), 90);
    now += 100ms;
    type(2, 0);
    EXPECT_EQ(undo.size(), 2);
    EXPECT_FALSE(undo.canRedo());
    (void)undo.undo();
    EXPECT_EQ(doc->pageRotation(2), 90);
}

TEST(journal, nested_operation_joins_outer_and_mupdf_own_operations_too) {
    auto doc = openPdf();
    UndoStack& undo = doc->undoStack();
    undo.setGroupWindow(0ms);
    fz_context* ctx = ctxOf(*doc);
    pdf_document* pdf = pdfOf(*doc);
    {
        Journal::Operation outer(*doc, "Outer");
        EXPECT_FALSE(outer.nested());
        setRotate(*doc, 0, 90);
        {
            Journal::Operation inner(*doc, "Inner");
            EXPECT_TRUE(inner.nested());
            setRotate(*doc, 1, 90);
            inner.invalidate(1);
            inner.commit();
        }
        // pdf_add_image сам открывает операцию «Add image» — внутри нашей она вложенная.
        ot::mu::PixmapHandle pix(ctx);
        ot::mu::ImageHandle img(ctx);
        ot::mu::PdfObjHandle ref(ctx);
        ot::mu::call(ctx, [&] {
            pix.reset(fz_new_pixmap(ctx, fz_device_gray(ctx), 4, 4, nullptr, 0));
            fz_clear_pixmap_with_value(ctx, pix, 0x40);
            img.reset(fz_new_image_from_pixmap(ctx, pix, nullptr));
            ref.reset(pdf_add_image(ctx, pdf, img));
        });
        outer.invalidate(0);
        outer.commit();
    }
    EXPECT_EQ(undo.size(), 1);
    EXPECT_EQ(undoSteps(*doc), 1);
    EXPECT_EQ(undo.undoName(), "Outer");
    const TouchedPages touched = undo.undo();
    EXPECT_EQ(touched.pages, (std::vector<int>{0, 1}));
    EXPECT_EQ(doc->pageRotation(0), 0);
    EXPECT_EQ(doc->pageRotation(1), 0);
}

TEST(journal, failed_nested_operation_abandons_outer_on_commit) {
    auto doc = openPdf();
    {
        Journal::Operation outer(*doc, "Outer");
        setRotate(*doc, 0, 90);
        try {
            Journal::Operation inner(*doc, "Inner");
            setRotate(*doc, 1, 90);
            throw std::runtime_error("inner failed");
        } catch (const std::runtime_error&) {  // NOLINT(bugprone-empty-catch): вызывающий проглотил ошибку
        }
        // Вложенную нельзя откатить отдельно — внешняя не фиксирует полуправку.
        EXPECT_THROW(outer.commit(), MuError);
    }
    EXPECT_EQ(doc->pageRotation(0), 0);
    EXPECT_EQ(doc->pageRotation(1), 0);
    EXPECT_FALSE(doc->undoStack().canUndo());
}

TEST(journal, operation_without_changes_adds_no_step) {
    auto doc = openPdf();
    {
        Journal::Operation op(*doc, "Nothing");
        op.invalidate(0);
        op.commit();
    }
    EXPECT_FALSE(doc->undoStack().canUndo());
    EXPECT_EQ(doc->undoStack().size(), 0);
}

TEST(journal, touched_pages_and_listener) {
    auto doc = openPdf(4);
    UndoStack& undo = doc->undoStack();
    undo.setGroupWindow(0ms);
    int changes = 0;
    undo.setListener([&] { ++changes; });
    {
        Journal::Operation op(*doc, "Two pages");
        setRotate(*doc, 3, 90);
        setRotate(*doc, 1, 90);
        op.invalidate(3);
        op.invalidate(1);
        op.invalidate(3);
        op.commit();
    }
    EXPECT_EQ(changes, 1);
    {
        Journal::Operation op(*doc, "Structure");
        setRotate(*doc, 0, 90);
        op.invalidate(0);
        op.invalidateAll();
        op.commit();
    }
    EXPECT_EQ(changes, 2);
    const TouchedPages structural = undo.undo();
    EXPECT_TRUE(structural.all);
    EXPECT_EQ(changes, 3);
    const TouchedPages two = undo.undo();
    EXPECT_FALSE(two.all);
    EXPECT_EQ(two.pages, (std::vector<int>{1, 3}));
    EXPECT_EQ(changes, 4);
    const TouchedPages again = undo.redo();
    EXPECT_EQ(again.pages, (std::vector<int>{1, 3}));
    EXPECT_EQ(changes, 5);
    // Без изменений — без уведомления.
    {
        Journal::Operation op(*doc, "Nothing");
        op.commit();
    }
    EXPECT_EQ(changes, 5);
}

TEST(journal, mupdf_operation_outside_ours_shows_up_in_history) {
    auto doc = openPdf();
    fz_context* ctx = ctxOf(*doc);
    // Операция, открытая самим MuPDF (fz_set_metadata → pdf_set_metadata — «Set Metadata»), без нашей
    // обёртки.
    ot::mu::call(ctx, [&] { fz_set_metadata(ctx, doc->handle(), FZ_META_INFO_TITLE, "Заголовок"); });
    UndoStack& undo = doc->undoStack();
    ASSERT_TRUE(undo.canUndo());
    EXPECT_EQ(undo.undoName(), "Set Metadata");
    const TouchedPages touched = undo.undo();
    EXPECT_TRUE(touched.all);  // страницы неизвестны — сбросить все
    EXPECT_EQ(doc->properties().title, "");
}

TEST(journal, blank_document_is_journaled) {
    auto doc = Document::createBlank(2);
    ASSERT_TRUE(doc->journaled());
    {
        Journal::Operation op(*doc, "Rotate");
        setRotate(*doc, 1, 90);
        op.commit();
    }
    EXPECT_TRUE(doc->undoStack().canUndo());
    (void)doc->undoStack().undo();
    EXPECT_EQ(doc->pageRotation(1), 0);
}
