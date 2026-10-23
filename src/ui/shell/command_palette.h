// Палитра команд (Ctrl+Shift+P; эталон 6.3, 6.8; view-32): поле фильтра над списком команд реестра окна
// (services/commands.h) — название, клавиша справа, заглушки будущих этапов и недоступные — серые. Фильтр —
// по подстроке без учёта регистра в названии (без «&» и «…») и id, каждое слово ввода должно найтись; начало
// названия — выше. ↑ ↓ PgUp PgDn — по списку не уходя из поля, Enter или двойной щелчок — выполнить и
// закрыть, Esc — закрыть. Окно без рамки у верхнего края окна программы; открывается open() — не блокирует
// цикл событий
// (--script, тесты).
#pragma once

#include <QDialog>
#include <QStringList>

class QListWidget;

namespace ot {
class CommandRegistry;
struct Command;
}  // namespace ot

namespace ot::ui {

class SearchField;

class CommandPalette : public QDialog {
    Q_OBJECT
public:
    explicit CommandPalette(CommandRegistry& commands, QWidget* parent = nullptr);

    SearchField* filterField() const { return filter_; }
    QListWidget* list() const { return list_; }

    // Перечитать команды из реестра и показать по фильтру (поле очищается).
    void reset();
    void setFilter(const QString& text);
    QStringList visibleIds() const;  // по порядку списка
    QString currentId() const;
    // Выполнить выбранную команду и закрыть; false — нет выбранной или она недоступна (палитра остаётся).
    bool runCurrent();

    // Подходит ли команда под фильтр (пусто — любая): rank 0 — название начинается с фильтра, 1 — каждое
    // слово фильтра есть в названии, 2 — какое-то слово нашлось только в id.
    static bool matches(const Command& command, const QString& filter, int* rank = nullptr);
    static QString plainTitle(const QString& title);  // без «&» и «…»

signals:
    void commandTriggered(const QString& id);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void changeEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    void rebuild();
    void retranslate();
    void applyMetrics();
    void placeOverParent();

    CommandRegistry& commands_;
    SearchField* filter_ = nullptr;
    QListWidget* list_ = nullptr;
};

}  // namespace ot::ui
