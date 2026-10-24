// «Просмотр сертификата» — Certificate Viewer (план 4.6, п. 2; эталон 3.8 «Панель «Подписи»» — сертификат:
// Сводка, Детали, Отзыв, Доверие, Политики; sign-27, sign-34, security-09 — статус сертификата, только
// чтение): слева цепочка от корня к подписанту (выбран подписант), справа вкладки по выбранному сертификату:
// «Сводка» — кому и кем выдан, срок, назначение, состояние; «Детали» — все поля и расширения, отпечатки;
// «Отзыв» — итог проверки отзыва (OCSP/CRL, когда, кем) для сертификатов цепочки, кроме якоря; «Доверие» —
// якорь ли он в хранилище программы и для чего, «Добавить в доверенные…» / «Изменить доверие…» (TrustDialog →
// TrustService: проверка подписей открытых документов повторяется); «Политики» — OID политик.
#pragma once

#include <QDialog>

#include <functional>
#include <optional>
#include <vector>

#include "core/sign/revocation.h"
#include "core/sign/x509_info.h"

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QTreeWidget;

namespace ot::ui {

class TrustDialog;

class CertificateViewer : public QDialog {
    Q_OBJECT
public:
    enum Tab { Summary, Details, Revocation, Trust, Policies };
    // Ответ на «Параметры доверия» (тесты): флаги или nullopt — «Отмена»; по умолчанию — exec().
    using TrustPrompt = std::function<std::optional<unsigned>(TrustDialog&)>;

    // chain — подписант → … → корень (или что построено); revocation — по сертификатам цепочки в том же
    // порядке (без якоря), может быть короче.
    CertificateViewer(std::vector<sign::CertSummary> chain, std::vector<sign::RevocationResult> revocation,
                      QWidget* parent = nullptr);

    QTreeWidget* chainTree() const { return chain_; }
    QTabWidget* tabs() const { return tabs_; }
    int chainLength() const { return static_cast<int>(certs_.size()); }
    int selected() const { return selected_; }
    void select(int index);  // 0 — подписант
    void showTab(Tab tab);

    QLabel* summaryLabel() const { return summary_; }
    QLabel* statusLabel() const { return status_; }
    QTreeWidget* detailsTree() const { return details_; }
    QLabel* revocationLabel() const { return revocation_; }
    QLabel* trustLabel() const { return trust_; }
    QPushButton* trustButton() const { return trustButton_; }
    QLabel* policiesLabel() const { return policies_; }

    void setTrustPrompt(TrustPrompt prompt) { trustPrompt_ = std::move(prompt); }
    // «Добавить в доверенные…» / «Изменить доверие…» для выбранного сертификата.
    void editTrust();

signals:
    void trustChanged();

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslate();
    void fill();

    std::vector<sign::CertSummary> certs_;
    std::vector<sign::RevocationResult> revocations_;
    int selected_ = 0;
    TrustPrompt trustPrompt_;
    QTreeWidget* chain_ = nullptr;
    QTabWidget* tabs_ = nullptr;
    QLabel* summary_ = nullptr;
    QLabel* status_ = nullptr;
    QTreeWidget* details_ = nullptr;
    QPlainTextEdit* detailValue_ = nullptr;
    QLabel* revocation_ = nullptr;
    QLabel* trust_ = nullptr;
    QPushButton* trustButton_ = nullptr;
    QLabel* policies_ = nullptr;
    QLabel* chainCaption_ = nullptr;
    QPushButton* close_ = nullptr;
};

}  // namespace ot::ui
