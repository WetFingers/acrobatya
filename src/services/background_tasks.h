// Фоновые задачи для строки состояния (план 2.4; эталон 6.1 «Строка состояния», 6.6 «фоновые задачи —
// индикатор в строке состояния, отмена»; никаких модальных «подождите»): кто запускает долгую работу
// (сохранение как PDF, OCR 8.1, конвертация 7.2, сохранение 2.5), регистрирует её здесь — название (строку
// ui), прогресс 0–100 (−1 — неизвестен) и отмену; строка состояния показывает первую и число остальных, «×»
// зовёт отмену. Только из GUI-потока.
#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include <functional>

namespace ot {

class BackgroundTasks : public QObject {
    Q_OBJECT
public:
    struct Task {
        quint64 id = 0;
        QString title;
        int progress = -1;  // 0–100; −1 — неизвестен (бегущая полоса)
        std::function<void()> cancel;
    };

    static BackgroundTasks& shared();

    // Новая задача; cancel пусто — отменить нельзя.
    quint64 start(const QString& title, std::function<void()> cancel = {});
    void setProgress(quint64 id, int percent);
    void setTitle(quint64 id, const QString& title);
    void finish(quint64 id);
    // Отменить: зовёт cancel задачи (снимает её finish самого владельца).
    void cancel(quint64 id);
    QList<Task> tasks() const { return tasks_; }
    bool isEmpty() const { return tasks_.isEmpty(); }

signals:
    void changed();

private:
    QList<Task> tasks_;
    quint64 next_ = 1;
};

}  // namespace ot
