// ui.form_fill — заполнение формы на вкладке (план 6.1, п. 2; эталон 3.6, 6.1, 7.2; forms-01, forms-02,
// forms-03, forms-05, forms-15) на tests/fixtures/forms/forms-all.pdf: фиолетовая полоса «Документ содержит
// поля [Подсветить поля]» и подсветка; жёлтая полоса «JavaScript отключён [Включить для этого файла]» — без
// JavaScript формат не применяется, «Включить» запоминает файл в state.sqlite и включает вычисления; щелчок
// по текстовому полю — редактор поверх поля, Enter записывает шагом «Fill field» (кириллица); флажок щелчком;
// combo открывает список; дата — календарь по AFDate_FormatEx; Tab обходит поля по /Tabs /R; проверка
// сценарием отклоняет «abc» окном app.alert (фейковый ответчик) и рамкой; кнопка «Сброс» возвращает /DV;
// отправка и поле подписи — сигналы окну; XFA — своя полоса; запись — инкрементом; --script form.setValue.
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include "core/doc/saver.h"
#include "core/forms/data_io.h"
#include "core/forms/fields.h"
#include "core/util/features.h"
#include "services/app_info.h"
#include "services/document_session.h"
#include "services/forms_ops.h"
#include "services/js_prompter.h"
#include "services/script_runner.h"
#include "services/settings.h"
#include "services/settings_keys.h"
#include "services/state_db.h"
#include "support/ui_test.h"
#include "ui/app/script_actions.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shell/message_bar.h"
#include "ui/tools/forms/field_editors.h"
#include "ui/tools/forms/form_fill_tool.h"
#include "ui/view/page_view.h"
#include "ui/widgets/message_bar.h"

using ot::Settings;
using ot::ui::DocumentView;
using ot::ui::FieldEditor;
using ot::ui::FormFillTool;
using ot::ui::MainWindow;
using ot::ui::MessageKind;
using ot::ui::PageView;
namespace forms = ot::forms;
namespace keys = ot::settings;

Q_DECLARE_METATYPE(ot::forms::FieldAction)

namespace {

QString fixture(const char* name) {
    return QStringLiteral(OTTISK_SOURCE_DIR "/tests/fixtures/forms/") + QLatin1String(name);
}
QString dataDir() { return QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-form-fill"); }

DocumentView* openDoc(MainWindow& w, const QString& path) {
    DocumentView* doc = nullptr;
    [&] {
        w.resize(1440, 900);
        w.show();
        w.activateWindow();
        QVERIFY(QTest::qWaitForWindowExposed(&w));
        QVERIFY(w.openFile(path));
        QTRY_VERIFY(w.currentDocument() != nullptr && w.currentDocument()->view().pageCount() > 0);
        doc = w.currentDocument();
        QTRY_VERIFY(doc->forms().isLoaded());
        doc->view().setZoomPercent(100);
        doc->view().goToPage(0);
        doc->view().setFocus();
    }();
    return doc;
}

const forms::Field* field(DocumentView& doc, const char* name) { return doc.forms().form().find(name); }

QPoint centerOf(DocumentView& doc, const forms::Widget& w) {
    return doc.view()
        .pageTransform(w.page)
        .mapRect(QRectF(QPointF(w.rect.x0, w.rect.y0), QPointF(w.rect.x1, w.rect.y1)))
        .center()
        .toPoint();
}

forms::Field current(DocumentView& doc, const char* name) {
    const forms::Form f = forms::read(doc.session()).result();
    return *f.find(name);
}

}  // namespace

class FormFillTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        qRegisterMetaType<ot::forms::FieldAction>();
        QDir data(dataDir());
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
        QSettings(ot::app::settingsPath(), QSettings::IniFormat)
            .setValue(QLatin1String(MainWindow::kRestoreViewKey), false);
    }

    void cleanup() {
        Settings s;
        s.reset(keys::kFormHighlight.name);
        s.reset(keys::kJavaScript.name);
    }

    void formBarAndHighlight() {
        OT_FEATURE("forms-02");
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, fixture("forms-all.pdf"));
        QVERIFY(doc != nullptr);
        QVERIFY(doc->forms().hasForm());
        ot::ui::MessageBar* bar = doc->messageStack().bar(MessageKind::Form);
        QVERIFY(bar != nullptr);
        QCOMPARE(bar->objectName(), QStringLiteral("formBar"));
        auto* button = bar->findChild<QPushButton*>(QStringLiteral("formHighlightButton"));
        QVERIFY(button != nullptr);
        QCOMPARE(button->text(), QStringLiteral("Подсветить поля"));
        QVERIFY(!doc->forms().highlight());
        button->click();
        QVERIFY(doc->forms().highlight());
        QVERIFY(Settings().get(keys::kFormHighlight));
        QCOMPARE(button->text(), QStringLiteral("Убрать подсветку"));
        QVERIFY(doc->messageStack().bar(MessageKind::Xfa) == nullptr);
    }

    void javaScriptOffByDefaultThenEnabledForFile() {
        OT_FEATURE("security-40");
        OT_FEATURE("forms-31");
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, fixture("forms-all.pdf"));
        QVERIFY(doc != nullptr);
        QCOMPARE(doc->forms().jsState(), forms::JsState::Disabled);
        QVERIFY(!doc->session().javaScriptEnabled());
        ot::ui::MessageBar* bar = doc->messageStack().bar(MessageKind::JavaScriptOff);
        QVERIFY(bar != nullptr);
        QCOMPARE(bar->objectName(), QStringLiteral("jsBar"));
        // Без JavaScript формат не применяется и сумма не считается.
        QVERIFY(
            forms::setValue(doc->session(), QStringLiteral("a"), QStringLiteral("1234.5")).result().accepted);
        QCOMPARE(forms::formatted(doc->session(), QStringLiteral("a")).result(), QStringLiteral("1234.5"));
        QCOMPARE(QString::fromStdString(current(*doc, "total").value), QStringLiteral("2000"));
        // «Включить для этого файла».
        auto* enable = bar->findChild<QPushButton*>();
        QVERIFY(enable != nullptr);
        QCOMPARE(enable->text(), QStringLiteral("Включить для этого файла"));
        enable->click();
        QTRY_VERIFY(doc->session().javaScriptEnabled());
        QCOMPARE(doc->forms().jsState(), forms::JsState::Enabled);
        QVERIFY(doc->messageStack().bar(MessageKind::JavaScriptOff) == nullptr);
        QVERIFY(ot::js_policy::enabledForFile(fixture("forms-all.pdf"), ot::StateDb::shared()));
        QCOMPARE(forms::formatted(doc->session(), QStringLiteral("a")).result(), QStringLiteral("1,234.50"));
        QVERIFY(
            forms::setValue(doc->session(), QStringLiteral("b"), QStringLiteral("100")).result().accepted);
        QCOMPARE(QString::fromStdString(current(*doc, "total").value), QStringLiteral("1334.5"));
    }

    void textEditorCommitsOnEnter() {
        OT_FEATURE("forms-01");
        OT_FEATURE("forms-03");
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, fixture("forms-all.pdf"));
        QVERIFY(doc != nullptr);
        PageView& v = doc->view();
        const forms::Widget name = field(*doc, "name")->widgets.front();
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, centerOf(*doc, name));
        FieldEditor* editor = doc->forms().editor();
        QVERIFY(editor != nullptr);
        QCOMPARE(editor->kind(), FieldEditor::Kind::Line);
        auto* line = qobject_cast<QLineEdit*>(editor->input());
        QVERIFY(line != nullptr);
        QCOMPARE(line->text(), QStringLiteral("Ivan Petrov"));
        QVERIFY(editor->geometry().intersects(
            v.pageTransform(0)
                .mapRect(QRectF(QPointF(name.rect.x0, name.rect.y0), QPointF(name.rect.x1, name.rect.y1)))
                .toRect()));
        QSignalSpy committed(&doc->forms(), &FormFillTool::committed);
        line->setText(QStringLiteral("Привет"));  // кириллица — setText, не keyClicks
        QTest::keyClick(line, Qt::Key_Return);
        QTRY_COMPARE(committed.count(), 1);
        QCOMPARE(committed.at(0).at(1).toBool(), true);
        QVERIFY(doc->forms().editor() == nullptr);
        QCOMPARE(QString::fromStdString(current(*doc, "name").value), QStringLiteral("Привет"));
        QTRY_COMPARE(doc->session().undoState().undoName, QStringLiteral("Fill field"));
        // Esc бросает правку.
        doc->forms().activate(name.page, name.objnum);
        line = qobject_cast<QLineEdit*>(doc->forms().editor()->input());
        line->setText(QStringLiteral("брошено"));
        QTest::keyClick(line, Qt::Key_Escape);
        QVERIFY(doc->forms().editor() == nullptr);
        QCOMPARE(QString::fromStdString(current(*doc, "name").value), QStringLiteral("Привет"));
    }

    void checkboxAndRadioByClick() {
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, fixture("forms-all.pdf"));
        QVERIFY(doc != nullptr);
        PageView& v = doc->view();
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {},
                          centerOf(*doc, field(*doc, "agree")->widgets.front()));
        QTRY_COMPARE(QString::fromStdString(current(*doc, "agree").value), QStringLiteral("Off"));
        QTRY_VERIFY(doc->forms().isLoaded() && !field(*doc, "agree")->checked());
        QTest::mouseClick(v.viewport(), Qt::LeftButton, {}, centerOf(*doc, field(*doc, "color")->widgets[2]));
        QTRY_COMPARE(QString::fromStdString(current(*doc, "color").value), QStringLiteral("blue"));
        // Стрелка в группе — соседний переключатель.
        QTRY_VERIFY(doc->forms().focused().has_value());
        QTest::keyClick(&v, Qt::Key_Up);
        QTRY_COMPARE(QString::fromStdString(current(*doc, "color").value), QStringLiteral("green"));
    }

    void comboOpensAndListSelects() {
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, fixture("forms-all.pdf"));
        QVERIFY(doc != nullptr);
        const forms::Widget city = field(*doc, "city")->widgets.front();
        QTest::mouseClick(doc->view().viewport(), Qt::LeftButton, {}, centerOf(*doc, city));
        FieldEditor* editor = doc->forms().editor();
        QVERIFY(editor != nullptr);
        QCOMPARE(editor->kind(), FieldEditor::Kind::Combo);
        auto* combo = qobject_cast<QComboBox*>(editor->input());
        QVERIFY(combo != nullptr);
        QCOMPARE(combo->count(), 3);
        QCOMPARE(combo->currentText(), QStringLiteral("Kazan"));
        QTRY_VERIFY(combo->view()->isVisible());
        combo->hidePopup();
        combo->setCurrentIndex(2);
        emit combo->activated(2);
        QTRY_COMPARE(QString::fromStdString(current(*doc, "city").value), QStringLiteral("Sochi"));
        // Список с мультивыбором.
        doc->forms().activate(0, field(*doc, "langs")->widgets.front().objnum);
        editor = doc->forms().editor();
        QVERIFY(editor != nullptr);
        QCOMPARE(editor->kind(), FieldEditor::Kind::List);
        auto* list = qobject_cast<QListWidget*>(editor->input());
        QCOMPARE(list->selectedItems().size(), 2);
        list->item(0)->setSelected(false);
        list->item(1)->setSelected(true);
        doc->forms().commitEditor();
        QTRY_COMPARE(current(*doc, "langs").values, (std::vector<std::string>{"en", "de"}));
    }

    void dateCalendar() {
        OT_FEATURE("forms-15");
        QCOMPARE(ot::ui::qtDateFormat(QStringLiteral("dd.mm.yyyy")), QStringLiteral("dd'.'MM'.'yyyy"));
        QCOMPARE(ot::ui::qtDateFormat(QStringLiteral("m/d/yy h:MM tt")),
                 QStringLiteral("M'/'d'/'yy' 'h':'mm' 'AP"));
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, fixture("forms-all.pdf"));
        QVERIFY(doc != nullptr);
        QVERIFY(doc->forms().focusField(QStringLiteral("date")));
        FieldEditor* editor = doc->forms().editor();
        QVERIFY(editor != nullptr);
        QCOMPARE(editor->kind(), FieldEditor::Kind::Date);
        QVERIFY(editor->calendarButton() != nullptr);
        editor->openPopup();
        editor->pickDate(QDate(2026, 3, 8));
        QTRY_COMPARE(QString::fromStdString(current(*doc, "date").value), QStringLiteral("08.03.2026"));
    }

    void tabFollowsRows() {
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, fixture("forms-all.pdf"));
        QVERIFY(doc != nullptr);
        FormFillTool& f = doc->forms();
        QVERIFY(f.focusField(QStringLiteral("name")));
        QStringList seen{f.focusedField()};
        for (int i = 0; i < 12; ++i) {
            if (FieldEditor* e = f.editor()) {
                QTest::keyClick(e->input(), Qt::Key_Tab);
            } else {
                QTest::keyClick(&doc->view(), Qt::Key_Tab);
            }
            QTRY_VERIFY(f.focusedField() != seen.last() || f.focusedField().isEmpty());
            seen << f.focusedField();
        }
        // /Tabs /R: «Языки» (ряд ниже «Города») раньше «Страны», хотя в /Annots наоборот; «Итого» — только
        // для чтения.
        QCOMPARE(seen, (QStringList{QStringLiteral("name"), QStringLiteral("comment"),
                                    QStringLiteral("comment_ru"), QStringLiteral("date"), QStringLiteral("a"),
                                    QStringLiteral("b"), QStringLiteral("secret"), QStringLiteral("pin"),
                                    QStringLiteral("agree"), QStringLiteral("color"), QStringLiteral("city"),
                                    QStringLiteral("langs"), QStringLiteral("reset")}));
    }

    void validateAlertAndRejectedFrame() {
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, fixture("forms-all.pdf"));
        QVERIFY(doc != nullptr);
        QStringList alerts;
        doc->forms().setAlertPrompt([&alerts](const forms::AlertRequest& r) {
            alerts << QString::fromStdString(r.message);
            return forms::AlertReply{};
        });
        doc->forms().enableJavaScriptForFile();
        QTRY_VERIFY(doc->session().javaScriptEnabled());
        QVERIFY(doc->forms().focusField(QStringLiteral("qty")));
        auto* line = qobject_cast<QLineEdit*>(doc->forms().editor()->input());
        QSignalSpy committed(&doc->forms(), &FormFillTool::committed);
        QTest::keyClicks(line, QStringLiteral("abc"));
        line->setText(QStringLiteral("abc"));
        QTest::keyClick(line, Qt::Key_Return);
        QTRY_COMPARE(committed.count(), 1);
        QCOMPARE(committed.at(0).at(1).toBool(), false);
        QCOMPARE(alerts.size(), 1);
        QVERIFY2(alerts.front().contains(QStringLiteral("abc")), qPrintable(alerts.front()));
        QVERIFY(doc->forms().rejected().count(QStringLiteral("qty")) == 1);
        QCOMPARE(QString::fromStdString(current(*doc, "qty").value), QStringLiteral("3"));
        // Кнопка JavaScript — app.alert.
        QTest::mouseClick(doc->view().viewport(), Qt::LeftButton, {},
                          centerOf(*doc, field(*doc, "hello")->widgets.front()));
        QTRY_COMPARE(alerts.size(), 2);
        QCOMPARE(alerts.last(), QStringLiteral("Привет из формы «Оттиска»"));
    }

    void buttonsResetSubmitAndSignature() {
        OT_FEATURE("forms-06");
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, fixture("forms-all.pdf"));
        QVERIFY(doc != nullptr);
        QVERIFY(forms::setValue(doc->session(), QStringLiteral("name"), QStringLiteral("Мария"))
                    .result()
                    .accepted);
        QTRY_VERIFY(doc->forms().isLoaded());
        QTest::mouseClick(doc->view().viewport(), Qt::LeftButton, {},
                          centerOf(*doc, field(*doc, "reset")->widgets.front()));
        QTRY_COMPARE(QString::fromStdString(current(*doc, "name").value), QStringLiteral("Ivan Petrov"));
        doc->forms().setSubmitConfirm([](const forms::SubmitPlan&) { return false; });  // без окна: только сигнал
        QSignalSpy submit(&doc->forms(), &FormFillTool::submitRequested);
        QTest::mouseClick(doc->view().viewport(), Qt::LeftButton, {},
                          centerOf(*doc, field(*doc, "submit_http")->widgets.front()));
        QTRY_COMPARE(submit.count(), 1);
        const auto action = submit.at(0).at(0).value<forms::FieldAction>();
        QCOMPARE(QString::fromStdString(action.target), QStringLiteral("https://example.org/ottisk/submit"));
        QSignalSpy sign(&doc->forms(), &FormFillTool::signatureFieldClicked);
        doc->forms().activate(0, field(*doc, "sig")->widgets.front().objnum);
        QCOMPARE(sign.count(), 1);
        QCOMPARE(sign.at(0).at(0).toString(), QStringLiteral("sig"));
        // «Очистить форму».
        QVERIFY(forms::setValue(doc->session(), QStringLiteral("comment"), QStringLiteral("x"))
                    .result()
                    .accepted);
        doc->forms().resetForm();
        QTRY_COMPARE(QString::fromStdString(current(*doc, "comment").value), QString());
    }

    void exportAndImportData() {
        OT_FEATURE("forms-35");
        OT_FEATURE("forms-36");
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, fixture("forms-all.pdf"));
        QVERIFY(doc != nullptr);
        QVERIFY(forms::setValue(doc->session(), QStringLiteral("name"), QStringLiteral("Анна"))
                    .result()
                    .accepted);
        QVERIFY(forms::setValue(doc->session(), QStringLiteral("agree"), QStringLiteral("Off"))
                    .result()
                    .accepted);
        const QString xfdf = QDir(dataDir()).filePath(QStringLiteral("данные.xfdf"));
        QFile::remove(xfdf);
        QSignalSpy exported(&doc->forms(), &FormFillTool::dataExported);
        doc->forms().exportDataTo(xfdf, forms::DataFormat::Xfdf);
        QTRY_COMPARE(exported.count(), 1);
        QVERIFY(exported.at(0).at(1).toBool());
        QVERIFY(QFile::exists(xfdf));
        // Чистая копия: импорт с подтверждением (имя файла и число значений).
        MainWindow w2;
        w2.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* other = openDoc(w2, fixture("forms-all.pdf"));
        QVERIFY(other != nullptr);
        QString asked;
        int askedValues = 0;
        other->forms().setImportConfirm([&](const QString& file, int values) {
            asked = file;
            askedValues = values;
            return true;
        });
        other->forms().setReportWindow(false);
        QSignalSpy imported(&other->forms(), &FormFillTool::dataImported);
        other->forms().importDataFrom(xfdf);
        QTRY_COMPARE(imported.count(), 1);
        QCOMPARE(asked, xfdf);
        QVERIFY(askedValues > 10);
        QCOMPARE(QString::fromStdString(current(*other, "name").value), QStringLiteral("Анна"));
        QCOMPARE(QString::fromStdString(current(*other, "agree").value), QStringLiteral("Off"));
        QTRY_COMPARE(other->session().undoState().undoName, QStringLiteral("Import form data"));
        // Отказ подтверждения — ничего не меняется; чужой FDF — отказ с причиной.
        other->forms().setImportConfirm([](const QString&, int) { return false; });
        other->forms().importDataFrom(xfdf);
        QCOMPARE(imported.count(), 1);
        const QString foreign = QDir(dataDir()).filePath(QStringLiteral("чужой.fdf"));
        {
            QFile f(foreign);
            QVERIFY(f.open(QIODevice::WriteOnly));
            const forms::FormData data{{forms::FieldValue{"name", {"x"}, false}}, "другой.pdf", {}};
            const std::string bytes = forms::writeData(data, forms::DataFormat::Fdf);
            f.write(bytes.data(), static_cast<qint64>(bytes.size()));
        }
        other->forms().setImportConfirm([](const QString&, int) { return true; });
        QSignalSpy failed(&other->forms(), &FormFillTool::dataImportFailed);
        other->forms().importDataFrom(foreign);
        QTRY_COMPARE(failed.count(), 1);
        QVERIFY2(failed.at(0).at(1).toString().contains(QStringLiteral("другой.pdf")),
                 qPrintable(failed.at(0).at(1).toString()));
        QCOMPARE(QString::fromStdString(current(*other, "name").value), QStringLiteral("Анна"));
    }

    void xfaBar() {
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, fixture("xfa-stub.pdf"));
        QVERIFY(doc != nullptr);
        ot::ui::MessageBar* bar = doc->messageStack().bar(MessageKind::Xfa);
        QVERIFY(bar != nullptr);
        QCOMPARE(bar->objectName(), QStringLiteral("xfaBar"));
    }

    void saveIncrementalAndScript() {
        OT_FEATURE("forms-05");
        const QString file = QDir(dataDir()).filePath(QStringLiteral("анкета.pdf"));
        QFile::remove(file);
        QVERIFY(QFile::copy(fixture("forms-all.pdf"), file));
        MainWindow w;
        w.setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        DocumentView* doc = openDoc(w, file);
        QVERIFY(doc != nullptr);
        ot::ScriptRunner runner;
        ot::ui::registerBaseActions(runner, w);
        ot::ScriptContext ctx;
        const ot::ScriptResult r = runner.run(QJsonDocument::fromJson(R"([
            {"cmd": "form.setValue", "args": {"name": "comment_ru", "value": "Новое значение"}},
            {"cmd": "form.setValue", "args": {"name": "agree", "value": false}},
            {"cmd": "form.setValue", "args": {"name": "langs", "value": ["en"]}},
            {"cmd": "doc.save", "args": {"mode": "incremental"}}])"),
                                              ctx);
        QVERIFY2(r.ok, qPrintable(r.describe()));
        const forms::Field ru = current(*doc, "comment_ru");
        QCOMPARE(QString::fromStdString(ru.value), QStringLiteral("Новое значение"));
        QCOMPARE(QString::fromStdString(current(*doc, "agree").value), QStringLiteral("Off"));
        // Отказ — ошибка шага с именем поля.
        const ot::ScriptResult bad = runner.run(
            QJsonDocument::fromJson(R"([{"cmd": "form.setValue", "args": {"name": "total", "value": "1"}}])"),
            ctx);
        QVERIFY(!bad.ok);
        QVERIFY2(bad.describe().contains(QStringLiteral("read-only")), qPrintable(bad.describe()));
    }
};

OT_UI_TEST(FormFillTest, "form_fill");

#include "form_fill_test.moc"
