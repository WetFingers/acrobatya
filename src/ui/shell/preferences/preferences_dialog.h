// Диалог «Установки» (Ctrl+K; эталон 3.18, 6.3; view-25, ocr-42): категории слева в порядке эталона 3.18,
// справа страница категории с заголовком, внизу «ОК» / «Отмена». Живые с 2.4b — «Общие», «Просмотр страницы»,
// «Документы»; остальные — заглушки с версией, в которой появятся (сессии-владельцы ставят свои страницы).
// «ОК» пишет все страницы в settings.ini и сигналит applied — окно применяет тему, язык и клавиши без
// перезапуска; «Отмена» и Esc ничего не пишут. Диалог немодальный для приложения (open(), окно-модальный):
// --script и тесты не зависают на exec().
#pragma once

#include <QDialog>
#include <QStringList>

#include <vector>

class QDialogButtonBox;
class QLabel;
class QListWidget;
class QStackedWidget;

namespace ot::ui {

class PreferencesPage;

class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesDialog(QWidget* parent = nullptr);

    QStringList categoryIds() const;
    PreferencesPage* page(const QString& categoryId) const;
    QString currentCategory() const;
    bool showCategory(const QString& categoryId);  // false — нет такой категории
    QListWidget* categoryList() const { return list_; }
    QDialogButtonBox* buttons() const { return buttons_; }

    // Перечитать настройки во все страницы (при каждом показе диалога).
    void reload();
    // Записать все страницы в settings.ini и сигнал applied (то же делает «ОК»).
    void applyAll();

    void accept() override;

signals:
    void applied();

protected:
    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void addPage(PreferencesPage* page);
    void retranslate();
    void applyMetrics();

    QListWidget* list_ = nullptr;
    QLabel* title_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
    std::vector<PreferencesPage*> pages_;
};

}  // namespace ot::ui
