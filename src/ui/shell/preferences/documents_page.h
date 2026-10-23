// «Установки → Документы» (эталон 3.18, 4.5): восстанавливать последний вид при повторном открытии (живо с
// 2.3), автосохранение и его интервал 1–30 мин, предложение восстановить несохранённое при запуске, PDF/A —
// только чтение. Автосохранение и восстановление в окне и режим PDF/A подключает сессия 2.5 (ключи уже
// здесь).
#pragma once

#include <QList>

#include "ui/shell/preferences/preferences_page.h"

class QCheckBox;
class QLabel;

namespace ot::ui {

class NumberField;
class PropertySheet;

class DocumentsPage : public PreferencesPage {
    Q_OBJECT
public:
    explicit DocumentsPage(QWidget* parent = nullptr);

    QString categoryId() const override { return QStringLiteral("documents"); }
    QString title() const override;
    void load(const Settings& settings) override;
    void apply(Settings& settings) override;

    QCheckBox* restoreViewBox() const { return restoreView_; }
    QCheckBox* autosaveBox() const { return autosave_; }
    NumberField* autosaveMinutesField() const { return minutes_; }

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslate();

    PropertySheet* sheet_ = nullptr;
    QList<QLabel*> sections_;  // подписи разделов и строк листа — в порядке добавления
    QList<QLabel*> rows_;
    QCheckBox* restoreView_ = nullptr;
    QCheckBox* autosave_ = nullptr;
    NumberField* minutes_ = nullptr;
    QCheckBox* recovery_ = nullptr;
    QCheckBox* pdfa_ = nullptr;
};

}  // namespace ot::ui
