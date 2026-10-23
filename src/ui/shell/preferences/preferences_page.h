// Страница категории диалога «Установки» (эталон 3.18, 6.3): читает настройки в свои поля (load) и пишет их
// по «ОК» (apply) через Settings (services/settings.h); действие на работающую программу (тема, язык,
// клавиши) окно применяет по сигналу PreferencesDialog::applied. Заглушка будущего этапа — stage() > 0
// (страница «появится в версии 0.N»; сессия-владелец заменяет её настоящей в PreferencesDialog::addPages).
#pragma once

#include <QComboBox>
#include <QString>
#include <QVariant>
#include <QWidget>

#include <cstddef>

namespace ot {
class Settings;
}

namespace ot::ui {

class PreferencesPage : public QWidget {
    Q_OBJECT
public:
    using QWidget::QWidget;

    virtual QString categoryId() const = 0;  // "general", "pageDisplay"… — группа ключей settings_keys.h
    virtual QString title() const = 0;       // название категории по русскому Acrobat (tr)
    virtual int stage() const { return 0; }  // > 0 — заглушка: появится в версии 0.stage
    virtual void load(const Settings& settings) { (void)settings; }
    virtual void apply(Settings& settings) { (void)settings; }

protected:
    // Пункты списка с данными — значениями ключа (подписи ставит retranslate страницы).
    template <std::size_t N>
    static void fillData(QComboBox* box, const char* const (&values)[N]) {
        for (const char* v : values) {
            box->addItem(QString(), QString::fromLatin1(v));
        }
    }
    // Выбрать пункт по данным; нет такого — первый.
    static void selectData(QComboBox* box, const QVariant& value) {
        const int index = box->findData(value);
        box->setCurrentIndex(index >= 0 ? index : 0);
    }
};

}  // namespace ot::ui
