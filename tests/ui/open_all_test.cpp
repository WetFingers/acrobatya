// ui.open_all — режим --open-all (план 2.4b; эталон 3.18, 9.6 — M1 «корпус открывается без падений»): папка с
// мутантами (обрезанные, испорченные байты, xref, мусор, пустой и текстовый файл), зашифрованным и целыми PDF
// в подпапке — сбоев 0: битые — отказ, целые — открыты и стр. 1 нарисована; мутанты engine
// (build/<пресет>/gen/ mutants, если сгенерированы) — тоже 0; AcroBatya.exe --open-all <папка> — код 0 и
// строка итога, нет папки — код 1.
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRandomGenerator>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include "support/ui_test.h"
#include "ui/app/cli.h"

namespace {

QByteArray readFixture(const char* rel) {
    QFile f(QStringLiteral(OTTISK_SOURCE_DIR "/tests/") + QLatin1String(rel));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

void put(const QString& path, const QByteArray& bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(bytes);
}

QString exePath() {
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("AcroBatya.exe"));
}

// Мутанты из целого PDF: детерминированно (seed), без внешних генераторов.
int writeMutants(const QString& dir, const QByteArray& pdf) {
    QRandomGenerator rng(20261024);
    int n = 0;
    auto add = [&](const QByteArray& bytes) {
        put(QDir(dir).filePath(QStringLiteral("m%1.pdf").arg(++n, 2, 10, QLatin1Char('0'))), bytes);
    };
    add(pdf.left(pdf.size() / 2));   // обрезан пополам
    add(pdf.left(pdf.size() - 40));  // без трейлера
    add(pdf.left(16));               // только заголовок
    QByteArray noXref = pdf;
    const qsizetype xref = noXref.lastIndexOf("xref");
    if (xref > 0) {
        noXref.replace(xref, 4, "XXXX");
    }
    add(noXref);                   // xref испорчен
    for (int k = 0; k < 4; ++k) {  // байты испорчены по всему файлу
        QByteArray flipped = pdf;
        for (int i = 0; i < 64; ++i) {
            const auto at = static_cast<qsizetype>(rng.bounded(static_cast<quint32>(flipped.size())));
            flipped[at] = static_cast<char>(rng.bounded(256));
        }
        add(flipped);
    }
    QByteArray garbage(4096, '\0');
    for (char& c : garbage) {
        c = static_cast<char>(rng.bounded(256));
    }
    add(garbage);                         // мусор
    add(QByteArray());                    // пустой
    add("this is not a PDF at all\r\n");  // текст под именем .pdf
    add("%PDF-1.7\n1 0 obj << /Type /Catalog /Pages 2 0 R >> endobj\ntrailer << /Root 1 0 R >>\n%%EOF\n");
    return n;
}

}  // namespace

class OpenAllTest : public QObject {
    Q_OBJECT
private slots:
    void mutantsGiveNoFailures() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QByteArray mini = readFixture("data/mini/type1-diff.pdf");
        const QByteArray demo = readFixture("fixtures/demo/report.pdf");
        QVERIFY(!mini.isEmpty() && !demo.isEmpty());
        const int mutants = writeMutants(dir.path(), mini) +
                            writeMutants(QDir(dir.path()).filePath(QStringLiteral("demo")), demo);
        put(QDir(dir.path()).filePath(QStringLiteral("whole/report.pdf")), demo);
        put(QDir(dir.path()).filePath(QStringLiteral("whole/mini.PDF")), mini);
        put(QDir(dir.path()).filePath(QStringLiteral("whole/encrypted.pdf")),
            readFixture("fixtures/enc/enc-aes-256-r6.pdf"));
        put(QDir(dir.path()).filePath(QStringLiteral("whole/notes.txt")), "not probed");
        const ot::ui::OpenAllReport r = ot::ui::openAll(dir.path(), 20000);
        QCOMPARE(r.files, mutants + 3);  // .txt не пробуется
        QVERIFY2(r.failures == 0, qPrintable(r.lines.join(QLatin1Char('\n'))));
        QVERIFY(r.opened >= 2);
        QVERIFY(r.refused >= 1);
        QCOMPARE(r.opened + r.refused + r.failures, r.files);
        QVERIFY(r.lines.join(QLatin1Char('\n')).contains(QStringLiteral("encrypted.pdf: password")));
    }

    void engineMutantsGiveNoFailures() {
        const QString dir = QStringLiteral(OTTISK_BINARY_DIR "/gen/mutants");
        if (!QFileInfo(dir).isDir()) {
            QSKIP("engine mutants are not generated (ctest -R engine.mutants.generate)");
        }
        const ot::ui::OpenAllReport r = ot::ui::openAll(dir, 20000);
        QVERIFY(r.files > 0);
        QVERIFY2(r.failures == 0, qPrintable(r.lines.join(QLatin1Char('\n'))));
    }

    void exeReturnsNumberOfFailures() {
        const QString exe = exePath();
        if (!QFileInfo::exists(exe)) {
            QSKIP("AcroBatya.exe is not built next to ui_tests");
        }
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const int n = writeMutants(dir.path(), readFixture("data/mini/type1-diff.pdf"));
        put(QDir(dir.path()).filePath(QStringLiteral("ok.pdf")), readFixture("data/mini/blank-a4.pdf"));
        QProcess p;
        p.start(exe, {QStringLiteral("--open-all"), dir.path()});
        QVERIFY(p.waitForFinished(120000));
        const QString out = QString::fromUtf8(p.readAllStandardOutput());
        QCOMPARE(p.exitCode(), 0);
        QVERIFY2(out.contains(QStringLiteral("open-all: %1 files, opened ").arg(n + 1)), qPrintable(out));
        QVERIFY2(out.contains(QStringLiteral(", failures 0")), qPrintable(out));
        QProcess missing;
        missing.start(exe, {QStringLiteral("--open-all"), QDir(dir.path()).filePath(QStringLiteral("nope"))});
        QVERIFY(missing.waitForFinished(60000));
        QCOMPARE(missing.exitCode(), 1);
    }
};

OT_UI_TEST(OpenAllTest, "open_all");
#include "open_all_test.moc"
