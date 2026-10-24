// «Метка времени документа» (план 4.6, п. 1; эталон 3.8 «Инструмент», «LTV и B-LTA»; sign-20 — UI): что это
// (доказательство, что документ в таком виде существовал в момент метки; удостоверение не нужно, подписи не
// задевает), служба времени из «Серверов времени» (по умолчанию — DigiCert), «Добавить метку». Список пуст —
// кнопка серая и подсказка, где добавить сервер (сигнал serversRequested).
#pragma once

#include <QDialog>

#include <optional>

#include "core/sign/tsa_servers.h"

class QComboBox;
class QLabel;
class QPushButton;

namespace ot::ui {

class TimestampDialog : public QDialog {
    Q_OBJECT
public:
    explicit TimestampDialog(QWidget* parent = nullptr);

    QComboBox* serverBox() const { return server_; }
    QPushButton* addButton() const { return add_; }
    QPushButton* serversButton() const { return servers_; }
    QLabel* noteLabel() const { return note_; }
    std::optional<sign::TsaServer> server() const;

signals:
    void serversRequested();

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslate();

    sign::TsaServers list_;
    QLabel* intro_ = nullptr;
    QLabel* serverLabel_ = nullptr;
    QComboBox* server_ = nullptr;
    QPushButton* servers_ = nullptr;
    QLabel* note_ = nullptr;
    QPushButton* add_ = nullptr;
    QPushButton* cancel_ = nullptr;
};

}  // namespace ot::ui
