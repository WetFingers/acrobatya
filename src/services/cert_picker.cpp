#include "services/cert_picker.h"

#include <windows.h>
#include <cryptuiapi.h>
#include <wincrypt.h>

#include <QCoreApplication>
#include <QGuiApplication>

#include <spdlog/spdlog.h>

#include "core/sign/cng_store.h"

#pragma comment(lib, "cryptui.lib")

namespace ot {

bool CertPicker::available() {
    if (qobject_cast<QGuiApplication*>(QCoreApplication::instance()) == nullptr) {
        return false;
    }
    const QString platform = QGuiApplication::platformName();
    return platform != QLatin1String("offscreen") && platform != QLatin1String("minimal");
}

std::optional<PickedCertificate> CertPicker::pick(quintptr window, const QString& title,
                                                  const QString& prompt) {
    if (!available()) {
        return std::nullopt;
    }
    try {
        const sign::cng::CertStore personal = sign::cng::CertStore::openPersonal();
        const sign::cng::CertStore list = sign::cng::filtered(personal, sign::cng::CertFilter{});
        const std::wstring t = title.toStdWString();
        const std::wstring p = prompt.toStdWString();
        PCCERT_CONTEXT chosen = CryptUIDlgSelectCertificateFromStore(static_cast<HCERTSTORE>(list.handle()),
                                                                     reinterpret_cast<HWND>(window),
                                                                     t.c_str(), p.c_str(), 0, 0, nullptr);
        if (chosen == nullptr) {
            return std::nullopt;  // отмена
        }
        const sign::cng::CertEntry e = sign::cng::describe(sign::cng::CertContext(chosen));
        PickedCertificate out;
        out.thumbprint = QString::fromStdString(e.thumbprint);
        out.name = QString::fromStdString(e.summary.commonName);
        out.subject = QString::fromStdString(e.summary.subject);
        out.issuer = QString::fromStdString(e.summary.issuer);
        out.notAfter = QString::fromStdString(e.summary.notAfter);
        out.der = QByteArray(reinterpret_cast<const char*>(e.summary.der.data()),
                             static_cast<qsizetype>(e.summary.der.size()));
        out.smartCard = e.smartCard;
        return out;
    } catch (const std::exception& ex) {
        spdlog::error("cert picker: {}", ex.what());
        return std::nullopt;
    }
}

}  // namespace ot
