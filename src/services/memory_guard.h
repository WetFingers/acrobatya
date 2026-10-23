// MemoryGuard — «мало памяти» (эталон 4.1): когда программа уходит в фон (ApplicationDeactivate, состояние
// Qt::ApplicationInactive) и после простоя (60 с без ввода), — relieve(): общий store MuPDF сжимается до 50 %
// предела (fz_shrink_store), кеш тайлов (если подключён) — до половины бюджета, сигнал lowMemory для прочих
// кешей. Ввод пользователя (клавиши, мышь, колесо, касания) перезапускает таймер простоя: фильтр событий
// ставится на QCoreApplication сам. Объект живёт в GUI-потоке.
#pragma once

#include <QObject>
#include <QTimer>

namespace ot {

class TileCache;

class MemoryGuard : public QObject {
    Q_OBJECT
public:
    static constexpr int kDefaultIdleMs = 60000;
    static constexpr unsigned int kShrinkPercent = 50;

    explicit MemoryGuard(QObject* parent = nullptr, int idleMs = kDefaultIdleMs);
    ~MemoryGuard() override;

    // Кеш тайлов, который сжимать вместе со store (nullptr — не сжимать).
    void setTileCache(TileCache* cache) noexcept { tiles_ = cache; }
    void setIdleInterval(int ms);
    int idleInterval() const { return idle_.interval(); }
    // Сколько раз сработало (для тестов и журнала).
    int relieveCount() const noexcept { return relieved_; }

public slots:
    // Освободить память сейчас.
    void relieve();
    // Был ввод пользователя: простой считается заново.
    void noteActivity();

signals:
    void lowMemory();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QTimer idle_;
    TileCache* tiles_ = nullptr;
    int relieved_ = 0;
};

}  // namespace ot
