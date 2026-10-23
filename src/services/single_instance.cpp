#include "services/single_instance.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>

#include <memory>

#include <windows.h>
#include <sddl.h>

#include <spdlog/spdlog.h>

#pragma comment(lib, "advapi32.lib")

namespace ot {

namespace {

constexpr int kMaxLine = 1 << 20;  // запрос — пути файлов; больше мегабайта — чужой клиент

}  // namespace

QString SingleInstance::userSid() {
    HANDLE token = nullptr;
    QString sid;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        DWORD size = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &size);
        if (size > 0) {
            auto buffer = std::make_unique<unsigned char[]>(size);
            auto* user = reinterpret_cast<TOKEN_USER*>(buffer.get());
            LPWSTR text = nullptr;
            if (GetTokenInformation(token, TokenUser, user, size, &size) &&
                ConvertSidToStringSidW(user->User.Sid, &text)) {
                sid = QString::fromWCharArray(text);
                LocalFree(text);
            }
        }
        CloseHandle(token);
    }
    if (sid.isEmpty()) {
        sid = qEnvironmentVariable("USERNAME", QStringLiteral("user"));
    }
    return sid;
}

QString SingleInstance::instance() {
    const QString name = qEnvironmentVariable("OTTISK_INSTANCE");
    return name.isEmpty() ? QStringLiteral("main") : name;
}

QString SingleInstance::serverName() {
    return QStringLiteral("AcroBatya-") + userSid() + QLatin1Char('-') + instance();
}

QByteArray SingleInstance::encode(const Request& request) {
    QJsonArray files;
    for (const QString& f : request.files) {
        files.append(f);
    }
    QJsonObject o;
    o.insert(QStringLiteral("files"), files);
    o.insert(QStringLiteral("page"), request.page);
    return QJsonDocument(o).toJson(QJsonDocument::Compact) + '\n';
}

bool SingleInstance::decode(const QByteArray& line, Request* request) {
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(line.trimmed(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return false;
    }
    const QJsonObject o = doc.object();
    Request r;
    for (const QJsonValue& v : o.value(QStringLiteral("files")).toArray()) {
        if (v.isString() && !v.toString().isEmpty()) {
            r.files << v.toString();
        }
    }
    r.page = o.value(QStringLiteral("page")).toInt();
    *request = r;
    return true;
}

bool SingleInstance::sendToRunning(const Request& request, const QString& name, int timeoutMs) {
    Request absolute = request;
    for (QString& f : absolute.files) {
        f = QFileInfo(f).absoluteFilePath();  // у первого экземпляра своя текущая папка
    }
    QLocalSocket socket;
    socket.connectToServer(name);
    if (!socket.waitForConnected(timeoutMs)) {
        return false;  // никто не слушает — этот экземпляр первый
    }
    socket.write(encode(absolute));
    if (!socket.waitForBytesWritten(timeoutMs)) {
        return false;
    }
    QByteArray reply;
    while (!reply.contains('\n') && socket.waitForReadyRead(timeoutMs)) {
        reply += socket.readAll();
    }
    socket.disconnectFromServer();
    return reply.trimmed() == "ok";
}

SingleInstance::SingleInstance(QString name, QObject* parent)
    : QObject(parent), name_(std::move(name)), server_(new QLocalServer(this)) {
    server_->setSocketOptions(QLocalServer::UserAccessOption);
    connect(server_, &QLocalServer::newConnection, this, &SingleInstance::accept);
}

SingleInstance::~SingleInstance() = default;

bool SingleInstance::listen() {
    if (server_->isListening()) {
        return true;
    }
    if (!server_->listen(name_)) {
        error_ = server_->errorString();
        spdlog::warn("single instance: cannot listen on {}: {}", name_.toStdString(), error_.toStdString());
        return false;
    }
    return true;
}

bool SingleInstance::isListening() const { return server_->isListening(); }

void SingleInstance::accept() {
    while (QLocalSocket* socket = server_->nextPendingConnection()) {
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QLocalSocket::readyRead, this, [this, socket] { readFrom(socket); });
        if (socket->bytesAvailable() > 0) {
            readFrom(socket);
        }
    }
}

void SingleInstance::readFrom(QLocalSocket* socket) {
    // Строка может прийти частями: начало копится в свойстве сокета.
    const QByteArray buffer = socket->property("otBuffer").toByteArray() + socket->readAll();
    const qsizetype end = buffer.indexOf('\n');
    if (end < 0) {
        if (buffer.size() > kMaxLine) {
            socket->abort();
            return;
        }
        socket->setProperty("otBuffer", buffer);
        return;
    }
    socket->setProperty("otBuffer", QByteArray());
    Request request;
    const bool ok = decode(buffer.left(end), &request);
    socket->write(ok ? "ok\n" : "bad\n");
    socket->flush();
    socket->disconnectFromServer();
    if (!ok) {
        spdlog::warn("single instance: bad request ({} bytes)", end);
        return;
    }
    emit requestReceived(request);
}

}  // namespace ot
