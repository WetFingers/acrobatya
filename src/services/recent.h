// Недавние файлы (эталон 3.18 «Главная», 4.7; view-29, view-31): state.sqlite, таблица recent (миграция 3) —
// 50 последних открытых (закреплённые не вытесняются и идут первыми), избранное (звёздочка; живёт и вне
// недавних), последний вид файла (страница, масштаб, раскладка — окно восстанавливает его при открытии).
// Ключ — нормализованный путь (DocumentRegistry::pathKey: регистр и разделители Windows не различаются).
// Поток: GUI (сигнал changed); запросы — под StateDb::mutex().
#pragma once

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QString>

#include <optional>

namespace ot {

class StateDb;

struct LastView {
    int page = 0;            // с нуля
    double zoomPercent = 0;  // 0 — не сохранён
    QString layout;          // SinglePage | Continuous | Facing | FacingContinuous
    bool operator==(const LastView&) const = default;
};

struct RecentEntry {
    QString path;
    QDateTime openedAt;
    bool pinned = false;
    bool favorite = false;
    int pageCount = 0;
    LastView view;
};

class RecentFiles : public QObject {
    Q_OBJECT
public:
    static constexpr int kLimit = 50;

    static RecentFiles& shared();  // над StateDb::shared()
    explicit RecentFiles(StateDb& db, QObject* parent = nullptr);

    // Файл открыт: наверх недавних (pageCount — для «Кратких сведений»; 0 — прежнее).
    void touch(const QString& path, int pageCount = 0);
    // Закреплённые — первыми, затем по времени открытия, не больше kLimit.
    QList<RecentEntry> recent() const;
    QList<RecentEntry> favorites() const;
    std::optional<RecentEntry> find(const QString& path) const;

    void setPinned(const QString& path, bool pinned);
    void setFavorite(const QString& path, bool favorite);
    // «Убрать из списка»: из недавних (избранное остаётся).
    void remove(const QString& path);
    void clear();

    void saveView(const QString& path, const LastView& view);
    std::optional<LastView> lastView(const QString& path) const;

signals:
    void changed();

private:
    QList<RecentEntry> query(const char* where, const char* order) const;
    bool exec(const char* sql, const QString& path, std::initializer_list<qint64> numbers = {}) const;
    void trim();

    StateDb& db_;
};

}  // namespace ot
