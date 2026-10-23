// FolderSearch — «Расширенный поиск» по папке (эталон 3.2, план 1.4): обход QDirIterator (рекурсивно, фильтр
// *.pdf) → новые и изменённые файлы (ключ — путь + mtime, core/text/index.h) извлекаются в своём пуле потоков
// (каждый файл — свой Document; слои без сегментации, SearchSession::textOptions) и пишутся в индекс FTS5
// state.sqlite → запрос фразой по индексу внутри папки → результаты по файлам: файл → страницы → контекст.
// Зашифрованный без пароля — пропускается с пометкой (encrypted), битый — failed; удалённые с диска файлы
// папки уходят из индекса. Прогресс — после каждого файла, отмена — между файлами и страницами (найденное к
// этому моменту в индексе остаётся).
//
// run — блокирующий (тесты, задачи), start — в фоне (QtConcurrent::run). Сигналы испускаются из потока
// поиска: получателям в GUI-потоке они приходят QueuedConnection.
#pragma once

#include <QFuture>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <atomic>

namespace ot {

class StateDb;

struct FolderSearchOptions {
    bool recursive = true;
    QStringList nameFilters{QStringLiteral("*.pdf")};
    int threads = 0;  // 0 — idealThreadCount() − 2 (не меньше 1)
};

struct FolderPageHit {
    int page = 0;
    QString snippet;  // контекст: совпадения между U+0002 и U+0003, обрезка — «…»
};

struct FolderFileHits {
    QString path;  // абсолютный, с «/»
    QVector<FolderPageHit> pages;
};

struct FolderSearchProgress {
    int filesDone = 0;   // проверено (свежие из индекса и извлечённые)
    int filesTotal = 0;  // найдено по фильтру
    QString file;        // последний обработанный
};

struct FolderSearchResult {
    QVector<FolderFileHits> files;
    QStringList encrypted;  // пропущены: нужен пароль
    QStringList failed;     // не открылись
    int scanned = 0;        // файлов по фильтру
    int extracted = 0;      // извлечено заново (новые и изменённые)
    bool cancelled = false;

    int hitCount() const;  // страниц с совпадениями
};

class FolderSearch : public QObject {
    Q_OBJECT
public:
    explicit FolderSearch(StateDb& db, QObject* parent = nullptr);

    FolderSearchResult run(const QString& folder, const QString& query,
                           const FolderSearchOptions& options = {});
    QFuture<FolderSearchResult> start(const QString& folder, const QString& query,
                                      const FolderSearchOptions& options = {});
    // Любой поток: остановить текущий поиск (run вернёт cancelled).
    void cancel() noexcept { cancel_ = true; }
    bool isCancelled() const noexcept { return cancel_.load(); }

signals:
    void progress(const ot::FolderSearchProgress& progress);
    void fileFound(const ot::FolderFileHits& hits);
    void finished(const ot::FolderSearchResult& result);

private:
    StateDb& db_;
    std::atomic<bool> cancel_{false};
};

}  // namespace ot

Q_DECLARE_METATYPE(ot::FolderSearchProgress)
Q_DECLARE_METATYPE(ot::FolderFileHits)
Q_DECLARE_METATYPE(ot::FolderSearchResult)
