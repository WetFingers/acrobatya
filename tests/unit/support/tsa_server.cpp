#include "support/tsa_server.h"

#include <QByteArray>
#include <QDeadlineTimer>
#include <QFileInfo>
#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include "support/pki.h"
#include "support/qt_app.h"
#include "support/test_pdf.h"

namespace ot::test {

TsaServer::TsaServer(std::unique_ptr<QProcess> process, int port)
    : process_(std::move(process)), port_(port) {}

TsaServer::~TsaServer() {
    if (!process_) {
        return;
    }
    process_->closeWriteChannel();  // stdin закрыт — tsa_serve.py останавливается сам
    if (!process_->waitForFinished(3000)) {
        process_->kill();
        process_->waitForFinished(3000);
    }
}

std::string TsaServer::url(std::string_view path) const {
    return "http://127.0.0.1:" + std::to_string(port_) + std::string(path);
}

std::unique_ptr<TsaServer> TsaServer::start(std::string* reason, int timeoutMs) {
    qtApp();
    const QString python = qEnvironmentVariable("OTTISK_PYTHON");
    if (python.isEmpty() || !QFileInfo::exists(python)) {
        *reason = "OTTISK_PYTHON is not set (ctest environment, cmake/tests.cmake)";
        return nullptr;
    }
    const auto script = sourceDir() / "tools" / "pki" / "tsa_serve.py";
    auto process = std::make_unique<QProcess>();
    process->setProgram(python);
    QStringList args = {QStringLiteral("-X"),    QStringLiteral("utf8"),
                        QStringLiteral("-I"),    QString::fromStdWString(script.wstring()),
                        QStringLiteral("--pki"), QString::fromStdWString(pkiDir().wstring())};
    if (const auto openssl = opensslExe()) {
        args << QStringLiteral("--openssl") << QString::fromStdWString(openssl->wstring());
    }
    process->setArguments(args);
    process->setProcessChannelMode(QProcess::SeparateChannels);
    process->start();
    if (!process->waitForStarted(timeoutMs)) {
        *reason = "cannot start " + python.toStdString() + ": " + process->errorString().toStdString();
        return nullptr;
    }
    // Первая строка stdout — порт.
    QByteArray out;
    const QDeadlineTimer deadline(timeoutMs);
    while (!out.contains('\n')) {
        if (process->state() == QProcess::NotRunning) {
            break;
        }
        if (deadline.hasExpired()) {
            break;
        }
        process->waitForReadyRead(100);
        out += process->readAllStandardOutput();
    }
    const int newline = out.indexOf('\n');
    bool ok = false;
    const int port = newline > 0 ? out.left(newline).trimmed().toInt(&ok) : 0;
    if (!ok || port <= 0) {
        if (process->state() == QProcess::NotRunning && process->exitCode() == 77) {
            *reason = "tsa_serve.py: " + process->readAllStandardError().trimmed().toStdString();
        } else {
            *reason = "tsa_serve.py gave no port: " + out.toStdString() + " " +
                      process->readAllStandardError().toStdString();
        }
        process->kill();
        process->waitForFinished(3000);
        return nullptr;
    }
    return std::unique_ptr<TsaServer>(new TsaServer(std::move(process), port));
}

}  // namespace ot::test
