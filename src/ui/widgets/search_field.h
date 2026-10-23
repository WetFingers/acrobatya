// Поле поиска (эталон 6.1: «Поиск» глобальной панели, панель «Найти»): иконка-лупа, крестик очистки, Enter —
// искать дальше, Shift+Enter — назад, Esc — очистить.
#pragma once

#include <QLineEdit>

class QAction;

namespace ot::ui {

class SearchField : public QLineEdit {
    Q_OBJECT
public:
    explicit SearchField(QWidget* parent = nullptr);

signals:
    void searchRequested(const QString& text);
    void previousRequested(const QString& text);
    void cleared();

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void applyMetrics();

    QAction* clear_ = nullptr;
};

}  // namespace ot::ui
