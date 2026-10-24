// Панель «Подписи» справа (план 4.6, п. 2; эталон 3.8 «Панель «Подписи»», 6.1, 2 — сценарий 5; sign-25,
// sign-26, sign-27, sign-28, sign-22): итог проверки вкладки (ui/shell/signature_status.h) деревом — «Подпись
// 1: Иванов И. (действительна)» со значком статуса, под ней — статус и причины, подписант, время и его
// источник, метка времени, причина и место, формат и уровень PAdES (B-B/T/LT/LTA), LTV, разрешённые
// изменения, изменения после подписи, поле и страница; пустые поля подписи — отдельными строками
// («Подписать поле…»). Сверху — «Проверить все» и «Параметры ▾»; меню подписи: перейти к полю, проверить
// подпись и все, «Просмотреть подписанную версию» (байты подписанной ревизии → вкладка «только чтение»),
// «Добавить сведения для проверки» (/DSS), «Очистить подпись» (последняя, не заблокированная), «Свойства
// подписи…», «Показать сертификат подписанта…». Подписей нет — пустое состояние.
#pragma once

#include <QPointer>
#include <QString>

#include <functional>

#include "core/sign/verifier.h"
#include "ui/panes/nav_page.h"

class QLabel;
class QMenu;
class QPushButton;
class QStackedWidget;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace ot::ui {

class CertificateViewer;
class EmptyState;
class SignaturePropertiesDialog;

class SignaturesPane : public NavPage {
    Q_OBJECT
public:
    // Ответ на «Очистить подпись?» (тесты); по умолчанию — вопрос.
    using Confirm = std::function<bool(const QString& field)>;

    explicit SignaturesPane(QWidget* parent = nullptr);

    QString paneId() const override { return QStringLiteral("signatures"); }
    QString title() const override;
    void setDocument(DocumentView* view) override;
    void activated() override;

    QTreeWidget* tree() const { return tree_; }
    QPushButton* verifyButton() const { return verify_; }
    QToolButton* optionsButton() const { return options_; }
    QLabel* summaryLabel() const { return summary_; }
    bool isEmptyShown() const;
    // Строка поля (подписанного или пустого); nullptr — нет.
    QTreeWidgetItem* itemFor(const QString& field) const;
    // Текст строки и всех её подробностей (тесты).
    QString itemText(const QString& field) const;
    // Меню строки (пункты — по objectName: sigGoTo, sigVerify, sigVerifyAll, sigSignedVersion, sigAddLtv,
    // sigClear, sigProperties, sigCertificate, sigSignField); владеет вызывающий.
    QMenu* menuFor(const QString& field);

    void setConfirm(Confirm confirm) { confirm_ = std::move(confirm); }

    // Действия меню.
    void goToField(const QString& field);
    void verifyAll();
    SignaturePropertiesDialog* showProperties(const QString& field);
    CertificateViewer* showCertificate(const QString& field);
    void viewSignedVersion(const QString& field);
    void addValidationInfo();
    void clearSignature(const QString& field);
    void signField(const QString& field);
    // Можно ли очистить подпись поля (последняя, поле не только для чтения); причина — в why.
    bool canClear(const QString& field, QString* why = nullptr) const;

signals:
    void rowsChanged();
    // Действие с записью закончилось (сведения LTV, очистка): ok и сообщение.
    void actionFinished(bool ok, const QString& message);

protected:
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void fill();
    void retranslate();
    void addSignature(const sign::SignatureInfo& sig);
    QString fieldOf(const QTreeWidgetItem* item) const;
    void notify(bool ok, const QString& message);

    QPointer<DocumentView> view_;
    QStackedWidget* stack_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    EmptyState* empty_ = nullptr;
    QLabel* summary_ = nullptr;
    QPushButton* verify_ = nullptr;
    QToolButton* options_ = nullptr;
    QMenu* optionsMenu_ = nullptr;
    Confirm confirm_;
    bool dirty_ = true;
    std::vector<QMetaObject::Connection> connections_;
};

}  // namespace ot::ui
