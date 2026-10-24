// «Серверы времени» — вкладка Установок → Подписи (план 4.6, п. 3; эталон 3.8 «Доверие» — «Серверы времени»;
// вопрос 12.2; sign-19): список служб RFC 3161 (по умолчанию — DigiCert, есть Sectigo и GlobalSign) со
// звёздочкой у службы по умолчанию, «Добавить…»/«Изменить…» (имя, адрес http/https, логин и пароль — пароль
// только на время работы программы), «Удалить», «По умолчанию», «Проверить» (тестовая метка в фоне — время
// ответа или причина), «ставить метку времени при подписи» (B-T) и что делать, если служба не ответила:
// спросить / подписать без метки / отменить. Пишется по «ОК» Установок (signatures/tsaServers и др.).
#pragma once

#include <QWidget>

#include <functional>
#include <optional>

#include "core/sign/tsa_servers.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QTreeWidget;

namespace ot {
class Settings;
}

namespace ot::ui {

class TsaWidget : public QWidget {
    Q_OBJECT
public:
    // «Добавить…»/«Изменить…»: служба из диалога (nullopt — отмена).
    using ServerPrompt =
        std::function<std::optional<sign::TsaServer>(QWidget* parent, const sign::TsaServer& initial)>;

    explicit TsaWidget(QWidget* parent = nullptr);

    void load(const Settings& settings);
    void apply(Settings& settings);

    const sign::TsaServers& servers() const { return servers_; }
    QTreeWidget* list() const { return list_; }
    QPushButton* addButton() const { return add_; }
    QPushButton* editButton() const { return edit_; }
    QPushButton* removeButton() const { return remove_; }
    QPushButton* defaultButton() const { return default_; }
    QPushButton* checkButton() const { return check_; }
    QLabel* checkLabel() const { return checkLabel_; }
    QCheckBox* timestampBox() const { return timestamp_; }
    QComboBox* fallbackBox() const { return fallback_; }

    void setServerPrompt(ServerPrompt prompt) { prompt_ = std::move(prompt); }

    bool select(const QString& name);
    QString selectedName() const;
    // Действия (и для тестов): false — не вышло (занятое имя, пустой адрес).
    bool addServer(const sign::TsaServer& server);
    bool updateSelected(const sign::TsaServer& server);
    bool removeSelected();
    bool setDefaultSelected();
    // «Проверить»: тестовая метка в фоне; итог — checkFinished и строка под списком.
    void checkSelected();

signals:
    void checkFinished(bool ok, const QString& message);

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslate();
    void fill(const QString& select = {});
    void syncButtons();
    std::optional<sign::TsaServer> ask(const sign::TsaServer& initial);

    sign::TsaServers servers_;
    ServerPrompt prompt_;
    bool checking_ = false;
    QTreeWidget* list_ = nullptr;
    QPushButton* add_ = nullptr;
    QPushButton* edit_ = nullptr;
    QPushButton* remove_ = nullptr;
    QPushButton* default_ = nullptr;
    QPushButton* check_ = nullptr;
    QLabel* checkLabel_ = nullptr;
    QCheckBox* timestamp_ = nullptr;
    QLabel* fallbackLabel_ = nullptr;
    QComboBox* fallback_ = nullptr;
};

}  // namespace ot::ui
