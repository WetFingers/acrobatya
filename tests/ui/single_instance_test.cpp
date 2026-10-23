// ui.single_instance — второй экземпляр (план 2.3; эталон 3.1 «Открытие»): имя канала
// AcroBatya-<SID>-<экземпляр> (в тестах OTTISK_INSTANCE=test — файлы не уходят «АкроБате» человека), запрос
// JSON с кириллицей туда и обратно, клиент QLocalSocket передаёт путь первому и получает «ok», сломанный
// запрос — «bad» без сигнала, без сервера sendToRunning — false, окно открывает присланные файлы.
#include <QFutureWatcher>
#include <QLocalSocket>
#include <QSignalSpy>
#include <QUuid>
#include <QtConcurrent/QtConcurrent>
#include <QtTest/QtTest>

#include "services/single_instance.h"
#include "support/ui_test.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/view/page_view.h"

using ot::SingleInstance;

namespace {

QString uniqueName() {
    return QStringLiteral("AcroBatya-test-") + QUuid::createUuid().toString(QUuid::WithoutBraces);
}

QString fixture(const char* rel) {
    return QStringLiteral(OTTISK_SOURCE_DIR "/tests/fixtures/") + QLatin1String(rel);
}

}  // namespace

class SingleInstanceTest : public QObject {
    Q_OBJECT
private slots:
    void nameHasSidAndInstance() {
        QCOMPARE(qEnvironmentVariable("OTTISK_INSTANCE"), QStringLiteral("test"));  // окружение ctest
        QCOMPARE(SingleInstance::instance(), QStringLiteral("test"));
        const QString name = SingleInstance::serverName();
        QVERIFY2(name.startsWith(QStringLiteral("AcroBatya-S-1-5-")), qPrintable(name));
        QVERIFY(name.endsWith(QStringLiteral("-test")));
        qunsetenv("OTTISK_INSTANCE");
        QVERIFY(SingleInstance::serverName().endsWith(QStringLiteral("-main")));
        qputenv("OTTISK_INSTANCE", "test");
    }

    void requestRoundTrip() {
        const SingleInstance::Request r{
            {QStringLiteral("C:/Документы/отчёт.pdf"), QStringLiteral("D:/a b.pdf")}, 7};
        const QByteArray line = SingleInstance::encode(r);
        QVERIFY(line.endsWith('\n'));
        QCOMPARE(line.count('\n'), 1);
        SingleInstance::Request back;
        QVERIFY(SingleInstance::decode(line, &back));
        QCOMPARE(back, r);
        QVERIFY(!SingleInstance::decode("{not json", &back));
    }

    // Клиент — QLocalSocket теста: путь первому экземпляру, ответ «ok».
    void socketForwardsPathToFirst() {
        SingleInstance first(uniqueName());
        QVERIFY(first.listen());
        QSignalSpy got(&first, &SingleInstance::requestReceived);
        QLocalSocket client;
        client.connectToServer(first.name());
        QTRY_COMPARE(client.state(), QLocalSocket::ConnectedState);
        const QString path = QStringLiteral("C:/Папка/договор.pdf");
        client.write(SingleInstance::encode({{path}, 2}).left(10));  // строка частями
        client.flush();
        QTest::qWait(20);
        client.write(SingleInstance::encode({{path}, 2}).mid(10));
        client.flush();
        QTRY_COMPARE(got.count(), 1);
        const auto request = got.at(0).at(0).value<SingleInstance::Request>();
        QCOMPARE(request.files, QStringList{path});
        QCOMPARE(request.page, 2);
        QTRY_VERIFY(client.bytesAvailable() > 0 || client.state() == QLocalSocket::UnconnectedState);
        QCOMPARE(client.readAll().trimmed(), QByteArray("ok"));
    }

    void badRequestIsRejected() {
        SingleInstance first(uniqueName());
        QVERIFY(first.listen());
        QSignalSpy got(&first, &SingleInstance::requestReceived);
        QLocalSocket client;
        client.connectToServer(first.name());
        QTRY_COMPARE(client.state(), QLocalSocket::ConnectedState);
        client.write("garbage\n");
        client.flush();
        QTRY_VERIFY(client.bytesAvailable() > 0 || client.state() == QLocalSocket::UnconnectedState);
        QCOMPARE(client.readAll().trimmed(), QByteArray("bad"));
        QCOMPARE(got.count(), 0);
    }

    // sendToRunning (второй экземпляр) — из другого потока: первый отвечает из цикла событий этого.
    void sendToRunningAndWindowOpens() {
        SingleInstance first(uniqueName());
        QVERIFY(first.listen());
        ot::ui::MainWindow w;
        w.show();
        connect(&first, &SingleInstance::requestReceived, &w, &ot::ui::MainWindow::handleRequest);
        const QString name = first.name();
        const QStringList files{fixture("demo/report.pdf"), fixture("demo/form.pdf")};
        QFuture<bool> sent =
            QtConcurrent::run([name, files] { return SingleInstance::sendToRunning({files, 2}, name); });
        QTRY_VERIFY(sent.isFinished());
        QVERIFY(sent.result());
        QTRY_COMPARE(w.documentCount(), 2);
        QTRY_COMPARE(w.document(0)->view().currentPage(), 1);  // --page 2 → вторая страница (с нуля — 1)
    }

    void noServerMeansFirstInstance() {
        QVERIFY(!SingleInstance::sendToRunning({{QStringLiteral("x.pdf")}, 0}, uniqueName(), 200));
        SingleInstance a(uniqueName());
        QVERIFY(a.listen());
        QVERIFY(a.isListening());
    }
};

OT_UI_TEST(SingleInstanceTest, "single_instance");
#include "single_instance_test.moc"
