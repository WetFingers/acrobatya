// «Параметры доверия» сертификата (план 4.6, п. 2–3; эталон 3.8 «Доверие»; sign-30, sign-34): «Добавить в
// доверенные» из просмотра сертификата и «Изменить доверие» в Установках → Подписи → Доверенные сертификаты —
// кому выдан, отпечаток SHA-1, для чего доверять (как в Acrobat): подписанные документы или данные (по
// умолчанию), сертифицированные документы, динамическое содержимое, встроенный JavaScript с повышенными
// правами. Флаги — биты kTrust* core/sign/trust_store.h; записывает зовущий (TrustService).
#pragma once

#include <QDialog>

#include "core/sign/trust_store.h"
#include "core/sign/x509_info.h"

class QCheckBox;
class QLabel;
class QPushButton;

namespace ot::ui {

class TrustDialog : public QDialog {
    Q_OBJECT
public:
    TrustDialog(const sign::CertSummary& cert, unsigned flags, bool existing, QWidget* parent = nullptr);

    unsigned flags() const;
    QCheckBox* signaturesBox() const { return signatures_; }
    QCheckBox* certificationBox() const { return certification_; }
    QCheckBox* dynamicBox() const { return dynamic_; }
    QCheckBox* javaScriptBox() const { return javaScript_; }
    QPushButton* okButton() const { return ok_; }

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslate();

    sign::CertSummary cert_;
    bool existing_;
    QLabel* intro_ = nullptr;
    QLabel* who_ = nullptr;
    QLabel* warning_ = nullptr;
    QCheckBox* signatures_ = nullptr;
    QCheckBox* certification_ = nullptr;
    QCheckBox* dynamic_ = nullptr;
    QCheckBox* javaScript_ = nullptr;
    QPushButton* ok_ = nullptr;
    QPushButton* cancel_ = nullptr;
};

}  // namespace ot::ui
