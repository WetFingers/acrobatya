// «Установки → Общие» (эталон 3.18; view-25 — тема, ocr-42 — «Крупные элементы управления»): язык интерфейса,
// тема оформления, крупные элементы, что открывать при запуске, однобуквенные ускорители инструментов,
// «Очистить список недавних файлов». Тема, масштаб и язык применяются окном без перезапуска (applied).
#pragma once

#include <QList>

#include "ui/shell/preferences/preferences_page.h"

class QCheckBox;
class QLabel;
class QComboBox;
class QPushButton;

namespace ot::ui {

class PropertySheet;

class GeneralPage : public PreferencesPage {
    Q_OBJECT
public:
    explicit GeneralPage(QWidget* parent = nullptr);

    QString categoryId() const override { return QStringLiteral("general"); }
    QString title() const override;
    void load(const Settings& settings) override;
    void apply(Settings& settings) override;

    QComboBox* languageBox() const { return language_; }
    QComboBox* themeBox() const { return theme_; }
    QCheckBox* largeElementsBox() const { return large_; }
    QComboBox* startupBox() const { return startup_; }
    QCheckBox* singleKeyBox() const { return singleKey_; }
    QPushButton* clearRecentButton() const { return clearRecent_; }

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslate();

    PropertySheet* sheet_ = nullptr;
    QList<QLabel*> sections_;  // подписи разделов и строк листа — в порядке добавления
    QList<QLabel*> rows_;
    QComboBox* language_ = nullptr;
    QComboBox* theme_ = nullptr;
    QCheckBox* large_ = nullptr;
    QComboBox* startup_ = nullptr;
    QCheckBox* singleKey_ = nullptr;
    QPushButton* clearRecent_ = nullptr;
};

}  // namespace ot::ui
