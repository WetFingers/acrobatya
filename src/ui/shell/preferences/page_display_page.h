// «Установки → Просмотр страницы» (эталон 3.18, 3.1 «Сглаживание и качество»): раскладка и масштаб по
// умолчанию, сглаживание текста, изображений и векторной графики, тонкие линии, логические номера страниц,
// рамки областей. С 2.4b живы логические номера (строка состояния, миниатюры); действие остальных ключей —
// сессия 2.5b («Настройки „Просмотр страницы“ целиком»), они уже пишутся в settings.ini.
#pragma once

#include <QList>

#include "ui/shell/preferences/preferences_page.h"

class QCheckBox;
class QLabel;
class QComboBox;

namespace ot::ui {

class PropertySheet;

class PageDisplayPage : public PreferencesPage {
    Q_OBJECT
public:
    explicit PageDisplayPage(QWidget* parent = nullptr);

    QString categoryId() const override { return QStringLiteral("pageDisplay"); }
    QString title() const override;
    void load(const Settings& settings) override;
    void apply(Settings& settings) override;

    QComboBox* layoutBox() const { return layout_; }
    QComboBox* zoomBox() const { return zoom_; }
    QCheckBox* smoothTextBox() const { return smoothText_; }
    QCheckBox* logicalNumbersBox() const { return logical_; }

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslate();

    PropertySheet* sheet_ = nullptr;
    QList<QLabel*> sections_;  // подписи разделов и строк листа — в порядке добавления
    QList<QLabel*> rows_;
    QComboBox* layout_ = nullptr;
    QComboBox* zoom_ = nullptr;
    QCheckBox* smoothText_ = nullptr;
    QCheckBox* smoothImages_ = nullptr;
    QCheckBox* smoothLineArt_ = nullptr;
    QCheckBox* thinLines_ = nullptr;
    QCheckBox* logical_ = nullptr;
    QCheckBox* boxes_ = nullptr;
};

}  // namespace ot::ui
