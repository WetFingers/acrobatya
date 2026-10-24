// ui.print_dialog — диалог «Печать» (план 2.6; эталон 3.3, 6.3; view-37, view-42, view-43, create-16): Ctrl+P
// открывает диалог с демонстрационными принтерами; диапазон «1-2» с логическими номерами, неверный диапазон и
// 0 страниц — «Печать» серая; буклет на принтере без дуплекса — «Только лицевая сторона» и подсказка; печать
// в PDF через фабрику устройств (QPdfWriter) — MuPDF читает 3 листа n-up 4; «Microsoft Print to PDF» — файл
// по «Установки → Печать» без вопроса; выделенные миниатюры — «Выделенные страницы» и «2-3, 6»; предпросмотр
// — растр листа и листание; «Комментарии и формы», серый и «как изображение» — в параметрах задания; без
// права печати — команда серая и диалог не открывается; инструмент «Печать» из «Все инструменты» открывает
// диалог; страница Установок «Печать» — чтение и запись.
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <filesystem>
#include <memory>

#include "core/doc/document.h"
#include "core/doc/saver.h"
#include "core/mu/context.h"
#include "core/util/features.h"
#include "services/app_info.h"
#include "services/commands.h"
#include "services/print_job.h"
#include "services/settings.h"
#include "services/settings_keys.h"
#include "support/ui_test.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shell/preferences/preferences_dialog.h"
#include "ui/shell/preferences/print_page.h"
#include "ui/tools/print/print_dialog.h"
#include "ui/tools/print/print_preview.h"
#include "ui/tools/print/print_runner.h"
#include "ui/tools/tool_registry.h"
#include "ui/widgets/message_bar.h"
#include "ui/widgets/segmented_bar.h"
#include "ui/widgets/tool_button.h"

using ot::ui::DocumentView;
using ot::ui::MainWindow;
using ot::ui::PrintDialog;

namespace {

QString source(const char* rel) { return QStringLiteral(OTTISK_SOURCE_DIR "/tests/") + QLatin1String(rel); }

int pdfPages(const QString& path) {
    auto doc = ot::Document::openOrThrow(std::filesystem::path(path.toStdWString()));
    return doc->pageCount();
}

// Устройства печати — PDF-файлы в каталоге dir (имя — принтер-N.pdf), выход «Печать в PDF» — заданный файл.
struct PdfPrinters {
    QString dir;
    QStringList outputs;
    QStringList
        files;  // последний — устройство задания (диалог спрашивает бумагу и дуплекс у новых устройств)
    std::unique_ptr<ot::ScopedPrintDevices> scope;
    int n = 0;

    explicit PdfPrinters(const QString& d) : dir(d) {
        scope = std::make_unique<ot::ScopedPrintDevices>(
            QStringList{QStringLiteral("Office"), QStringLiteral("Microsoft Print to PDF")},
            [this](const QString& printer, const QString& output) -> std::unique_ptr<ot::PrintDevice> {
                outputs << output;
                const QString file =
                    output.isEmpty()
                        ? QDir(dir).filePath(printer + QString::number(++n) + QStringLiteral(".pdf"))
                        : output;
                files << file;
                return ot::makePdfDevice(file, {595.2756f, 841.8898f}, 72);
            });
    }
};

}  // namespace

class PrintDialogTest : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<MainWindow> window_;

    MainWindow& open(const QString& file) {
        window_ = std::make_unique<MainWindow>();
        window_->setCloseConfirm([](DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        window_->resize(1440, 900);
        window_->show();
        window_->openFile(file);
        [&] {
            QTRY_VERIFY(window_->currentDocument() != nullptr &&
                        window_->currentDocument()->session().isOpen());
        }();
        return *window_;
    }

private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-print_dialog"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
    }

    void cleanup() {
        window_.reset();
        QFile::remove(ot::app::settingsPath());
    }

    void ctrlPOpensDialogWithDefaults() {
        OT_FEATURE("view-37");
        MainWindow& w = open(source("fixtures/demo/report.pdf"));
        QVERIFY(w.commands().blockedReason(QStringLiteral("doc.print")).isEmpty());
        QVERIFY(w.commands().trigger(QStringLiteral("doc.print")));
        PrintDialog* d = w.printDialog();
        QVERIFY(d != nullptr);
        QCOMPARE(d->printer(), QStringLiteral("HP LaserJet Pro M404"));
        QVERIFY(d->allButton()->isChecked());
        QVERIFY(d->fitButton()->isChecked());
        QCOMPARE(static_cast<int>(d->plan().sheets.size()), 12);
        QVERIFY(d->printButton()->isEnabled());
        QVERIFY(!d->summaryButton()->isEnabled());   // сводка комментариев — 0.9
        QVERIFY(!d->selectedButton()->isEnabled());  // миниатюры не выделены
        // Предпросмотр: растр листа A4 при 96 dpi и листание.
        QTRY_VERIFY(d->preview()->isReady());
        QCOMPARE(d->preview()->image().size(), QSize(794, 1123));
        QCOMPARE(d->preview()->sheetCount(), 12);
        QVERIFY(d->preview()->nextButton()->isEnabled());
        d->preview()->next();
        QCOMPARE(d->preview()->currentSheet(), 1);
        QTRY_VERIFY(d->preview()->isReady());
    }

    void rangeLabelsAndNothingToPrint() {
        OT_FEATURE("view-37");
        MainWindow& w = open(source("fixtures/demo/report.pdf"));
        PrintDialog* d = w.showPrintDialog();
        QVERIFY(d != nullptr);
        // report.pdf: i, ii, 1…10 — «1-2» логические: физические 3 и 4.
        d->rangeEdit()->setText(QStringLiteral("1-2"));
        QVERIFY(d->rangeButton()->isChecked());
        QCOMPARE(d->plan().pages, (std::vector<int>{2, 3}));
        QVERIFY(d->printButton()->isEnabled());
        d->rangeEdit()->setText(QStringLiteral("abc"));
        QCOMPARE(d->plan().status, ot::PrintStatus::RangeError);
        QVERIFY(d->rangeError()->isVisibleTo(d));
        QVERIFY(!d->printButton()->isEnabled());
        // «1» (физическая 3, нечётная) и «только чётные» — печатать нечего.
        d->rangeEdit()->setText(QStringLiteral("1"));
        d->subsetBox()->setCurrentIndex(2);
        QCOMPARE(d->plan().status, ot::PrintStatus::NothingToPrint);
        QVERIFY(!d->printButton()->isEnabled());
        QVERIFY(!d->rangeError()->isVisibleTo(d));
        d->subsetBox()->setCurrentIndex(1);
        QVERIFY(d->printButton()->isEnabled());
        // Текущая страница.
        d->currentButton()->setChecked(true);
        QCOMPARE(d->plan().pages, (std::vector<int>{0}));
    }

    void bookletWithoutDuplex() {
        OT_FEATURE("view-40");
        MainWindow& w = open(source("fixtures/demo/report.pdf"));
        PrintDialog* d = w.showPrintDialog({}, QStringLiteral("Microsoft Print to PDF"));
        QVERIFY(d != nullptr);
        QCOMPARE(d->printer(), QStringLiteral("Microsoft Print to PDF"));
        d->setLayoutMode(ot::print::Layout::Booklet);
        QVERIFY(d->bookletHint()->isVisibleTo(d));
        QCOMPARE(d->bookletSubsetBox()->currentIndex(), 1);  // «Только лицевая сторона»
        QCOMPARE(d->plan().sheets.size(), 3u);
        QVERIFY(!d->duplexBox()->isEnabled());
        // Принтер с дуплексом — подсказки нет, «Обе стороны» доступны.
        d->setPrinter(QStringLiteral("HP LaserJet Pro M404"));
        QVERIFY(!d->bookletHint()->isVisibleTo(d));
        d->bookletSubsetBox()->setCurrentIndex(0);
        QCOMPARE(d->plan().sheets.size(), 6u);
        QCOMPARE(d->plan().duplex, ot::DuplexMode::ShortSide);
    }

    void printToPdfDevice() {
        OT_FEATURE("view-39");
        QTemporaryDir tmp;
        PdfPrinters printers(tmp.path());
        MainWindow& w = open(source("fixtures/demo/report.pdf"));
        PrintDialog* d = w.showPrintDialog();
        QVERIFY(d != nullptr);
        QCOMPARE(d->printer(), QStringLiteral("Office"));
        d->setLayoutMode(ot::print::Layout::Multiple);
        d->bordersBox()->setChecked(true);
        d->grayBox()->setChecked(true);
        d->contentBox()->setCurrentIndex(1);  // «Документ»
        const ot::PrintSettings s = d->settings();
        QCOMPARE(s.content, ot::print::PrintContent::Document);
        QVERIFY(s.grayscale);
        QCOMPARE(s.layout.cols * s.layout.rows, 4);
        QPointer<ot::ui::PrintRunner> runner = d->print();
        QVERIFY(runner != nullptr);
        QSignalSpy finished(runner.data(), &ot::ui::PrintRunner::finished);
        runner->setNotify(false);
        QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, 20000);
        QCOMPARE(finished.at(0).at(0).value<ot::PrintStatus>(), ot::PrintStatus::Done);
        QTRY_VERIFY(w.printDialog() == nullptr);  // диалог закрыт и удалён
        QCOMPARE(pdfPages(printers.files.last()), 3);
    }

    void printToPdfUsesPreferences() {
        OT_FEATURE("create-16");
        QTemporaryDir tmp;
        {
            ot::Settings st;
            st.set(ot::settings::kPrintPdfFolder, tmp.path());
            st.set(ot::settings::kPrintPdfName, QStringLiteral("{name} печать"));
            st.set(ot::settings::kPrintPdfAsk, false);
        }
        PdfPrinters printers(tmp.path());
        MainWindow& w = open(source("fixtures/demo/report.pdf"));
        PrintDialog* d = w.showPrintDialog({}, QStringLiteral("Microsoft Print to PDF"));
        QVERIFY(d != nullptr);
        d->rangeEdit()->setText(QStringLiteral("i-ii"));
        QPointer<ot::ui::PrintRunner> runner = d->print();
        QVERIFY(runner != nullptr);
        runner->setNotify(false);
        QSignalSpy finished(runner.data(), &ot::ui::PrintRunner::finished);
        QTRY_VERIFY_WITH_TIMEOUT(finished.count() == 1, 20000);
        const QString expected = QDir(tmp.path()).filePath(QStringLiteral("report печать.pdf"));
        QVERIFY(printers.outputs.contains(expected));
        QCOMPARE(pdfPages(expected), 2);
        // Второй раз — не поверх: «report печать (2).pdf».
        d = w.showPrintDialog({}, QStringLiteral("Microsoft Print to PDF"));
        d->currentButton()->setChecked(true);
        runner = d->print();
        QVERIFY(runner != nullptr);
        runner->setNotify(false);
        QSignalSpy again(runner.data(), &ot::ui::PrintRunner::finished);
        QTRY_VERIFY_WITH_TIMEOUT(again.count() == 1, 20000);
        QVERIFY(QFileInfo::exists(QDir(tmp.path()).filePath(QStringLiteral("report печать (2).pdf"))));
    }

    void selectedThumbnailsBecomeSelectedPages() {
        OT_FEATURE("view-10");
        MainWindow& w = open(source("fixtures/demo/report.pdf"));
        w.currentDocument()->setSelectedPages({1, 2, 5});
        PrintDialog* d = w.showPrintDialog();
        QVERIFY(d != nullptr);
        QVERIFY(d->selectedButton()->isEnabled());
        QVERIFY(d->selectedButton()->isChecked());
        // В поле «Страницы» — логические номера (i, ii, 1…): «ii-1, 4»; «Страницы» дают то же.
        QCOMPARE(d->rangeEdit()->text(), QStringLiteral("ii-1, 4"));
        QCOMPARE(d->plan().pages, (std::vector<int>{1, 2, 5}));
        d->rangeButton()->setChecked(true);
        QCOMPARE(d->plan().pages, (std::vector<int>{1, 2, 5}));
        // Заранее выбранные страницы (закладка) — важнее выделения миниатюр.
        d = w.showPrintDialog({7, 8});
        QCOMPARE(d->plan().pages, (std::vector<int>{7, 8}));
    }

    void imageDpiAndContentModes() {
        OT_FEATURE("view-43");
        OT_FEATURE("view-42");
        MainWindow& w = open(source("fixtures/demo/comments.pdf"));
        PrintDialog* d = w.showPrintDialog();
        QVERIFY(d != nullptr);
        QVERIFY(!d->imageDpiBox()->isEnabled());
        d->asImageBox()->setChecked(true);
        QVERIFY(d->imageDpiBox()->isEnabled());
        d->imageDpiBox()->setCurrentIndex(0);
        ot::PrintSettings s = d->settings();
        QVERIFY(s.asImage);
        QCOMPARE(s.imageDpi, 150);
        QCOMPARE(s.content, ot::print::PrintContent::DocumentAndMarkups);
        d->contentBox()->setCurrentIndex(2);
        QCOMPARE(d->settings().content, ot::print::PrintContent::FormFieldsOnly);
        d->copiesBox()->setValue(3);
        QVERIFY(d->collateBox()->isEnabled());
        QCOMPARE(d->settings().copies, 3);
    }

    void printDeniedCommandBlocked() {
        OT_FEATURE("view-37");
        QTemporaryDir tmp;
        const QString file = QDir(tmp.path()).filePath(QStringLiteral("noprint.pdf"));
        {
            auto doc = ot::Document::openOrThrowMemory(ot::Document::createBlank(2)->savePdfToMemory());
            ot::SaveOptions o;
            o.mode = ot::SaveMode::Full;
            o.encryption =
                ot::SaveEncryption{ot::SaveEncryption::Method::Aes256, "", "owner", -4 & ~(1 << 2)};
            QVERIFY(ot::Saver::save(*doc, std::filesystem::path(file.toStdWString()), o).ok());
        }
        MainWindow& w = open(file);
        QVERIFY(!w.currentDocument()->permissions().print);
        QVERIFY(!w.commands().blockedReason(QStringLiteral("doc.print")).isEmpty());
        QVERIFY(w.showPrintDialog() == nullptr);
    }

    void printToolOpensDialog() {
        MainWindow& w = open(source("fixtures/demo/report.pdf"));
        QVERIFY(ot::ui::ToolRegistry::instance().activate(QStringLiteral("print"), w.currentDocument()));
        QVERIFY(w.printDialog() != nullptr);
        w.printDialog()->close();
        ot::ui::ToolRegistry::instance().deactivate();
    }

    void preferencesPrintRoundTrip() {
        OT_FEATURE("create-16");
        MainWindow& w = open(source("fixtures/demo/report.pdf"));
        ot::ui::PreferencesDialog* prefs = w.showPreferences(QStringLiteral("print"));
        auto* page = qobject_cast<ot::ui::PrintPage*>(prefs->page(QStringLiteral("print")));
        QVERIFY(page != nullptr);
        page->load(ot::Settings());
        QCOMPARE(page->qualityBox()->currentData().toInt(), 300);
        QVERIFY(page->askBox()->isChecked());
        page->qualityBox()->setCurrentIndex(page->qualityBox()->findData(600));
        page->scaleBox()->setCurrentIndex(page->scaleBox()->findData(QStringLiteral("actual")));
        page->printerBox()->setCurrentIndex(
            page->printerBox()->findData(QStringLiteral("Microsoft Print to PDF")));
        page->nameEdit()->setText(QStringLiteral("{name}-копия"));
        page->askBox()->setChecked(false);
        ot::Settings st;
        page->apply(st);
        QCOMPARE(st.get(ot::settings::kPrintQuality), 600);
        QCOMPARE(st.get(ot::settings::kPrintScale), QStringLiteral("actual"));
        QCOMPARE(st.get(ot::settings::kPrintPrinter), QStringLiteral("Microsoft Print to PDF"));
        QCOMPARE(st.get(ot::settings::kPrintPdfName), QStringLiteral("{name}-копия"));
        QVERIFY(!st.get(ot::settings::kPrintPdfAsk));
        prefs->close();
        // Новый диалог — принтер и масштаб из Установок.
        PrintDialog* d = w.showPrintDialog();
        QCOMPARE(d->printer(), QStringLiteral("Microsoft Print to PDF"));
        QVERIFY(d->actualButton()->isChecked());
    }
};

OT_UI_TEST(PrintDialogTest, "print_dialog");

#include "print_dialog_test.moc"
