// Действия --script подписей (план 4.6, п. 4; рецепт «Новое действие --script»; рубеж Р2):
//   sign.sign{pfx, password, level?, reason?, location?, page?, rect?, field?, lock?, tsa?, pkcs7?, target?}
//   —
//     подписать текущий документ удостоверением из файла .pfx (уровень B/T/LT/LTA, по умолчанию T; служба
//     времени — имя из «Серверов времени» или адрес, по умолчанию — служба по умолчанию; рамка — [x0, y0, x1,
//     y1] в пунктах страницы от левого верхнего угла, нет — невидимая; поле — существующее пустое /Sig;
//     target — куда записать, по умолчанию — файл документа);
//   sign.certify{pfx, password, perms, …} — сертифицировать (perms: 1 — никаких изменений, 2 — формы и
//     подписи, 3 — и комментарии); прочие аргументы — как у sign.sign;
//   sign.timestamp{tsa?, target?} — метка времени документа;
//   sign.verifyAll{expect?} — проверить подписи вкладки (как «Проверить все»); expect — valid | certified |
//     problems | invalid | notSigned: иной итог — ошибка шага;
//   sign.trust{cert, certified?} — сертификат .cer/.pem/.p7c — в доверенные программы (для подписей, с
//     certified — и для сертифицированных документов).
// Служба времени не ответила — по настройке «служба не ответила»: «спросить» в сценарии — отказ (вопрос
// задать некому). Ошибки — английский текст ScriptError.
#include "ui/tools/certificate/sign_script.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QThread>

#include <exception>
#include <memory>

#include "core/sign/doc_timestamp.h"
#include "core/sign/pkcs12.h"
#include "core/sign/signer.h"
#include "services/http_fetcher.h"
#include "services/script_runner.h"
#include "services/settings.h"
#include "services/trust_service.h"
#include "services/tsa_settings.h"
#include "ui/app/script_actions.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shell/signature_status.h"

namespace ot::ui {

namespace sign_script {

constexpr const char* kSignSchema =
    R"({"type": "object", "properties": {"pfx": {"type": "string", "minLength": 1},
        "password": {"type": "string"}, "level": {"type": "string", "enum": ["B", "T", "LT", "LTA"]},
        "reason": {"type": "string"}, "location": {"type": "string"},
        "page": {"type": "integer", "minimum": 1},
        "rect": {"type": "array", "items": {"type": "number"}, "minItems": 4, "maxItems": 4},
        "field": {"type": "string", "minLength": 1}, "lock": {"type": "boolean"},
        "tsa": {"type": "string", "minLength": 1}, "pkcs7": {"type": "boolean"},
        "target": {"type": "string", "minLength": 1}},
        "required": ["pfx", "password"], "additionalProperties": false})";

constexpr const char* kCertifySchema =
    R"({"type": "object", "properties": {"pfx": {"type": "string", "minLength": 1},
        "password": {"type": "string"}, "perms": {"type": "integer", "minimum": 1, "maximum": 3},
        "level": {"type": "string", "enum": ["B", "T", "LT", "LTA"]},
        "reason": {"type": "string"}, "location": {"type": "string"},
        "page": {"type": "integer", "minimum": 1},
        "rect": {"type": "array", "items": {"type": "number"}, "minItems": 4, "maxItems": 4},
        "tsa": {"type": "string", "minLength": 1}, "target": {"type": "string", "minLength": 1}},
        "required": ["pfx", "password", "perms"], "additionalProperties": false})";

QByteArray readFile(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        throw ScriptError(QStringLiteral("cannot read %1").arg(path));
    }
    return f.readAll();
}

std::span<const std::uint8_t> bytes(const QByteArray& b) {
    return {reinterpret_cast<const std::uint8_t*>(b.constData()), static_cast<std::size_t>(b.size())};
}

std::shared_ptr<const sign::SigningKey> identity(const QString& pfx, const QString& password) {
    const QByteArray data = readFile(pfx);
    try {
        return std::make_shared<const sign::SigningKey>(
            sign::loadPkcs12(bytes(data), password.toStdString()));
    } catch (const std::exception& e) {
        throw ScriptError(QStringLiteral("identity %1: %2").arg(pfx, QString::fromUtf8(e.what())));
    }
}

// Служба времени: имя из «Серверов времени», адрес http(s) или служба по умолчанию.
std::optional<sign::TsaServer> tsaServer(const QJsonObject& a) {
    const sign::TsaServers servers = loadTsaServers(Settings());
    const QString name = a.value(QLatin1String("tsa")).toString();
    if (name.isEmpty()) {
        if (const sign::TsaServer* s = servers.defaultServer()) {
            return *s;
        }
        return std::nullopt;
    }
    if (const sign::TsaServer* s = servers.find(name.toStdString())) {
        return *s;
    }
    if (name.startsWith(QLatin1String("http://")) || name.startsWith(QLatin1String("https://"))) {
        sign::TsaServer s;
        s.name = "script";
        s.url = name.toStdString();
        return s;
    }
    throw ScriptError(QStringLiteral("time-stamp server \"%1\" is not in the list").arg(name));
}

sign::SignLevel level(const QJsonObject& a) {
    const QString l = a.value(QLatin1String("level")).toString(QStringLiteral("T"));
    return l == QLatin1String("B")     ? sign::SignLevel::B
           : l == QLatin1String("LT")  ? sign::SignLevel::LT
           : l == QLatin1String("LTA") ? sign::SignLevel::LTA
                                       : sign::SignLevel::T;
}

void sign(MainWindow& window, const QJsonObject& a, ScriptContext& ctx,
          std::optional<sign::MdpPermission> certify) {
    DocumentView& doc = script::document(window);
    const Settings settings;
    sign::SignRequest r;
    r.identity = identity(ctx.path(a.value(QLatin1String("pfx")).toString()),
                          a.value(QLatin1String("password")).toString());
    r.level = level(a);
    r.reason = a.value(QLatin1String("reason")).toString().toStdString();
    r.location = a.value(QLatin1String("location")).toString().toStdString();
    r.lock = a.value(QLatin1String("lock")).toBool(false);
    r.subFilter = a.value(QLatin1String("pkcs7")).toBool(settings.get(settings::kSignCompatibility))
                      ? sign::SubFilter::Pkcs7Detached
                      : sign::SubFilter::CadesDetached;
    r.placement.field = a.value(QLatin1String("field")).toString().toStdString();
    r.placement.page = a.value(QLatin1String("page")).toInt(1) - 1;
    if (r.placement.page >= doc.session().pageCount()) {
        throw ScriptError(QStringLiteral("page %1 is out of range").arg(r.placement.page + 1));
    }
    const QJsonArray rect = a.value(QLatin1String("rect")).toArray();
    if (rect.size() == 4) {
        r.placement.rect = {static_cast<float>(rect[0].toDouble()), static_cast<float>(rect[1].toDouble()),
                            static_cast<float>(rect[2].toDouble()), static_cast<float>(rect[3].toDouble())};
    }
    r.certify = certify;
    std::shared_ptr<const sign::TrustStore> trust;
    if (r.level != sign::SignLevel::B) {
        if (const std::optional<sign::TsaServer> server = tsaServer(a)) {
            r.timestamp = makeTsaClient(*server);
        }
        r.withoutTimestamp = [fallback = timestampFallback(settings)](const sign::TsaError&) {
            return fallback == sign::TimestampFallback::WithoutTimestamp;  // спросить в сценарии некого
        };
    }
    if (r.level == sign::SignLevel::LT || r.level == sign::SignLevel::LTA) {
        trust = TrustService::shared().snapshot();
        r.trust = trust.get();
        r.revocationFetcher = &httpFetcher();
        r.vri = settings.get(settings::kLtvVri);
    }
    const QString target = a.contains(QLatin1String("target"))
                               ? ctx.path(a.value(QLatin1String("target")).toString())
                               : QString();
    try {
        (void)script::await(doc.session().sign(std::move(r), target), "sign");
    } catch (const sign::SignError& e) {
        throw ScriptError(QStringLiteral("signing failed (%1): %2")
                              .arg(static_cast<int>(e.status()))
                              .arg(QString::fromUtf8(e.what())));
    }
}

const char* statusName(sign::DocumentStatus s) {
    switch (s) {
        case sign::DocumentStatus::Valid:
            return "valid";
        case sign::DocumentStatus::Certified:
            return "certified";
        case sign::DocumentStatus::Problems:
            return "problems";
        case sign::DocumentStatus::Invalid:
            return "invalid";
        case sign::DocumentStatus::NotSigned:
            break;
    }
    return "notSigned";
}

}  // namespace sign_script

void registerSignScriptActions(ScriptRunner& runner, MainWindow& w) {
    MainWindow* window = &w;
    runner.registerAction(QStringLiteral("sign.sign"), sign_script::kSignSchema,
                          [window](const QJsonObject& a, ScriptContext& ctx) {
                              sign_script::sign(*window, a, ctx, std::nullopt);
                          });
    runner.registerAction(QStringLiteral("sign.certify"), sign_script::kCertifySchema,
                          [window](const QJsonObject& a, ScriptContext& ctx) {
                              const int p = a.value(QLatin1String("perms")).toInt();
                              sign_script::sign(*window, a, ctx, static_cast<sign::MdpPermission>(p));
                          });
    runner.registerAction(
        QStringLiteral("sign.timestamp"),
        R"({"type": "object", "properties": {"tsa": {"type": "string", "minLength": 1},
            "target": {"type": "string", "minLength": 1}}, "additionalProperties": false})",
        [window](const QJsonObject& a, ScriptContext& ctx) {
            DocumentView& doc = script::document(*window);
            const std::optional<sign::TsaServer> server = sign_script::tsaServer(a);
            if (!server) {
                throw ScriptError(QStringLiteral("no time-stamp server"));
            }
            const QString target = a.contains(QLatin1String("target"))
                                       ? ctx.path(a.value(QLatin1String("target")).toString())
                                       : QString();
            try {
                (void)script::await(doc.session().timestampDocument(makeTsaClient(*server), target),
                                    "sign.timestamp");
            } catch (const sign::SignError& e) {
                throw ScriptError(QStringLiteral("time-stamp failed: %1").arg(QString::fromUtf8(e.what())));
            }
        });
    runner.registerAction(
        QStringLiteral("sign.verifyAll"),
        R"({"type": "object", "properties": {"expect": {"type": "string",
            "enum": ["valid", "certified", "problems", "invalid", "notSigned"]}}, "additionalProperties": false})",
        [window](const QJsonObject& a, ScriptContext&) {
            DocumentView& doc = script::document(*window);
            SignatureStatus& status = doc.signatures();
            status.verify();
            QElapsedTimer t;
            t.start();
            // Проверка идёт в пуле CryptoWorker — ждать с циклом событий (итог приходит сигналом в окно).
            while (status.isVerifying() || !status.hasResult()) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
                if (t.elapsed() > 120000) {
                    throw ScriptError(QStringLiteral("sign.verifyAll: timed out"));
                }
                QThread::msleep(1);
            }
            script::settle();
            const QString got = QLatin1String(sign_script::statusName(status.verification().status));
            const QString want = a.value(QLatin1String("expect")).toString();
            if (!want.isEmpty() && got != want) {
                QStringList details;
                for (const sign::SignatureInfo& s : status.verification().signatures) {
                    details << QStringLiteral("%1: %2").arg(QString::fromStdString(s.field),
                                                            QLatin1String(sign::verdictName(s.verdict)));
                }
                throw ScriptError(QStringLiteral("signatures are %1, expected %2 (%3)")
                                      .arg(got, want, details.join(QStringLiteral("; "))));
            }
        });
    runner.registerAction(
        QStringLiteral("sign.trust"),
        R"({"type": "object", "properties": {"cert": {"type": "string", "minLength": 1},
            "certified": {"type": "boolean"}}, "required": ["cert"], "additionalProperties": false})",
        [](const QJsonObject& a, ScriptContext& ctx) {
            const QString path = ctx.path(a.value(QLatin1String("cert")).toString());
            const unsigned flags = a.value(QLatin1String("certified")).toBool(false)
                                       ? sign::kTrustSignatures | sign::kTrustCertification
                                       : sign::kTrustSignatures;
            try {
                TrustService::shared().edit([&](sign::TrustStore& s) {
                    (void)s.importFile(std::filesystem::path(path.toStdWString()), flags);
                });
            } catch (const std::exception& e) {
                throw ScriptError(
                    QStringLiteral("cannot trust %1: %2").arg(path, QString::fromUtf8(e.what())));
            }
        });
}

}  // namespace ot::ui
