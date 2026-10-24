// ui.annot_props — «Свойства комментария» (план 5.2, п. 3; эталон 3.4 «Свойства комментария»; comment-08):
// «Свойства…» контекстного меню выбранного открывает диалог; набор полей «Вида» — по типу (прямоугольник —
// цвет, заливка, толщина, стиль с облаком; записка — значок; текстовое поле — заливка, рамка, шрифт, кегль,
// цвет текста, выравнивание); «ОК» пишет изменённое одним шагом «Set annotation properties»; «Общие» — автор
// и тема; «Заблокировано» (/F Locked) — «Выбор объектов» не двигает и не удаляет; «Сделать свойствами по
// умолчанию» — следующая аннотация типа получает стиль; меню целиком — «Изменить текст» у поля, «Ответить» и
// «Статус» серые.
#include <QAction>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSettings>
#include <QTabWidget>
#include <QtTest/QtTest>

#include <cmath>

#include "core/annot/annots.h"
#include "core/annot/defaults.h"
#include "core/util/features.h"
#include "services/annot_ops.h"
#include "services/app_info.h"
#include "services/comment_settings.h"
#include "services/document_session.h"
#include "services/settings.h"
#include "support/ui_test.h"
#include "ui/dialogs/annot_props_dialog.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/tools/comment/comment_mode.h"
#include "ui/tools/comment/comment_tool.h"
#include "ui/tools/tool_registry.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"
#include "ui/widgets/color_swatch.h"
#include "ui/widgets/number_field.h"

using ot::annot::AnnotInfo;
using ot::annot::AnnotProps;
using ot::annot::AnnotType;
using ot::ui::AnnotPropsDialog;
using ot::ui::AnnotSelectTool;
using ot::ui::CommentAction;
using ot::ui::CommentTool;
using ot::ui::DocumentView;
using ot::ui::MainWindow;
using ot::ui::PageView;
using ot::ui::ToolRegistry;
using ot::ui::ViewToolId;

namespace {

QString fixture(const char* rel) {
    return QStringLiteral(OTTISK_SOURCE_DIR "/tests/fixtures/") + QLatin1String(rel);
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

AnnotProps propsOf(DocumentView& doc, const ot::annot::AnnotRef& ref) {
    return ot::annot::props(doc.session(), ref).result().value_or(AnnotProps{});
}

QAction* menuAction(QMenu& menu, const char* name) {
    for (QAction* a : menu.actions()) {
        if (a->objectName() == QLatin1String(name)) {
            return a;
        }
    }
    return nullptr;
}

// «Свойства…» из меню выбранного → открытый и загруженный диалог.
AnnotPropsDialog* openProperties(MainWindow& w, PageView& v, const AnnotInfo& info) {
    v.tools().setTool(ViewToolId::AnnotSelect);
    v.tools().annotSelect().setSelection({info});
    QMenu menu;
    v.tools().annotSelect().fillContextMenu(menu);
    QAction* props = menuAction(menu, "commentProperties");
    if (props == nullptr || !props->isEnabled()) {
        return nullptr;
    }
    props->trigger();
    // Открытый (прежний, закрытый, ещё ждёт deleteLater) и загруженный.
    auto shown = [&w]() -> AnnotPropsDialog* {
        for (AnnotPropsDialog* d : w.findChildren<AnnotPropsDialog*>()) {
            if (d->isVisible() && d->loaded()) {
                return d;
            }
        }
        return nullptr;
    };
    [&] { QTRY_VERIFY_WITH_TIMEOUT(shown() != nullptr, 10000); }();
    return shown();
}

}  // namespace

class AnnotPropsTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-annot_props"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
        QSettings(ot::app::settingsPath(), QSettings::IniFormat)
            .setValue(QLatin1String(MainWindow::kRestoreViewKey), false);
    }

    void cleanup() { ToolRegistry::instance().deactivate(); }

    void appearanceByTypeOneStep() {
        OT_FEATURE("comment-08");
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, "edit/latin.pdf");
        QVERIFY(doc != nullptr);
        PageView& v = doc->view();
        AnnotProps sp = ot::annot::toolDefaults(AnnotType::Square);
        sp.rect = {100, 300, 260, 400};
        const auto square = ot::annot::create(doc->session(), 0, sp).result();
        AnnotPropsDialog* d = openProperties(w, v, ofType(*doc, AnnotType::Square).front());
        QVERIFY(d != nullptr);
        QCOMPARE(d->windowTitle(), QStringLiteral("Свойства: Прямоугольник"));
        QVERIFY(d->rowVisible(d->colorSwatch()) && d->rowVisible(d->fillSwatch()) &&
                d->rowVisible(d->widthField()));
        QVERIFY(d->rowVisible(d->lineStyleBox()) && !d->rowVisible(d->iconBox()) &&
                !d->rowVisible(d->fontBox()));
        QVERIFY(d->lineStyleBox()->findData(AnnotPropsDialog::Cloud2) >= 0);
        QVERIFY(d->patch().empty());
        d->widthField()->setValue(3);
        d->fillSwatch()->presetButtons().at(3)->click();
        d->lineStyleBox()->setCurrentIndex(d->lineStyleBox()->findData(AnnotPropsDialog::Cloud2));
        d->opacityField()->setValue(60);
        const quint64 revision = doc->session().revision();
        d->okButton()->click();
        QTRY_VERIFY_WITH_TIMEOUT(propsOf(*doc, square).cloudy, 10000);
        const AnnotProps p = propsOf(*doc, square);
        QCOMPARE(p.borderWidth, 3.0f);
        QVERIFY(std::abs(p.cloudIntensity - 2) < 0.01);
        QVERIFY(std::abs(p.opacity - 0.6f) < 0.01);
        QVERIFY(!p.interiorColor.empty());
        QCOMPARE(doc->session().revision(), revision + 1);  // один шаг
        QCOMPARE(doc->session().undoState().undoName, QStringLiteral("Set annotation properties"));

        // Записка: значок; текстовое поле — заливка вместо цвета и поля текста.
        AnnotProps np = ot::annot::toolDefaults(AnnotType::Text);
        np.rect = {400, 300, 420, 320};
        const auto note = ot::annot::create(doc->session(), 0, np).result();
        d = openProperties(w, v, ofType(*doc, AnnotType::Text).front());
        QVERIFY(d != nullptr);
        QVERIFY(d->rowVisible(d->iconBox()) && !d->rowVisible(d->fillSwatch()) &&
                !d->rowVisible(d->widthField()));
        d->iconBox()->setCurrentIndex(d->iconBox()->findData(QStringLiteral("Key")));
        d->showTab(AnnotPropsDialog::General);
        d->authorEdit()->setText(QStringLiteral("Иван Петров"));
        d->subjectEdit()->setText(QStringLiteral("Вопрос"));
        d->okButton()->click();
        QTRY_COMPARE_WITH_TIMEOUT(QString::fromStdString(propsOf(*doc, note).icon), QStringLiteral("Key"),
                                  10000);
        QCOMPARE(QString::fromStdString(propsOf(*doc, note).author), QStringLiteral("Иван Петров"));
        QCOMPARE(QString::fromStdString(propsOf(*doc, note).subject), QStringLiteral("Вопрос"));

        AnnotProps fp = ot::annot::toolDefaults(AnnotType::FreeText);
        fp.rect = {100, 500, 300, 540};
        fp.contents = "Text";
        const auto box = ot::annot::create(doc->session(), 0, fp).result();
        d = openProperties(w, v, ofType(*doc, AnnotType::FreeText).front());
        QVERIFY(d != nullptr);
        QVERIFY(d->rowVisible(d->fontBox()) && d->rowVisible(d->alignBar()) && !d->rowVisible(d->iconBox()));
        QVERIFY(d->colorSwatch()->noneAllowed());
        d->fontBox()->setCurrentIndex(d->fontBox()->findData(QStringLiteral("Courier New")));
        d->fontSizeField()->setValue(18);
        d->okButton()->click();
        QTRY_COMPARE_WITH_TIMEOUT(QString::fromStdString(propsOf(*doc, box).fontName),
                                  QStringLiteral("Courier New"), 10000);
        QCOMPARE(propsOf(*doc, box).fontSize, 18.0f);
    }

    void lockedDoesNotMove() {
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, "edit/latin.pdf");
        QVERIFY(doc != nullptr);
        PageView& v = doc->view();
        AnnotProps sp = ot::annot::toolDefaults(AnnotType::Circle);
        sp.rect = {100, 300, 200, 380};
        const auto circle = ot::annot::create(doc->session(), 0, sp).result();
        AnnotPropsDialog* d = openProperties(w, v, ofType(*doc, AnnotType::Circle).front());
        QVERIFY(d != nullptr);
        QVERIFY(!d->lockedBox()->isChecked());
        d->lockedBox()->setChecked(true);
        d->okButton()->click();
        QTRY_VERIFY_WITH_TIMEOUT((propsOf(*doc, circle).flags & ot::annot::kAnnotLocked) != 0, 10000);
        // Выбор со свежими флагами: «Заблокировано» — без ручек, не тянется и не удаляется.
        AnnotSelectTool& tool = v.tools().annotSelect();
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, at(v, 150, 340));
        QTRY_VERIFY_WITH_TIMEOUT(tool.selection().size() == 1 && tool.selection().front().locked(), 10000);
        QVERIFY(!AnnotSelectTool::movable(tool.selection().front()));
        const quint64 revision = doc->session().revision();
        const QPoint from = at(v, 150, 340);
        QTest::mousePress(v.viewport(), Qt::LeftButton, {}, from);
        QTest::mouseMove(v.viewport(), from + QPoint(30, 20));
        QTest::mouseMove(v.viewport(), from + QPoint(60, 40));
        QTest::mouseRelease(v.viewport(), Qt::LeftButton, {}, from + QPoint(60, 40));
        QTest::keyClick(v.viewport(), Qt::Key_Delete);
        QTest::qWait(200);
        QCOMPARE(doc->session().revision(), revision);
        QVERIFY(std::abs(propsOf(*doc, circle).rect.x0 - 100) < 0.5);
        // Снять блокировку — снова тянется.
        d = openProperties(w, v, tool.selection().front());
        QVERIFY(d != nullptr && d->lockedBox()->isChecked());
        d->lockedBox()->setChecked(false);
        d->okButton()->click();
        QTRY_VERIFY_WITH_TIMEOUT((propsOf(*doc, circle).flags & ot::annot::kAnnotLocked) == 0, 10000);
    }

    void makeDefaultAppliesToNext() {
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, "edit/latin.pdf");
        QVERIFY(doc != nullptr);
        PageView& v = doc->view();
        AnnotProps rp = ot::annot::toolDefaults(AnnotType::Square);
        rp.rect = {100, 300, 200, 380};
        ot::annot::create(doc->session(), 0, rp).result();
        AnnotPropsDialog* d = openProperties(w, v, ofType(*doc, AnnotType::Square).front());
        QVERIFY(d != nullptr);
        d->colorSwatch()->presetButtons().at(5)->click();
        d->widthField()->setValue(4);
        d->makeDefaultBox()->setChecked(true);
        const QColor chosen = d->colorSwatch()->color();
        d->okButton()->click();
        const auto style = ot::comments::toolStyle(ot::Settings(), QStringLiteral("rectangle"));
        QVERIFY(style.color.has_value() && style.width.has_value());
        QCOMPARE(*style.width, 4.0f);
        QCOMPARE(QColor::fromRgb(static_cast<QRgb>(style.color->toRgb())).name(), chosen.name());

        // Следующий прямоугольник инструментом — со стилем по умолчанию.
        QVERIFY(ToolRegistry::instance().activate(QStringLiteral("comment"), doc));
        auto* tool = static_cast<CommentTool*>(ToolRegistry::instance().find(QStringLiteral("comment")));
        tool->start(CommentAction::Rectangle);
        const QPoint a = at(v, 300, 500);
        const QPoint b = at(v, 400, 560);
        QTest::mousePress(v.viewport(), Qt::LeftButton, {}, a);
        QTest::mouseMove(v.viewport(), (a + b) / 2);
        QTest::mouseMove(v.viewport(), b);
        QTest::mouseRelease(v.viewport(), Qt::LeftButton, {}, b);
        QTRY_COMPARE_WITH_TIMEOUT(static_cast<int>(ofType(*doc, AnnotType::Square).size()), 2, 10000);
        AnnotProps next;
        for (const AnnotInfo& i : ofType(*doc, AnnotType::Square)) {
            const AnnotProps p = propsOf(*doc, i.ref);
            if (p.rect.x0 > 250) {
                next = p;
            }
        }
        QCOMPARE(next.borderWidth, 4.0f);
        QCOMPARE(QColor::fromRgb(static_cast<QRgb>(next.color.toRgb())).name(), chosen.name());
    }

    void menuWhole() {
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, "edit/latin.pdf");
        QVERIFY(doc != nullptr);
        PageView& v = doc->view();
        AnnotProps fp = ot::annot::toolDefaults(AnnotType::FreeText);
        fp.rect = {100, 500, 300, 540};
        fp.contents = "Text";
        ot::annot::create(doc->session(), 0, fp).result();
        v.tools().setTool(ViewToolId::AnnotSelect);
        v.tools().annotSelect().setSelection(ofType(*doc, AnnotType::FreeText));
        QMenu menu;
        v.tools().annotSelect().fillContextMenu(menu);
        QAction* edit = menuAction(menu, "commentEditText");
        QAction* open = menuAction(menu, "commentOpenPopup");
        QVERIFY(edit != nullptr && edit->isVisible() && edit->isEnabled());
        QVERIFY(open != nullptr && !open->isVisible());
        QVERIFY(menuAction(menu, "commentProperties")->isEnabled());
        QVERIFY(menuAction(menu, "commentMakeDefault")->isEnabled());
        edit->trigger();
        QTRY_VERIFY_WITH_TIMEOUT(
            v.viewport()->findChild<QWidget*>(QStringLiteral("inlineTextEditor")) != nullptr, 10000);
    }
};

OT_UI_TEST(AnnotPropsTest, "annot_props");
#include "annot_props_test.moc"
