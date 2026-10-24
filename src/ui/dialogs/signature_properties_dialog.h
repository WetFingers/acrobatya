// «Свойства подписи» (план 4.6, п. 2; эталон 3.8 «Панель «Подписи»», 6.3; sign-27): итог проверки и вкладки
// как в Acrobat — «Сводка» (статус, причины проверки, кто и когда, «Показать сертификат подписанта…»,
// «Проверить подпись»), «Документ» (ревизия, покрывает ли подпись весь файл, разрешённые изменения DocMDP и
// FieldMDP, изменения после подписи, «Просмотреть подписанную версию»), «Подписант» (сертификат, цепочка,
// доверие, отзыв, «LTV включено»), «Дата и время» (заявленное время /M, метка времени и служба, время
// проверки и его источник). Данные — SignatureInfo из проверки вкладки; действия — сигналами.
#pragma once

#include <QDialog>

#include "core/sign/verifier.h"

class QLabel;
class QPushButton;
class QTabWidget;
class QTreeWidget;

namespace ot::ui {

class SignaturePropertiesDialog : public QDialog {
    Q_OBJECT
public:
    enum Tab { Summary, Document, Signer, DateTime };

    SignaturePropertiesDialog(sign::SignatureInfo signature, int versions, QWidget* parent = nullptr);

    const sign::SignatureInfo& signature() const { return sig_; }
    QTabWidget* tabs() const { return tabs_; }
    void showTab(Tab tab);
    QLabel* verdictLabel() const { return verdict_; }
    QTreeWidget* findingsTree() const { return findings_; }
    QLabel* documentLabel() const { return document_; }
    QTreeWidget* changesTree() const { return changes_; }
    QLabel* signerLabel() const { return signer_; }
    QLabel* timeLabel() const { return time_; }
    QPushButton* certificateButton() const { return certificate_; }
    QPushButton* verifyButton() const { return verify_; }
    QPushButton* signedVersionButton() const { return signedVersion_; }

signals:
    void certificateRequested();
    void verifyRequested();
    void signedVersionRequested();

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslate();

    sign::SignatureInfo sig_;
    int versions_ = 0;
    QTabWidget* tabs_ = nullptr;
    QLabel* icon_ = nullptr;
    QLabel* verdict_ = nullptr;
    QLabel* who_ = nullptr;
    QTreeWidget* findings_ = nullptr;
    QLabel* document_ = nullptr;
    QTreeWidget* changes_ = nullptr;
    QLabel* signer_ = nullptr;
    QLabel* time_ = nullptr;
    QPushButton* certificate_ = nullptr;
    QPushButton* verify_ = nullptr;
    QPushButton* signedVersion_ = nullptr;
    QPushButton* close_ = nullptr;
};

}  // namespace ot::ui
