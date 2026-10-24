#include "support/pki.h"

#include <QProcess>
#include <QString>
#include <QStringList>

#include <stdexcept>

#include "core/util/paths.h"
#include "support/qt_app.h"
#include "support/test_pdf.h"

namespace ot::test {

std::filesystem::path pkiDir() { return sourceDir() / "tests" / "fixtures" / "pki"; }

std::string readPki(const char* name) { return paths::readFile(pkiDir() / name); }

std::optional<std::filesystem::path> opensslExe() {
    std::vector<std::filesystem::path> candidates;
    const QString env = qEnvironmentVariable("OTTISK_OPENSSL");
    if (!env.isEmpty()) {
        candidates.emplace_back(env.toStdWString());
    }
    candidates.emplace_back(L"C:\\Program Files\\Git\\mingw64\\bin\\openssl.exe");
    candidates.emplace_back(L"C:\\Program Files\\Git\\usr\\bin\\openssl.exe");
    for (const auto& c : candidates) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(c, ec)) {
            return c;
        }
    }
    return std::nullopt;
}

ProcessResult runProcess(const std::filesystem::path& exe,
                         const std::vector<std::filesystem::path::string_type>& args, int timeoutMs) {
    qtApp();
    QProcess p;
    QStringList list;
    for (const auto& a : args) {
        list << QString::fromStdWString(a);
    }
    p.setProgram(QString::fromStdWString(exe.wstring()));
    p.setArguments(list);
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.setStandardInputFile(QProcess::nullDevice());
    ProcessResult r;
    p.start();
    if (!p.waitForStarted(timeoutMs)) {
        r.output = p.errorString().toStdString();
        return r;
    }
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(3000);
        r.output = "timeout: " + p.readAll().toStdString();
        return r;
    }
    r.output = p.readAll().toStdString();
    r.exitCode = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    return r;
}

}  // namespace ot::test
