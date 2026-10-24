// «Установки → Редактирование содержимого» (план 3.4, п. 2; эталон 3.9, 3.18; edit-49, edit-02): шрифт и
// кегль нового текста («Добавить текст»), шрифт подстановки, когда шрифта документа нет в системе (правило 4
// FontResolver: «Выбрать автоматически» — Segoe UI или Times New Roman по засечкам), и системные шрифты в
// списке FORMAT TEXT. Список семейств — DirectWrite (services/system_fonts_dwrite.h).
#pragma once

#include <QList>

#include "ui/shell/preferences/preferences_page.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QSpinBox;

namespace ot::ui {

class PropertySheet;

class EditingPage : public PreferencesPage {
    Q_OBJECT
public:
    explicit EditingPage(QWidget* parent = nullptr);

    QString categoryId() const override { return QStringLiteral("editing"); }
    QString title() const override;
    void load(const Settings& settings) override;
    void apply(Settings& settings) override;

    QComboBox* defaultFontBox() const { return font_; }
    QSpinBox* defaultSizeBox() const { return size_; }
    QComboBox* fallbackBox() const { return fallback_; }
    QCheckBox* systemFontsBox() const { return system_; }

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslate();

    PropertySheet* sheet_ = nullptr;
    QComboBox* font_ = nullptr;
    QSpinBox* size_ = nullptr;
    QComboBox* fallback_ = nullptr;
    QCheckBox* system_ = nullptr;
    QLabel* hint_ = nullptr;
    QList<QLabel*> sections_;
    QList<QLabel*> rows_;
};

}  // namespace ot::ui
