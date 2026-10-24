// Выбор сертификата из хранилища Windows (эталон 3.8 «Диалог подписания»: «хранилище Windows —
// CryptUIDlgSelectCertificateFromStore на MY, фильтр „есть закрытый ключ и Digital Signature“»; план 4.3, п.
// 1; sign-08). Диалог системный и модальный — только на настоящем экране: в offscreen и без QGuiApplication
// кнопка «Выбрать из хранилища Windows» выключена с подписью (available() == false), а pick ничего не
// открывает. В диалоге — только подходящие сертификаты «Личное» (core/sign/cng_store.h: filtered); карты и
// токены с мини-драйвером видны там же (sign-07 → sign-08). Заголовок и подсказку даёт интерфейс через tr().
#pragma once

#include <QByteArray>
#include <QString>
#include <QtGlobal>

#include <optional>

namespace ot {

struct PickedCertificate {
    QString thumbprint;  // SHA-1 HEX — WindowsCert{thumbprint} для подписи
    QString name;        // CN
    QString subject;
    QString issuer;
    QString notAfter;  // ISO
    QByteArray der;
    bool smartCard = false;
};

class CertPicker {
public:
    // Можно показать диалог Windows: есть QGuiApplication на настоящей платформе (не offscreen/minimal).
    static bool available();
    // Диалог над «Личное» с фильтром подписи; window — HWND окна-владельца (0 — без владельца). Отмена,
    // пустое хранилище или available() == false — nullopt.
    static std::optional<PickedCertificate> pick(quintptr window, const QString& title,
                                                 const QString& prompt);
};

}  // namespace ot
