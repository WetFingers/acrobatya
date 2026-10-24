// ui.print_cli — печать из командной строки и Проводника (план 2.6; эталон 3.3 «Командная строка», 10.3
// ProgID shell\print, shell\printto; view-37, view-10, view-11): разбор `/p файл` и `/pt файл принтер драйвер
// порт`;
// `/pt` без окна создаёт задание на фейковом устройстве (ScopedPrintDevices), код 0; нет файла и документ без
// права печати — код 1; AcroBatya.exe `/pt` под OTTISK_FAKE_PRINTER — PDF задания и строка jobs.jsonl, код 0;
// запрос печати уже открытому экземпляру — печать без вкладки; «Печать страниц…» миниатюр — диалог с
// выделенными страницами; страницы закладки — до следующей закладки того же уровня; --self-check — строка
// ProgID печати.
#include <QDir>
#include <QFile>
#include <QListView>
#include <QProcess>
#include <QRadioButton>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTemporaryDir>
#include <QTreeView>
#include <QtConcurrent/QtConcurrentRun>
#include <QtTest/QtTest>

#include <filesystem>
#include <memory>

#include "core/doc/document.h"
#include "core/doc/saver.h"
#include "services/app_info.h"
#include "services/print_job.h"
#include "services/single_instance.h"
#include "support/ui_test.h"
#include "ui/app/cli.h"
#include "ui/panes/bookmarks_pane.h"
#include "ui/panes/thumbnails_pane.h"
#include "ui/shell/document_tabs.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shell/nav_pane.h"
#include "ui/tools/print/print_dialog.h"

using ot::ui::CliOptions;
using ot::ui::MainWindow;
using ot::ui::parseCli;

namespace {

QString source(const char* rel) { return QStringLiteral(OTTISK_SOURCE_DIR "/tests/") + QLatin1String(rel); }

QString exePath() {
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("AcroBatya.exe"));
}

int pdfPages(const QString& path) {
    return ot::Document::openOrThrow(std::filesystem::path(path.toStdWString()))->pageCount();
}

// Устройство задания — тестовое FakePrintDevice (листы в памяти, живут после задания).
class ForwardDevice : public ot::PrintDevice {
public:
    explicit ForwardDevice(ot::FakePrintDevice& target) : t_(target) {}
    QString name() const override { return t_.name(); }
    ot::print::Paper paper() const override { return t_.paper(); }
    int resolution() const override { return t_.resolution(); }
    bool begin(const ot::PrintDeviceOptions& o, bool landscape) override { return t_.begin(o, landscape); }
    bool newSheet(bool landscape) override { return t_.newSheet(landscape); }
    void drawImage(const QRectF& r, const QImage& i) override { t_.drawImage(r, i); }
    void drawLine(const QPointF& a, const QPointF& b, qreal w) override { t_.drawLine(a, b, w); }
    void drawFrame(const QRectF& r, qreal w) override { t_.drawFrame(r, w); }
    void drawLabel(const QRectF& r, const QString& s, qreal p) override { t_.drawLabel(r, s, p); }
    bool end() override { return t_.end(); }
    void abort() override { t_.abort(); }

private:
    ot::FakePrintDevice& t_;
};

struct FakePrinters {
    ot::FakePrintDevice device{QStringLiteral("Fake"), 18};
    QStringList opened;
    ot::ScopedPrintDevices scope{
        {QStringLiteral("Office"), QStringLiteral("Home")},
        [this](const QString& printer, const QString&) -> std::unique_ptr<ot::PrintDevice> {
            opened << printer;
            return std::make_unique<ForwardDevice>(device);
        }};
};

}  // namespace

class PrintCliTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() {
        QDir data(QStringLiteral(OTTISK_BINARY_DIR "/test-data/ui-print_cli"));
        data.removeRecursively();
        QVERIFY(QDir().mkpath(data.path()));
        qputenv("OTTISK_DATA_DIR", data.path().toUtf8());
        // Свой экземпляр: запрос печати не уйдёт окну человека или соседнему набору.
        qputenv("OTTISK_INSTANCE",
                QByteArray("print-cli-").append(QByteArray::number(QCoreApplication::applicationPid())));
    }

    void parsesPrintArguments() {
        CliOptions o =
            parseCli({QStringLiteral("AcroBatya.exe"), QStringLiteral("/p"), QStringLiteral("a.pdf")});
        QVERIFY(o.error.isEmpty());
        QVERIFY(o.print);
        QCOMPARE(o.printFile, QStringLiteral("a.pdf"));
        QVERIFY(o.printer.isEmpty());
        QVERIFY(o.files.isEmpty());
        o = parseCli({QStringLiteral("AcroBatya.exe"), QStringLiteral("/PT"), QStringLiteral("a.pdf"),
                      QStringLiteral("HP LaserJet"), QStringLiteral("winspool"), QStringLiteral("Ne01:")});
        QVERIFY(o.error.isEmpty());
        QCOMPARE(o.printer, QStringLiteral("HP LaserJet"));
        QVERIFY(o.files.isEmpty());  // драйвер и порт Проводника — не файлы
        o = parseCli({QStringLiteral("AcroBatya.exe"), QStringLiteral("/pt"), QStringLiteral("a.pdf"),
                      QStringLiteral("HP"), QStringLiteral("--log"), QStringLiteral("info")});
        QVERIFY(o.error.isEmpty());
        QCOMPARE(o.logLevel, QStringLiteral("info"));
        o = parseCli({QStringLiteral("AcroBatya.exe"), QStringLiteral("/p")});
        QVERIFY(!o.error.isEmpty());
        // Запрос печати переживает протокол второго экземпляра.
        ot::SingleInstance::Request r;
        r.files = {QStringLiteral("C:/a.pdf")};
        r.print = true;
        r.printer = QStringLiteral("HP");
        ot::SingleInstance::Request back;
        QVERIFY(ot::SingleInstance::decode(ot::SingleInstance::encode(r), &back));
        QCOMPARE(back, r);
    }

    void ptPrintsWithoutWindow() {
        std::printf("OT_FEATURE:view-37\n");
        FakePrinters printers;
        CliOptions o = parseCli({QStringLiteral("AcroBatya.exe"), QStringLiteral("/pt"),
                                 source("fixtures/demo/report.pdf"), QStringLiteral("Home")});
        QCOMPARE(ot::ui::runPrint(o), 0);
        QCOMPARE(printers.opened.value(0), QStringLiteral("Home"));
        QVERIFY(printers.device.ended);
        QCOMPARE(printers.device.sheets.size(), 12);
        QVERIFY(QApplication::topLevelWidgets().isEmpty() ||
                std::none_of(QApplication::topLevelWidgets().begin(), QApplication::topLevelWidgets().end(),
                             [](QWidget* w) { return w->isVisible(); }));
        // Нет файла — код 1.
        o.printFile = source("fixtures/demo/none.pdf");
        QCOMPARE(ot::ui::runPrint(o), 1);
        // Без права печати — код 1, на устройство ничего.
        QTemporaryDir tmp;
        const QString file = QDir(tmp.path()).filePath(QStringLiteral("noprint.pdf"));
        auto doc = ot::Document::createBlank(1);
        ot::SaveOptions so;
        so.mode = ot::SaveMode::Full;
        so.encryption = ot::SaveEncryption{ot::SaveEncryption::Method::Aes256, "", "owner", -4 & ~(1 << 2)};
        QVERIFY(ot::Saver::save(*doc, std::filesystem::path(file.toStdWString()), so).ok());
        FakePrinters denied;
        o.printFile = file;
        QCOMPARE(ot::ui::runPrint(o), 1);
        QVERIFY(!denied.device.begun);
    }

    void exePtUnderFakePrinter() {
        if (!QFileInfo::exists(exePath())) {
            QSKIP("AcroBatya.exe is not built next to ui_tests");
        }
        QTemporaryDir tmp;
        QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
        env.insert(QStringLiteral("OTTISK_FAKE_PRINTER"),
                   QDir(tmp.path()).filePath(QStringLiteral("printed")));
        env.insert(QStringLiteral("OTTISK_DATA_DIR"), QDir(tmp.path()).filePath(QStringLiteral("data")));
        env.insert(QStringLiteral("OTTISK_INSTANCE"), QStringLiteral("print-cli-exe"));
        QProcess p;
        p.setProcessEnvironment(env);
        p.start(exePath(),
                {QStringLiteral("/pt"), source("fixtures/pages/mixed-sizes.pdf"),
                 QStringLiteral("Fake Printer"), QStringLiteral("winspool"), QStringLiteral("Ne00:")});
        QVERIFY(p.waitForFinished(60000));
        QCOMPARE(p.exitCode(), 0);
        const QString printed = QDir(tmp.path()).filePath(QStringLiteral("printed/Fake_Printer-1.pdf"));
        QVERIFY(QFileInfo::exists(printed));
        QCOMPARE(pdfPages(printed), 4);
        QFile log(QDir(tmp.path()).filePath(QStringLiteral("printed/jobs.jsonl")));
        QVERIFY(log.open(QIODevice::ReadOnly));
        QVERIFY(log.readAll().contains("\"printer\":\"Fake Printer\""));
        QProcess missing;
        missing.setProcessEnvironment(env);
        missing.start(exePath(),
                      {QStringLiteral("/p"), QDir(tmp.path()).filePath(QStringLiteral("none.pdf"))});
        QVERIFY(missing.waitForFinished(60000));
        QCOMPARE(missing.exitCode(), 1);
    }

    void runningInstancePrintsWithoutTab() {
        FakePrinters printers;
        MainWindow w;
        w.show();
        ot::SingleInstance instance;
        QVERIFY(instance.listen());
        connect(&instance, &ot::SingleInstance::requestReceived, &w, &MainWindow::handleRequest);
        const CliOptions o = parseCli({QStringLiteral("AcroBatya.exe"), QStringLiteral("/p"),
                                       source("fixtures/pages/mixed-sizes.pdf")});
        // Второй экземпляр — в своём потоке (sendToRunning ждёт ответа, окно отвечает в этом).
        auto code = QtConcurrent::run([o] {
            ot::SingleInstance::Request r;
            r.files = {QFileInfo(o.printFile).absoluteFilePath()};
            r.print = true;
            return ot::SingleInstance::sendToRunning(r);
        });
        QTRY_VERIFY(code.isFinished());
        QVERIFY(code.result());
        QTRY_VERIFY_WITH_TIMEOUT(printers.device.ended, 20000);
        QCOMPARE(printers.device.sheets.size(), 4);
        QCOMPARE(printers.opened.value(0), QStringLiteral("Office"));  // принтер по умолчанию
        QCOMPARE(w.documentCount(), 0);                                // вкладка не открылась
    }

    void thumbnailsAndBookmarksPrintPages() {
        std::printf("OT_FEATURE:view-10\n");
        std::printf("OT_FEATURE:view-11\n");
        MainWindow w;
        w.setCloseConfirm([](ot::ui::DocumentView&) { return MainWindow::CloseAnswer::Discard; });
        w.resize(1440, 900);
        w.show();
        QVERIFY(w.openFile(source("fixtures/demo/report.pdf")));
        QTRY_VERIFY(w.currentDocument() != nullptr && w.currentDocument()->session().isOpen());
        // Миниатюры: выделены 2, 3, 6 — «Печать страниц…» на выделенной даёт их, на другой — её одну.
        w.navPane().showPane(QStringLiteral("thumbnails"));
        auto* thumbs = static_cast<ot::ui::ThumbnailsPane*>(w.navPane().page(QStringLiteral("thumbnails")));
        QTRY_VERIFY(thumbs->list()->model()->rowCount() == 12);
        auto* sel = thumbs->list()->selectionModel();
        sel->clearSelection();
        for (const int row : {1, 2, 5}) {
            sel->select(thumbs->list()->model()->index(row, 0), QItemSelectionModel::Select);
        }
        QCOMPARE(w.currentDocument()->selectedPages(), (QList<int>{1, 2, 5}));
        QCOMPARE(thumbs->pagesForMenu(2), (QList<int>{1, 2, 5}));
        QCOMPARE(thumbs->pagesForMenu(8), (QList<int>{8}));
        thumbs->printPages(thumbs->pagesForMenu(2));
        ot::ui::PrintDialog* d = w.printDialog();
        QVERIFY(d != nullptr);
        QVERIFY(d->selectedButton()->isChecked());
        QCOMPARE(d->plan().pages, (std::vector<int>{1, 2, 5}));
        d->close();
        // Закладки: «1. Просмотр документов» — стр. 3–4 (до «2. Комментарии»), «1.2 Поиск» — 4, «5.2 Языки» —
        // до конца.
        w.navPane().showPane(QStringLiteral("bookmarks"));
        auto* marks = static_cast<ot::ui::BookmarksPane*>(w.navPane().page(QStringLiteral("bookmarks")));
        QStandardItemModel* model = marks->model();
        QTRY_VERIFY(model->rowCount() == 7);
        const QModelIndex chapter = model->index(2, 0);
        QCOMPARE(chapter.data().toString(), QStringLiteral("1. Просмотр документов"));
        QCOMPARE(marks->bookmarkPages(chapter), (QList<int>{2, 3}));
        marks->tree()->expand(chapter);
        QTRY_VERIFY(model->rowCount(chapter) == 2 &&
                    !model->index(0, 0, chapter).data(ot::ui::BookmarksPane::PlaceholderRole).toBool());
        QCOMPARE(marks->bookmarkPages(model->index(1, 0, chapter)), (QList<int>{3}));
        QCOMPARE(marks->bookmarkPages(model->index(0, 0)), (QList<int>{0}));
        const QModelIndex last = model->index(6, 0);
        marks->tree()->expand(last);
        QTRY_VERIFY(model->rowCount(last) == 2 &&
                    !model->index(0, 0, last).data(ot::ui::BookmarksPane::PlaceholderRole).toBool());
        QCOMPARE(marks->bookmarkPages(model->index(1, 0, last)), (QList<int>{11}));
        QCOMPARE(marks->bookmarkPages(last), (QList<int>{10, 11}));
        marks->printBookmark(chapter);
        d = w.printDialog();
        QVERIFY(d != nullptr);
        QCOMPARE(d->plan().pages, (std::vector<int>{2, 3}));
        d->close();
    }

    void selfCheckReportsPrintVerbs() {
        QStringList lines;
        ot::app::selfCheck(lines);
        QVERIFY(std::any_of(lines.begin(), lines.end(),
                            [](const QString& l) { return l.startsWith(QLatin1String("print verbs: ")); }));
    }
};

OT_UI_TEST(PrintCliTest, "print_cli");

#include "print_cli_test.moc"
