// Подписи вкладки (план 4.6, п. 2; эталон 3.8 «Валидатор», 6.1 «полоса сообщений»; sign-25, sign-26):
// проверка при открытии (Установки → Подписи → Проверка, по умолчанию — да), после записи (подпись, метка,
// сведения LTV, очистка) и при смене доверенных сертификатов — services/crypto_worker.h, без блокировки окна.
// Итог — полоса над документом как в Acrobat: синяя «Подписано, все подписи действительны» (у
// сертифицированного — «Сертифицировано…»), жёлтая «Проблемы с подписью», красная «Хотя бы одна подпись
// недействительна» — с кнопкой «Панель подписей» (и «Доверять подписанту…», если подпись цела, а подписант не
// доверен — по настройке); значки статуса в левом верхнем углу видимых полей подписи (слой над страницами, z
// 45). Поля подписи (и пустые) — для панели «Подписи». Владелец — DocumentView.
#pragma once

#include <QObject>
#include <QPointer>

#include <memory>
#include <vector>

#include "core/sign/sig_fields.h"
#include "core/sign/verifier.h"

namespace ot::ui {

class DocumentView;
class MessageBar;
class SignatureBadges;

class SignatureStatus : public QObject {
    Q_OBJECT
public:
    explicit SignatureStatus(DocumentView& view);
    ~SignatureStatus() override;

    DocumentView& view() const { return view_; }

    // Проверить подписи сейчас (повтор во время проверки — ещё раз после неё).
    void verify();
    bool isVerifying() const { return running_; }
    // Была ли проверка (verification() — её итог).
    bool hasResult() const { return hasResult_; }
    const sign::DocumentVerification& verification() const { return verification_; }
    // Подпись по полю (nullptr — не подписано или проверки ещё не было).
    const sign::SignatureInfo* signature(const QString& field) const;
    // Поля подписи документа (подписанные и пустые) — после проверки.
    const std::vector<sign::SignatureField>& fields() const { return fields_; }
    // Ошибка проверки (файл не открылся) — английский текст; пусто — нет.
    QString error() const { return error_; }

    // Полоса подписей (nullptr — нет).
    MessageBar* bar() const;
    SignatureBadges& badges() const { return *badges_; }

signals:
    void started();
    void verified();
    void paneRequested();
    // «Доверять подписанту…»: показать сертификат подписи на вкладке «Доверие».
    void trustRequested(const QString& field);

private:
    void finish(sign::DocumentVerification v, std::vector<sign::SignatureField> fields, QString error);
    void showBar();
    void clear();
    void onSaved();

    DocumentView& view_;
    std::unique_ptr<SignatureBadges> badges_;
    sign::DocumentVerification verification_;
    std::vector<sign::SignatureField> fields_;
    QString error_;
    quint64 generation_ = 0;
    bool running_ = false;
    bool again_ = false;
    bool hasResult_ = false;
};

}  // namespace ot::ui
