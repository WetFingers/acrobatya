// ui.format_panel — панель FORMAT TEXT, «Добавить текст» и «Установки → Редактирование содержимого» (план
// 3.4, п. 2; эталон 3.9 «Инструмент»; edit-02, edit-03 — UI, edit-49): кнопка «Ж» делает выделенное слово
// жирным — после записи у его символов в stext полужирный шрифт; кегль и выравнивание из панели меняют стиль
// и абзац; надстрочный — индекс; «Добавить текст» + щелчок на пустом месте страницы создаёт блок шириной
// 200 пт, набранный текст после записи виден в stext; страница Установок читает и пишет ключи editing/, шрифт
// по умолчанию идёт в стиль нового текста.
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QSettings>
#include <QSpinBox>
#include <QtTest/QtTest>

#include "core/text/stext.h"
#include "services/app_info.h"
#include "services/document_session.h"
#include "services/settings.h"
#include "support/ui_test.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shell/preferences/editing_page.h"
#include "ui/tools/edit/edit_mode.h"
#include "ui/tools/edit/edit_tool.h"
#include "ui/tools/edit/format_panel.h"
#include "ui/tools/tool_registry.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/widgets/number_field.h"
#include "ui/widgets/tool_button.h"

using ot::ui::DocumentView;
using ot::ui::EditMode;
using ot::ui::EditTool;
using ot::ui::FormatPanel;
using ot::ui::MainWindow;
using ot::ui::PageView;
using ot::ui::ToolRegistry;
using ot::ui::ViewToolId;

namespace {

QString fixture(const char* rel) {
    return QStringLiteral(OTTISK_SOURCE_DIR "/tests/fixtures/") + QLatin1String(rel);
}

EditTool& editTool() {
    return *static_cast<EditTool*>(ToolRegistry::instance().find(QStringLiteral("edit")));
}

DocumentView* openEdit(MainWindow& w, const char* rel) {
    DocumentView* doc = nullptr;
    [&] {
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        w.resize(1440, 900);
        w.show();
        w.activateWindow();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(w.openFile(fixture(rel)));
        QTRY_VERIFY(w.currentDocument() != nullptr && w.currentDocument()->view().pageCount() > 0);
        doc = w.currentDocument();
        doc->view().setZoomPercent(100);
        doc->view().goToPage(0);
        QVERIFY(ToolRegistry::instance().activate(QStringLiteral("edit"), doc));
        QTRY_VERIFY_WITH_TIMEOUT(EditTool::modeOf(doc->view()).hasContent(0), 10000);
        doc->view().setFocus();
    }();
    return doc;
}

std::shared_ptr<const ot::TextPage> pageText(DocumentView& doc) { return doc.session().textPage(0).result(); }

// Шрифт символов слова word на странице: полужирный ли.
bool wordIsBold(const ot::TextPage& page, const std::string& word) {
    const std::string text = page.text();
    const std::size_t at = text.find(word);
    if (at == std::string::npos) {
        return false;
    }
    // Индекс символа — по числу кодовых точек до at.
    const QString prefix = QString::fromUtf8(text.data(), static_cast<qsizetype>(at));
    const int first = static_cast<int>(prefix.toStdU32String().size());
    const auto& chars = page.chars();
    const auto& fonts = page.fonts();
    for (int i = first; i < first + static_cast<int>(word.size()); ++i) {
        const ot::TextChar& c = chars[static_cast<std::size_t>(i)];
        if (c.font < 0) {
            return false;
        }
        const ot::TextFont& f = fonts[static_cast<std::size_t>(c.font)];
        if (!f.bold && f.name.find("Bold") == std::string::npos) {
            return false;
        }
    }
    return true;
}

}  // namespace

class FormatPanelTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-format_panel"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
        QSettings(ot::app::settingsPath(), QSettings::IniFormat)
            .setValue(QLatin1String(MainWindow::kRestoreViewKey), false);
    }

    void cleanup() { ToolRegistry::instance().deactivate(); }

    void boldButtonMakesWordBold() {
        std::printf("OT_FEATURE:edit-03\n");
        MainWindow w;
        DocumentView* doc = openEdit(w, "edit/latin.pdf");
        QVERIFY(doc != nullptr);
        EditMode& mode = EditTool::modeOf(doc->view());
        FormatPanel* panel = editTool().formatPanel();
        QVERIFY(panel != nullptr);
        QCOMPARE(panel->mode(), &mode);
        const EditMode::Frame first = mode.frames().front();
        QVERIFY(mode.enterBlock(first.page, first.block, 0));
        QVERIFY(QString::fromStdU32String(mode.editor()->text()).startsWith(QStringLiteral("The editor ")));
        mode.setSelection(4, 10);  // «editor»
        QVERIFY(!panel->boldButton()->isChecked());
        QVERIFY(panel->boldButton()->isEnabled());
        QTest::mouseClick(panel->boldButton(), Qt::LeftButton);
        QVERIFY(mode.editor()->styleAt(4).bold);
        QVERIFY(mode.editor()->styleAt(9).bold);
        QVERIFY(!mode.editor()->styleAt(0).bold);
        QVERIFY(!mode.editor()->styleAt(11).bold);
        QVERIFY(panel->boldButton()->isChecked());
        mode.leaveBlock();
        QTRY_VERIFY_WITH_TIMEOUT(!mode.hasPendingEdit(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(doc->session().undoState().canUndo, 10000);
        const auto text = pageText(*doc);
        QVERIFY(text != nullptr);
        QVERIFY2(wordIsBold(*text, "editor"), text->text().c_str());
        QVERIFY(!wordIsBold(*text, "The"));
    }

    void sizeAlignmentAndScriptFromPanel() {
        MainWindow w;
        DocumentView* doc = openEdit(w, "edit/latin.pdf");
        QVERIFY(doc != nullptr);
        EditMode& mode = EditTool::modeOf(doc->view());
        FormatPanel* panel = editTool().formatPanel();
        const EditMode::Frame first = mode.frames().front();
        QVERIFY(mode.enterBlock(first.page, first.block, 0));
        mode.setSelection(0, 3);  // «The»
        panel->sizeField()->setValue(20);
        QCOMPARE(mode.editor()->styleAt(0).size, 20.0f);
        QVERIFY(mode.editor()->styleAt(5).size < 20.0f);
        QTest::mouseClick(panel->alignButton(ot::edit::Align::Center), Qt::LeftButton);
        QCOMPARE(mode.editor()->alignment(), ot::edit::Align::Center);
        QVERIFY(panel->alignButton(ot::edit::Align::Center)->isChecked());
        QVERIFY(!panel->alignButton(ot::edit::Align::Left)->isChecked());
        mode.setSelection(4, 10);
        QTest::mouseClick(panel->superscriptButton(), Qt::LeftButton);
        QCOMPARE(ot::edit::scriptOf(mode.editor()->styleAt(4)), ot::edit::Script::Super);
        panel->lineSpacingField()->setValue(2.0);
        QCOMPARE(mode.editor()->lineSpacing(), 2.0f);
        QVERIFY(mode.hasPendingEdit());
        QVERIFY(mode.revertPending());
    }

    void addTextCreatesBlockVisibleInStext() {
        std::printf("OT_FEATURE:edit-02\n");
        MainWindow w;
        DocumentView* doc = openEdit(w, "edit/latin.pdf");
        QVERIFY(doc != nullptr);
        PageView& v = doc->view();
        EditMode& mode = EditTool::modeOf(v);
        EditTool& tool = editTool();
        QTest::mouseClick(tool.addTextButton(), Qt::LeftButton);
        QVERIFY(mode.addTextArmed());
        QVERIFY(tool.addTextButton()->isChecked());
        const QPoint at = v.pageTransform(0).map(QPointF(72, 640)).toPoint();
        QCOMPARE(v.viewport()->cursor().shape(), Qt::ArrowCursor);  // курсор — после движения
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at);
        QTRY_VERIFY_WITH_TIMEOUT(mode.editing(), 10000);
        QVERIFY(!mode.addTextArmed());
        QVERIFY(mode.editor()->empty());
        QCOMPARE(mode.editor()->geometry().width, EditMode::kNewBlockWidth);
        const std::optional<QRectF> frame = mode.activeFrame();
        QVERIFY(frame.has_value());
        QVERIFY(std::abs(frame->left() - at.x()) <= 3);
        QTest::keyClicks(&v, QStringLiteral("Hello there"));
        QCOMPARE(QString::fromStdU32String(mode.editor()->text()), QStringLiteral("Hello there"));
        QCOMPARE(QString::fromStdString(mode.editor()->styleAt(0).family), QStringLiteral("Arial"));
        mode.leaveBlock();
        QTRY_VERIFY_WITH_TIMEOUT(!mode.hasPendingEdit(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(doc->session().undoState().canUndo, 10000);
        const auto text = pageText(*doc);
        QVERIFY2(text->text().find("Hello there") != std::string::npos, text->text().c_str());
        // Блок появился среди блоков страницы.
        QTRY_VERIFY_WITH_TIMEOUT(mode.hasContent(0), 10000);
        QCOMPARE(static_cast<int>(mode.frames().size()), 4);
        // Пустой новый блок исчезает без записи.
        mode.addTextAt(0, QPointF(300, 700));
        QTRY_VERIFY_WITH_TIMEOUT(mode.editing(), 10000);
        mode.leaveBlock();
        QTest::qWait(50);
        QCOMPARE(doc->session().undoState().undoName, QStringLiteral("Edit text"));
    }

    void moveAndWidthHandles() {
        MainWindow w;
        DocumentView* doc = openEdit(w, "edit/latin.pdf");
        QVERIFY(doc != nullptr);
        PageView& v = doc->view();
        EditMode& mode = EditTool::modeOf(v);
        const EditMode::Frame first = mode.frames().front();
        QVERIFY(mode.enterBlock(first.page, first.block, 0));
        const QRectF frame = *mode.activeFrame();
        const QPointF right(frame.right() + 2, frame.center().y());
        QCOMPARE(mode.handleAt(right), EditMode::Handle::Right);
        QCOMPARE(mode.handleAt(QPointF(frame.center().x(), frame.top() - 2)), EditMode::Handle::Move);
        const float width = mode.editor()->geometry().width;
        QTest::mousePress(v.viewport(), Qt::LeftButton, {}, right.toPoint());
        QTest::mouseMove(v.viewport(), (right - QPointF(60, 0)).toPoint());
        QTest::mouseRelease(v.viewport(), Qt::LeftButton, {}, (right - QPointF(60, 0)).toPoint());
        QVERIFY2(mode.editor()->geometry().width < width - 30, qPrintable(QString::number(width)));
        const float left = mode.editor()->geometry().left;
        const QPointF top(frame.center().x(), frame.top() - 2);
        QTest::mousePress(v.viewport(), Qt::LeftButton, {}, top.toPoint());
        QTest::mouseMove(v.viewport(), (top + QPointF(30, 40)).toPoint());
        QTest::mouseRelease(v.viewport(), Qt::LeftButton, {}, (top + QPointF(30, 40)).toPoint());
        QVERIFY(mode.editor()->geometry().left > left + 15);
        QVERIFY(mode.hasPendingEdit());
        mode.leaveBlock();
        QTRY_VERIFY_WITH_TIMEOUT(!mode.hasPendingEdit(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(doc->session().undoState().canUndo, 10000);
    }

    void editingPreferencesPage() {
        std::printf("OT_FEATURE:edit-49\n");
        ot::Settings settings;
        ot::ui::EditingPage page;
        page.load(settings);
        QCOMPARE(page.defaultFontBox()->currentData().toString(), QStringLiteral("Arial"));
        QCOMPARE(page.defaultSizeBox()->value(), 12);
        QCOMPARE(page.fallbackBox()->currentIndex(), 0);
        QCOMPARE(page.fallbackBox()->currentText(), QStringLiteral("Выбрать автоматически"));
        QVERIFY(page.systemFontsBox()->isChecked());
        page.defaultSizeBox()->setValue(18);
        page.fallbackBox()->setCurrentIndex(page.fallbackBox()->findData(QStringLiteral("Georgia")));
        page.systemFontsBox()->setChecked(false);
        page.apply(settings);
        QCOMPARE(settings.get(ot::settings::kEditDefaultFontSize), 18);
        QCOMPARE(settings.get(ot::settings::kEditFallbackFont), QStringLiteral("Georgia"));
        QVERIFY(!settings.get(ot::settings::kEditSystemFonts));

        MainWindow w;
        DocumentView* doc = openEdit(w, "edit/latin.pdf");
        QVERIFY(doc != nullptr);
        EditMode& mode = EditTool::modeOf(doc->view());
        QCOMPARE(mode.newTextStyle().size, 18.0f);
        QCOMPARE(QString::fromStdString(mode.newTextStyle().family), QStringLiteral("Arial"));
        // Без системных — в списке только шрифты документа.
        QComboBox* family = editTool().formatPanel()->familyBox();
        QVERIFY(family->findData(QStringLiteral("Georgia")) < 0);
        settings.set(ot::settings::kEditDefaultFontSize, ot::settings::kEditDefaultFontSize.def);
        settings.set(ot::settings::kEditFallbackFont, QString());
        settings.set(ot::settings::kEditSystemFonts, true);
        editTool().settingsChanged();
        QCOMPARE(mode.newTextStyle().size, 12.0f);
        QVERIFY(family->findData(QStringLiteral("Georgia")) >= 0);
    }
};

OT_UI_TEST(FormatPanelTest, "format_panel");
#include "format_panel_test.moc"
