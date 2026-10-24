// ui.comment_tool_panel — панель «Добавить комментарии» и «Установки → Комментирование» (план 5.1, п. 4;
// эталон 3.4, 3.18; comment-08, comment-24, comment-42): стиль действия в панели (цвет, толщина карандаша,
// непрозрачность, значок записки) пишется в commenting/toolStyles и попадает в новый комментарий; «Оставлять
// инструмент выбранным» не возвращает «Выделение» после записки и запоминается; страница Установок читает и
// пишет ключи commenting/…, имя Identity без «имени входа» становится автором /T новой записки.
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFontComboBox>
#include <QLineEdit>
#include <QSettings>
#include <QSpinBox>
#include <QtTest/QtTest>

#include "core/annot/annots.h"
#include "core/annot/defaults.h"
#include "core/util/features.h"
#include "services/annot_ops.h"
#include "services/app_info.h"
#include "services/comment_settings.h"
#include "services/document_session.h"
#include "services/settings.h"
#include "services/settings_keys.h"
#include "support/ui_test.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shell/preferences/commenting_page.h"
#include "ui/shell/preferences/preferences_dialog.h"
#include "ui/tools/comment/comment_mode.h"
#include "ui/tools/comment/comment_tool.h"
#include "ui/tools/tool_registry.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/widgets/tool_button.h"

using ot::annot::AnnotInfo;
using ot::annot::AnnotType;
using ot::ui::CommentAction;
using ot::ui::CommentingPage;
using ot::ui::CommentTool;
using ot::ui::DocumentView;
using ot::ui::MainWindow;
using ot::ui::PageView;
using ot::ui::PreferencesDialog;
using ot::ui::ToolRegistry;
using ot::ui::ViewToolId;

namespace {

CommentTool& commentTool() {
    return *static_cast<CommentTool*>(ToolRegistry::instance().find(QStringLiteral("comment")));
}

DocumentView* openComment(MainWindow& w) {
    DocumentView* doc = nullptr;
    [&] {
        w.resize(1440, 900);
        w.show();
        w.activateWindow();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(w.openFile(QStringLiteral(OTTISK_SOURCE_DIR "/tests/fixtures/edit/latin.pdf")));
        QTRY_VERIFY(w.currentDocument() != nullptr && w.currentDocument()->view().pageCount() > 0);
        doc = w.currentDocument();
        doc->view().setZoomPercent(100);
        doc->view().goToPage(0);
        QVERIFY(ToolRegistry::instance().activate(QStringLiteral("comment"), doc));
        QVERIFY(commentTool().panel() != nullptr);
        doc->view().setFocus();
    }();
    return doc;
}

QPoint at(const PageView& v, double x, double y) { return v.pageTransform(0).map(QPointF(x, y)).toPoint(); }

std::vector<AnnotInfo> ofType(DocumentView& doc, AnnotType type) {
    std::vector<AnnotInfo> out;
    for (const AnnotInfo& a : ot::annot::list(doc.session(), 0).result()) {
        if (a.type == type) {
            out.push_back(a);
        }
    }
    return out;
}

}  // namespace

class CommentToolPanelTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-comment_tool_panel"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
        QSettings(ot::app::settingsPath(), QSettings::IniFormat)
            .setValue(QLatin1String(MainWindow::kRestoreViewKey), false);
    }

    void cleanup() {
        ToolRegistry::instance().deactivate();
        ot::Settings settings;
        settings.reset(ot::settings::kCommentToolStyles.name);
        settings.reset(ot::settings::kCommentKeepTool.name);
    }

    void styleControlsWriteToolStyle() {
        OT_FEATURE("comment-08");
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openComment(w);
        QVERIFY(doc != nullptr);
        CommentTool& tool = commentTool();
        // Записка: значок доступен, толщина — нет; по умолчанию — «Комментарий».
        tool.start(CommentAction::Note);
        QVERIFY(tool.noteIcon()->isEnabled());
        QVERIFY(!tool.styleWidth()->isEnabled());
        QCOMPARE(tool.noteIcon()->currentData().toString(), QStringLiteral("Comment"));
        tool.noteIcon()->setCurrentIndex(tool.noteIcon()->findData(QStringLiteral("Help")));
        tool.styleColor()->setColor(QColor(0x4C, 0xAF, 0x50));  // как выбор в палитре: цвет и сигнал
        emit tool.styleColor()->colorChanged(QColor(0x4C, 0xAF, 0x50));
        const ot::comments::ToolStyle note = ot::comments::toolStyle(ot::Settings(), QStringLiteral("note"));
        QVERIFY(note.icon.has_value());
        QCOMPARE(QString::fromStdString(*note.icon), QStringLiteral("Help"));
        QCOMPARE(note.color->toRgb(), 0x4CAF50u);

        // Карандаш: толщина и непрозрачность — в новый штрих.
        tool.start(CommentAction::Ink);
        QVERIFY(tool.styleWidth()->isEnabled());
        QVERIFY(!tool.noteIcon()->isEnabled());
        tool.styleWidth()->setValue(3);
        tool.styleOpacity()->setValue(60);
        const ot::annot::AnnotProps inkStyle = CommentTool::modeOf(doc->view()).newProps(CommentAction::Ink);
        QCOMPARE(inkStyle.borderWidth, 3.0f);
        QVERIFY(std::abs(inkStyle.opacity - 0.6f) < 0.01f);
        PageView& v = doc->view();
        QTest::mousePress(v.viewport(), Qt::LeftButton, {}, at(v, 300, 600));
        QTest::mouseMove(v.viewport(), at(v, 350, 620));
        QTest::mouseMove(v.viewport(), at(v, 400, 600));
        QTest::mouseRelease(v.viewport(), Qt::LeftButton, {}, at(v, 400, 600));
        QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(ofType(*doc, AnnotType::Ink).size()), 1, 10000);
        const auto props =
            ot::annot::props(doc->session(), ofType(*doc, AnnotType::Ink).front().ref).result();
        QVERIFY(props.has_value());
        QCOMPARE(props->borderWidth, 3.0f);
        QVERIFY(std::abs(props->opacity - 0.6f) < 0.01f);
        // Значок записки из стиля — в новой записке.
        tool.start(CommentAction::Note);
        QCOMPARE(tool.noteIcon()->currentData().toString(), QStringLiteral("Help"));
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 450, 650));
        QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(ofType(*doc, AnnotType::Text).size()), 1, 10000);
        const auto noteProps =
            ot::annot::props(doc->session(), ofType(*doc, AnnotType::Text).front().ref).result();
        QCOMPARE(QString::fromStdString(noteProps->icon), QStringLiteral("Help"));
        QCOMPARE(noteProps->color.toRgb(), 0x4CAF50u);
    }

    void pinKeepsToolSelected() {
        OT_FEATURE("comment-42");
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openComment(w);
        QVERIFY(doc != nullptr);
        CommentTool& tool = commentTool();
        PageView& v = doc->view();
        QVERIFY(!tool.pinButton()->isChecked());
        tool.pinButton()->click();
        QVERIFY(tool.pinButton()->isChecked());
        QVERIFY(v.tools().pinned());
        QVERIFY(ot::Settings().get(ot::settings::kCommentKeepTool));
        tool.start(CommentAction::Note);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 400, 600));
        QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(ofType(*doc, AnnotType::Text).size()), 1, 10000);
        QCOMPARE(v.tools().tool(), ViewToolId::Comment);  // закреплён — остаётся
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 400, 700));
        QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(ofType(*doc, AnnotType::Text).size()), 2, 10000);
        tool.pinButton()->click();
        QVERIFY(!v.tools().pinned());
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 300, 700));
        QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(ofType(*doc, AnnotType::Text).size()), 3, 10000);
        QCOMPARE(v.tools().tool(), ViewToolId::Select);
    }

    void preferencesPageAndIdentityAuthor() {
        OT_FEATURE("comment-24");
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openComment(w);
        QVERIFY(doc != nullptr);
        PreferencesDialog* d = w.showPreferences(QStringLiteral("commenting"));
        auto* page = qobject_cast<CommentingPage*>(d->page(QStringLiteral("commenting")));
        QVERIFY(page != nullptr);
        QCOMPARE(page->stage(), 0);
        QCOMPARE(page->title(), QStringLiteral("Комментирование"));
        // Умолчания.
        QVERIFY(page->loginNameBox()->isChecked());
        QVERIFY(page->hoverBox()->isChecked());
        QVERIFY(page->connectorBox()->isChecked());
        QVERIFY(page->openOnCreateBox()->isChecked());
        QVERIFY(!page->keepToolBox()->isChecked());
        QCOMPARE(page->fontSizeBox()->value(), 10);
        // Identity без имени входа.
        page->loginNameBox()->setChecked(false);
        page->nameEdit()->setText(QStringLiteral("Иван Петров"));
        page->titleEdit()->setText(QStringLiteral("Редактор"));
        page->organizationEdit()->setText(QStringLiteral("АкроБатя"));
        page->fontSizeBox()->setValue(12);
        page->connectorBox()->setChecked(false);
        d->accept();
        const ot::Settings s;
        QVERIFY(!s.get(ot::settings::kCommentAlwaysLoginName));
        QCOMPARE(s.get(ot::settings::kCommentIdentityName), QStringLiteral("Иван Петров"));
        QCOMPARE(s.get(ot::settings::kCommentIdentityTitle), QStringLiteral("Редактор"));
        QCOMPARE(s.get(ot::settings::kCommentIdentityOrganization), QStringLiteral("АкроБатя"));
        QCOMPARE(s.get(ot::settings::kCommentPopupFontSize), 12);
        QVERIFY(!s.get(ot::settings::kCommentConnectorLines));
        QCOMPARE(QString::fromStdString(ot::comments::author(s)), QStringLiteral("Иван Петров"));

        PageView& v = doc->view();
        commentTool().start(CommentAction::Note);
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 400, 600));
        QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(ofType(*doc, AnnotType::Text).size()), 1, 10000);
        QCOMPARE(QString::fromStdString(ofType(*doc, AnnotType::Text).front().author),
                 QStringLiteral("Иван Петров"));

        // Вернуть «имя входа» — автор снова имя пользователя Windows.
        PreferencesDialog* again = w.showPreferences(QStringLiteral("commenting"));
        auto* page2 = qobject_cast<CommentingPage*>(again->page(QStringLiteral("commenting")));
        QVERIFY(!page2->loginNameBox()->isChecked());
        QCOMPARE(page2->nameEdit()->text(), QStringLiteral("Иван Петров"));
        page2->loginNameBox()->setChecked(true);
        again->accept();
        QCOMPARE(QString::fromStdString(ot::comments::author(ot::Settings())),
                 QString::fromStdString(ot::annot::loginName()));
    }
};

OT_UI_TEST(CommentToolPanelTest, "comment_tool_panel");
#include "comment_tool_panel_test.moc"
