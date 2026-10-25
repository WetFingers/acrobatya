// ui.fillsign_text — «Заполнить и подписать»: текст, гребёнка и отметки на плоской анкете (план 5.3, п. 2;
// эталон 3.5, 6.2 п. 4; forms-07, comment-09): инструмент в ToolRegistry с панелью (текст, ✓ ✕ ● — ▭,
// «Подпись ▾», «Инициалы ▾»); щелчок над подчёркиванием открывает набор, и поле FreeText без рамки встаёт
// базовой линией на линию с кеглем ≤ высоты клетки; пунктир клетки под курсором; гребёнка 8 клеток — 8 знаков
// с шагом клетки одним шагом истории; ✓ во флажок — по его центру глифом Segoe UI Symbol; поставленное
// выбрано, тянется и удаляется Del; щелчок «Текстом» по своему полю — его правка; только чтение — кнопки
// серые.
#include <QDir>
#include <QLabel>
#include <QSettings>
#include <QtTest/QtTest>

#include <algorithm>
#include <cmath>

#include "core/annot/annots.h"
#include "core/annot/fillsign.h"
#include "core/util/features.h"
#include "services/annot_ops.h"
#include "services/app_info.h"
#include "services/document_session.h"
#include "support/ui_test.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/tools/comment/inline_text_editor.h"
#include "ui/tools/fillsign/comb_editor.h"
#include "ui/tools/fillsign/fillsign_mode.h"
#include "ui/tools/fillsign/fillsign_tool.h"
#include "ui/tools/tool_registry.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/widgets/tool_button.h"

using ot::annot::AnnotInfo;
using ot::annot::AnnotProps;
using ot::annot::AnnotType;
using ot::ui::CombEditor;
using ot::ui::DocumentView;
using ot::ui::FillSignAction;
using ot::ui::FillSignMode;
using ot::ui::FillSignTool;
using ot::ui::InlineTextEditor;
using ot::ui::MainWindow;
using ot::ui::PageView;
using ot::ui::ToolRegistry;
using ot::ui::ViewToolId;

namespace {

constexpr double kPageHeight = 841.8898;
double top(double pdfY) { return kPageHeight - pdfY; }  // y генератора → y страницы MuPDF

const double kSurnameBase = top(718);  // подчёркивание «Фамилия», x 160–520
const double kDateBase = top(606);     // гребёнка «Дата рождения»: 8 клеток по 18 от x 180

QString fixture(const char* rel) {
    return QStringLiteral(OTTISK_SOURCE_DIR "/tests/fixtures/") + QLatin1String(rel);
}

QString dataDir() { return QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-fillsign-text"); }

FillSignTool& fillSignTool() {
    return *static_cast<FillSignTool*>(ToolRegistry::instance().find(QStringLiteral("fillsign")));
}

DocumentView* openDoc(MainWindow& w, const char* rel) {
    DocumentView* doc = nullptr;
    [&] {
        w.resize(1440, 900);
        w.show();
        w.activateWindow();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(w.openFile(fixture(rel)));
        QTRY_VERIFY(w.currentDocument() != nullptr && w.currentDocument()->view().pageCount() > 0);
        doc = w.currentDocument();
        doc->view().setZoomPercent(100);
        doc->view().goToPage(0);
        doc->view().setFocus();
    }();
    return doc;
}

QPoint at(const PageView& v, double x, double y, int page = 0) {
    return v.pageTransform(page).map(QPointF(x, y)).toPoint();
}

std::vector<AnnotInfo> ofType(DocumentView& doc, AnnotType type, int page = 0) {
    std::vector<AnnotInfo> out;
    for (const AnnotInfo& a : ot::annot::list(doc.session(), page).result()) {
        if (a.type == type) {
            out.push_back(a);
        }
    }
    return out;
}

int countOf(DocumentView& doc, AnnotType type) { return static_cast<int>(ofType(doc, type).size()); }

AnnotProps propsOf(DocumentView& doc, const ot::annot::AnnotRef& ref) {
    return ot::annot::props(doc.session(), ref).result().value_or(AnnotProps{});
}

// Инструмент включён на вкладке, геометрия первой страницы готова.
FillSignMode* startTool(DocumentView& doc) {
    ToolRegistry::instance().activate(QStringLiteral("fillsign"), &doc);
    FillSignMode& mode = FillSignTool::modeOf(doc.view());
    mode.geometry(0);
    (void)QTest::qWaitFor([&] { return mode.geometry(0) != nullptr; }, 10000);
    return mode.geometry(0) != nullptr ? &mode : nullptr;
}

}  // namespace

class FillSignTextTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(dataDir());
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
        QSettings(ot::app::settingsPath(), QSettings::IniFormat)
            .setValue(QLatin1String(MainWindow::kRestoreViewKey), false);
    }

    void cleanup() { ToolRegistry::instance().deactivate(); }

    void toolAndPanel() {
        ot::ui::Tool* tool = ToolRegistry::instance().find(QStringLiteral("fillsign"));
        QVERIFY(tool != nullptr);
        QVERIFY(!tool->isStub());
        QCOMPARE(tool->title(), QStringLiteral("Заполнить и подписать"));
        QVERIFY(tool->panel() != nullptr);
        for (const FillSignAction a : {FillSignAction::Text, FillSignAction::Check, FillSignAction::Cross,
                                       FillSignAction::Dot, FillSignAction::Line, FillSignAction::Box,
                                       FillSignAction::Signature, FillSignAction::Initials}) {
            QVERIFY2(fillSignTool().actionButton(a) != nullptr, qPrintable(FillSignMode::actionTitle(a)));
        }
        QVERIFY(fillSignTool().signatureMenu(ot::fillsign::SignatureKind::Signature) != nullptr);
        QVERIFY(fillSignTool().signatureMenu(ot::fillsign::SignatureKind::Initials) != nullptr);
    }

    void textSitsOnUnderline() {
        OT_FEATURE("forms-07");
        OT_FEATURE("comment-09");
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, "forms/flat/anketa.pdf");
        QVERIFY(doc != nullptr);
        FillSignMode* mode = startTool(*doc);
        QVERIFY(mode != nullptr);
        PageView& v = doc->view();
        QCOMPARE(v.tools().tool(), ViewToolId::FillSign);
        QCOMPARE(mode->action(), FillSignAction::Text);
        QVERIFY(fillSignTool().actionButton(FillSignAction::Text)->isChecked());
        QVERIFY(!fillSignTool().formHint()->isVisible());    // полей AcroForm нет
        QVERIFY(!fillSignTool().signedNote()->isVisible());  // не подписан

        // Курсор над линией — пунктир клетки линии.
        QTest::mouseMove(v.viewport(), at(v, 250, kSurnameBase - 8));
        QTRY_VERIFY(mode->hoverPlacement().has_value());
        QCOMPARE(mode->hoverPlacement()->kind, ot::fillsign::FieldKind::Line);

        QTest::mouseClick(v.viewport(), Qt::LeftButton, Qt::NoModifier, at(v, 200, kSurnameBase - 6));
        QTRY_VERIFY(InlineTextEditor::active(v) != nullptr);
        InlineTextEditor* editor = InlineTextEditor::active(v);
        editor->setPlainText(QStringLiteral("Иванов"));
        QTest::keyClick(editor, Qt::Key_Escape);
        QTRY_COMPARE(countOf(*doc, AnnotType::FreeText), 1);
        const AnnotInfo text = ofType(*doc, AnnotType::FreeText).front();
        const AnnotProps p = propsOf(*doc, text.ref);
        QCOMPARE(QString::fromStdString(p.contents), QStringLiteral("Иванов"));
        QCOMPARE(QString::fromStdString(p.fontName), QStringLiteral("Arial"));
        QCOMPARE(p.borderWidth, 0.0f);
        QVERIFY(p.color.empty());
        QCOMPARE(QString::fromStdString(p.intent), QStringLiteral("FreeTextTypeWriter"));
        // Базовая линия первой строки (вид ядра: отступ 2 пт и восходящая Arial 0,905) — на линии.
        const double baseline = p.rect.y0 + 2 + 0.905 * p.fontSize;
        QVERIFY2(baseline < kSurnameBase && baseline > kSurnameBase - 4,
                 qPrintable(QString::number(baseline)));
        QVERIFY(p.fontSize >= ot::fillsign::kMinFontSize && p.fontSize <= 20);  // клетка 20 пт без потолка
        QVERIFY(std::abs(p.rect.x0 + 2 - 200) < 1.5);

        // Щелчок «Текстом» по своему полю — правка на странице.
        QTest::mouseClick(v.viewport(), Qt::LeftButton, Qt::NoModifier,
                          at(v, (p.rect.x0 + p.rect.x1) / 2 - 100, (p.rect.y0 + p.rect.y1) / 2));
        QTRY_VERIFY(InlineTextEditor::active(v) != nullptr && InlineTextEditor::active(v)->ref().has_value());
        QCOMPARE(*InlineTextEditor::active(v)->ref(), text.ref);
        QCOMPARE(InlineTextEditor::active(v)->toPlainText(), QStringLiteral("Иванов"));
        InlineTextEditor::active(v)->commit();
    }

    void combPutsOneCharPerCell() {
        OT_FEATURE("forms-07");
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, "forms/flat/anketa.pdf");
        QVERIFY(doc != nullptr);
        FillSignMode* mode = startTool(*doc);
        QVERIFY(mode != nullptr);
        PageView& v = doc->view();
        QTest::mouseClick(v.viewport(), Qt::LeftButton, Qt::NoModifier, at(v, 205, kDateBase - 10));
        QTRY_VERIFY(CombEditor::active(v) != nullptr);
        CombEditor* comb = CombEditor::active(v);
        QCOMPARE(comb->maxLength(), 8);
        QCOMPARE(static_cast<int>(comb->placement().cells.size()), 8);
        QVERIFY(comb->placement().fontSize <= comb->placement().cellHeight);
        comb->setText(QStringLiteral("01021990"));
        QTest::keyClick(comb, Qt::Key_Return);
        QTRY_COMPARE(countOf(*doc, AnnotType::FreeText), 8);
        std::vector<AnnotProps> chars;
        for (const AnnotInfo& a : ofType(*doc, AnnotType::FreeText)) {
            chars.push_back(propsOf(*doc, a.ref));
        }
        std::sort(chars.begin(), chars.end(),
                  [](const AnnotProps& a, const AnnotProps& b) { return a.rect.x0 < b.rect.x0; });
        const char* expected = "01021990";
        for (std::size_t i = 0; i < chars.size(); ++i) {
            QCOMPARE(chars[i].contents, std::string(1, expected[i]));
            const double center = (chars[i].rect.x0 + chars[i].rect.x1) / 2;
            QVERIFY2(std::abs(center - (180 + 18 * i + 9)) < 0.6, qPrintable(QString::number(center)));
        }
        // Один шаг истории: «Отменить» снимает все 8.
        doc->session().undo();
        QTRY_COMPARE(countOf(*doc, AnnotType::FreeText), 0);
    }

    void marksSelectMoveAndDelete() {
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, "forms/flat/anketa.pdf");
        QVERIFY(doc != nullptr);
        FillSignMode* mode = startTool(*doc);
        QVERIFY(mode != nullptr);
        PageView& v = doc->view();
        // ✓ во флажок «Согласен на обработку данных» (12 × 12 у x 72, y 468–480 генератора).
        QTest::mouseClick(fillSignTool().actionButton(FillSignAction::Check), Qt::LeftButton);
        QCOMPARE(mode->action(), FillSignAction::Check);
        const QPointF boxCenter(78, top(474));
        QTest::mouseClick(v.viewport(), Qt::LeftButton, Qt::NoModifier,
                          at(v, boxCenter.x() + 2, boxCenter.y() - 1));
        QTRY_COMPARE(countOf(*doc, AnnotType::FreeText), 1);
        const AnnotInfo check = ofType(*doc, AnnotType::FreeText).front();
        const AnnotProps p = propsOf(*doc, check.ref);
        QCOMPARE(QString::fromStdString(p.contents), QStringLiteral("✓"));
        QCOMPARE(QString::fromStdString(p.fontName), QStringLiteral("Segoe UI Symbol"));
        QVERIFY(p.fontSize <= 12);
        QVERIFY(std::abs((p.rect.y0 + p.rect.y1) / 2 - boxCenter.y()) < 6);
        // Поставленная выбрана.
        QTRY_COMPARE(static_cast<int>(v.tools().annotSelect().selection().size()), 1);
        QCOMPARE(mode->action(), FillSignAction::Check);  // отметка остаётся

        // ✕ вне флажка, затем перетащить её на 40 пт вправо.
        QTest::mouseClick(fillSignTool().actionButton(FillSignAction::Cross), Qt::LeftButton);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, Qt::NoModifier, at(v, 400, 60));
        QTRY_COMPARE(countOf(*doc, AnnotType::FreeText), 2);
        AnnotInfo cross;
        for (const AnnotInfo& a : ofType(*doc, AnnotType::FreeText)) {
            if (a.ref != check.ref) {
                cross = a;
            }
        }
        QTRY_VERIFY(v.tools().annotSelect().isSelected(cross.ref));
        QTRY_VERIFY(v.tools().annotSelect().annotList(0) != nullptr &&
                    v.tools().annotSelect().annotList(0)->size() >= 2);
        const double cx = (cross.rect.x0 + cross.rect.x1) / 2;
        const double cy = (cross.rect.y0 + cross.rect.y1) / 2;
        const QPoint from = at(v, cx, cy);
        const QPoint to = at(v, cx + 40, cy);  // 40 пт правее
        QTest::mousePress(v.viewport(), Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(v.viewport(), (from + to) / 2);
        QTest::mouseMove(v.viewport(), to);
        QTest::mouseRelease(v.viewport(), Qt::LeftButton, Qt::NoModifier, to);
        auto movedX = [&] {
            for (const AnnotInfo& a : ofType(*doc, AnnotType::FreeText)) {
                if (a.ref == cross.ref) {
                    return static_cast<double>(a.rect.x0);
                }
            }
            return -1.0;
        };
        QTRY_VERIFY2(std::abs(movedX() - (cross.rect.x0 + 40)) < 2, qPrintable(QString::number(movedX())));
        QCOMPARE(countOf(*doc, AnnotType::FreeText), 2);  // перетаскивание не поставило новую
        // Del — удалить выбранную.
        QTRY_VERIFY(v.tools().annotSelect().isSelected(cross.ref));
        QTest::keyClick(v.viewport(), Qt::Key_Delete);
        QTRY_COMPARE(countOf(*doc, AnnotType::FreeText), 1);
    }

    void lockedDocumentDisablesButtons() {
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, "forms/flat/anketa.pdf");
        QVERIFY(doc != nullptr);
        ToolRegistry::instance().activate(QStringLiteral("fillsign"), doc);
        doc->setReadOnly(true);
        QTRY_VERIFY(!fillSignTool().lockReason().isEmpty());
        QVERIFY(!fillSignTool().actionButton(FillSignAction::Text)->isEnabled());
        QVERIFY(fillSignTool().lockLabel()->isVisible());
        QVERIFY(doc->view().tools().tool() != ViewToolId::FillSign);
        doc->setReadOnly(false);
        QTRY_VERIFY(fillSignTool().lockReason().isEmpty());
        QVERIFY(fillSignTool().actionButton(FillSignAction::Text)->isEnabled());
    }
};

OT_UI_TEST(FillSignTextTest, "fillsign_text");
#include "fillsign_text_test.moc"
