// «Предупреждения документа» перед подписью (план 4.6, п. 1; эталон 3.8 «Диалог подписания» — «Проверить
// документ»; sign-17): список того, что может изменить вид или поведение документа после подписи (core/sign/
// warnings_scan.h) — уровень, что найдено, где, подробность; «Просмотреть» — перейти к странице выбранного
// (сигнал reviewRequested). Ничего не найдено — «Предупреждений нет».
#pragma once

#include <QDialog>

#include "core/sign/warnings_scan.h"

class QLabel;
class QPushButton;
class QTreeWidget;

namespace ot::ui {

class WarningsDialog : public QDialog {
    Q_OBJECT
public:
    explicit WarningsDialog(sign::WarningScan scan, QWidget* parent = nullptr);

    QTreeWidget* list() const { return list_; }
    QLabel* summaryLabel() const { return summary_; }
    QPushButton* reviewButton() const { return review_; }
    const sign::WarningScan& scan() const { return scan_; }
    // Страница выбранного предупреждения (−1 — о документе или не выбрано).
    int selectedPage() const;

signals:
    void reviewRequested(int page);

protected:
    void changeEvent(QEvent* event) override;

private:
    void fill();
    void retranslate();

    sign::WarningScan scan_;
    QLabel* summary_ = nullptr;
    QTreeWidget* list_ = nullptr;
    QPushButton* review_ = nullptr;
    QPushButton* close_ = nullptr;
};

}  // namespace ot::ui
