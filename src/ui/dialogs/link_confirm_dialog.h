// «Предупреждение системы безопасности» перед ссылкой наружу (план 2.5b; эталон 7.4; security-42): адрес
// /URI — «Документ пытается открыть сайт https://…» [Открыть] [Разрешить для сайта] [Запретить]; «Разрешить»
// и «Запретить» запоминаются по хосту (решает вызывающий: services/link_policy.h LinkTrust), у адреса без
// хоста (mailto:) «Разрешить для сайта» нет; файл /GoToR — «Документ пытается открыть файл …» [Открыть]
// [Отмена]. Esc и «Отмена» — ничего не открывать и не запоминать.
#pragma once

#include <QDialog>
#include <QString>

class QLabel;
class QPushButton;

namespace ot::ui {

class LinkConfirmDialog : public QDialog {
    Q_OBJECT
public:
    enum class Kind { Url, File };
    enum class Choice { Cancel, Open, AllowSite, Block };

    // target — адрес (Url) или путь (File); host — сайт для «Разрешить для сайта» (пусто — кнопки нет).
    LinkConfirmDialog(Kind kind, const QString& target, const QString& host = {}, QWidget* parent = nullptr);

    Kind kind() const { return kind_; }
    Choice choice() const { return choice_; }
    QPushButton* openButton() const { return open_; }
    QPushButton* allowButton() const { return allow_; }  // nullptr у файла и адреса без хоста
    QPushButton* blockButton() const { return block_; }  // «Запретить» (адрес) или «Отмена» (файл)
    QLabel* targetLabel() const { return target_; }

    // Нажать кнопку выбора (тесты, сцены): Open / AllowSite / Block / Cancel.
    void choose(Choice choice);

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslate();

    Kind kind_;
    QString targetText_;
    QString host_;
    Choice choice_ = Choice::Cancel;
    QLabel* text_ = nullptr;
    QLabel* target_ = nullptr;
    QLabel* hint_ = nullptr;
    QPushButton* open_ = nullptr;
    QPushButton* allow_ = nullptr;
    QPushButton* block_ = nullptr;
};

}  // namespace ot::ui
