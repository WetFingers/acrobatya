#include "services/http_fetcher.h"

#include <QByteArray>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <spdlog/spdlog.h>

#include <atomic>

#include "services/settings.h"

namespace ot {

namespace {

std::atomic<net::HttpFetcher*> g_fetcher{nullptr};

const char* accessName(NetworkAccess a) {
    switch (a) {
        case NetworkAccess::Allowed:
            return "allowed";
        case NetworkAccess::SignaturesOnly:
            return "signaturesOnly";
        case NetworkAccess::Off:
            return "off";
    }
    return "?";
}

bool permitted(NetworkAccess access, net::Purpose purpose) {
    switch (access) {
        case NetworkAccess::Allowed:
            return true;
        case NetworkAccess::SignaturesOnly:
            return purpose == net::Purpose::Signatures;
        case NetworkAccess::Off:
            return false;
    }
    return false;
}

std::string logUrl(const QUrl& url) { return url.toString(QUrl::RemoveUserInfo).toStdString(); }

// Тот же хост и порт, схема не слабее (http → https можно, https → http нельзя).
bool sameOrigin(const QUrl& from, const QUrl& to) {
    if (from.host().compare(to.host(), Qt::CaseInsensitive) != 0) {
        return false;
    }
    const auto defaultPort = [](const QUrl& u) { return u.scheme() == QLatin1String("https") ? 443 : 80; };
    if (from.scheme() == QLatin1String("https") && to.scheme() != QLatin1String("https")) {
        return false;
    }
    if (from.scheme() == to.scheme() && from.port(defaultPort(from)) != to.port(defaultPort(to))) {
        return false;
    }
    return true;
}

}  // namespace

NetworkAccess networkAccess(const Settings& settings) {
    const QString v = settings.get(settings::kNetworkAccess);
    if (v == QLatin1String("off")) {
        return NetworkAccess::Off;
    }
    if (v == QLatin1String("signaturesOnly")) {
        return NetworkAccess::SignaturesOnly;
    }
    return NetworkAccess::Allowed;
}

net::Response QtHttpFetcher::fetch(const net::Request& request) {
    net::Response result;
    const QUrl url(QString::fromStdString(request.url), QUrl::StrictMode);
    const bool get = request.method == "GET";
    if (!url.isValid() || url.host().isEmpty() ||
        (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https")) ||
        (!get && request.method != "POST")) {
        result.status = net::Status::BadUrl;
        result.error = "bad request: " + request.method + " " + logUrl(url);
        spdlog::warn("net: {}", result.error);
        return result;
    }
    const NetworkAccess access = fixed_ ? *fixed_ : networkAccess(Settings());
    if (!permitted(access, request.purpose)) {
        result.status = net::Status::Disabled;
        result.error = std::string("network access is ") + accessName(access);
        spdlog::info("net: {} {} blocked ({})", request.method, logUrl(url), result.error);
        return result;
    }

    QNetworkAccessManager nam;
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     request.sameHostRedirectsOnly ? QNetworkRequest::UserVerifiedRedirectPolicy
                                                   : QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setMaximumRedirectsAllowed(5);
    req.setTransferTimeout(request.timeoutMs);
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    req.setHeader(QNetworkRequest::UserAgentHeader, QByteArrayLiteral("AcroBatya/" OTTISK_VERSION_STR));
    if (!request.contentType.empty()) {
        req.setHeader(QNetworkRequest::ContentTypeHeader, QByteArray::fromStdString(request.contentType));
    }
    for (const auto& [name, value] : request.headers) {
        req.setRawHeader(QByteArray::fromStdString(name), QByteArray::fromStdString(value));
    }

    QElapsedTimer clock;
    clock.start();
    QNetworkReply* reply = get ? nam.get(req)
                               : nam.post(req, QByteArray(reinterpret_cast<const char*>(request.body.data()),
                                                          static_cast<qsizetype>(request.body.size())));
    bool timedOut = false;
    bool redirectRefused = false;
    bool tooLarge = false;
    QUrl finalUrl = url;
    QObject::connect(reply, &QNetworkReply::redirected, reply, [&](const QUrl& to) {
        const QUrl target = url.resolved(to);
        finalUrl = target;
        if (!request.sameHostRedirectsOnly) {
            return;  // NoLessSafeRedirectPolicy: Qt идёт сам
        }
        if (sameOrigin(url, target)) {
            emit reply->redirectAllowed();
        } else {
            redirectRefused = true;
            reply->abort();
        }
    });
    QObject::connect(reply, &QNetworkReply::downloadProgress, reply, [&](qint64 received, qint64 total) {
        const auto limit = static_cast<qint64>(request.maxResponseBytes);
        if (received > limit || total > limit) {
            tooLarge = true;
            reply->abort();
        }
    });
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, reply, [&] {
        timedOut = true;
        reply->abort();
    });
    deadline.start(request.timeoutMs);
    if (!reply->isFinished()) {
        loop.exec(QEventLoop::ExcludeUserInputEvents);
    }
    deadline.stop();

    result.httpCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    result.contentType =
        net::mediaType(reply->header(QNetworkRequest::ContentTypeHeader).toString().toStdString());
    if (!redirectRefused) {
        finalUrl = reply->url();
    }
    result.finalUrl = finalUrl.toString(QUrl::RemoveUserInfo).toStdString();
    const QNetworkReply::NetworkError error = reply->error();
    if (redirectRefused) {
        result.status = net::Status::RedirectRefused;
        result.error = "redirect to another host refused: " + result.finalUrl;
    } else if (tooLarge) {
        result.status = net::Status::TooLarge;
        result.error = "response larger than " + std::to_string(request.maxResponseBytes) + " bytes";
    } else if (timedOut || error == QNetworkReply::TimeoutError ||
               error == QNetworkReply::OperationCanceledError) {
        result.status = net::Status::Timeout;
        result.error = "no response in " + std::to_string(request.timeoutMs) + " ms";
    } else if (result.httpCode != 0 && (result.httpCode < 200 || result.httpCode >= 300)) {
        result.status = net::Status::HttpError;
        result.error = "HTTP " + std::to_string(result.httpCode);
    } else if (error != QNetworkReply::NoError) {
        result.status = net::Status::NetworkError;
        result.error = reply->errorString().toStdString();
    } else {
        result.status = net::Status::Ok;
    }
    if (result.status == net::Status::Ok || result.status == net::Status::HttpError) {
        const QByteArray body = reply->readAll();
        if (static_cast<std::size_t>(body.size()) > request.maxResponseBytes) {
            result.status = net::Status::TooLarge;
            result.error = "response larger than " + std::to_string(request.maxResponseBytes) + " bytes";
        } else {
            result.body.assign(body.begin(), body.end());
        }
    }
    delete reply;
    spdlog::info("net: {} {} -> {} {} ({} bytes, {} ms){}", request.method, logUrl(url), result.httpCode,
                 net::statusName(result.status), result.body.size(), clock.elapsed(),
                 result.error.empty() ? std::string() : ": " + result.error);
    return result;
}

net::HttpFetcher& httpFetcher() {
    if (net::HttpFetcher* f = g_fetcher.load()) {
        return *f;
    }
    static QtHttpFetcher real;
    return real;
}

void setHttpFetcher(net::HttpFetcher* fetcher) { g_fetcher.store(fetcher); }

ScopedHttpFetcher::ScopedHttpFetcher(net::HttpFetcher& fetcher) : previous_(g_fetcher.exchange(&fetcher)) {}

ScopedHttpFetcher::~ScopedHttpFetcher() { g_fetcher.store(previous_); }

}  // namespace ot
