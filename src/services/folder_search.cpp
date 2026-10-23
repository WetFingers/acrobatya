#include "services/folder_search.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSet>
#include <QThread>
#include <QThreadPool>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <filesystem>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

#include "core/doc/document.h"
#include "core/text/index.h"
#include "core/text/search.h"
#include "core/text/stext.h"
#include "services/state_db.h"

namespace ot {

namespace {

struct Extracted {
    std::vector<std::string> pages;
    bool encrypted = false;
    bool failed = false;
    bool cancelled = false;
    std::string error;
};

// Текст всех страниц файла (слои без сегментации); битая страница — пустая.
Extracted extractFile(const QString& path, const std::atomic<bool>& cancel) {
    Extracted x;
    OpenResult opened = Document::open(std::filesystem::path(path.toStdWString()));
    if (opened.needsPassword) {
        x.encrypted = true;
        return x;
    }
    if (!opened.ok || !opened.document) {
        x.failed = true;
        x.error = opened.error.message;
        return x;
    }
    const Document& doc = *opened.document;
    int pages = 0;
    try {
        pages = doc.pageCount();
    } catch (const std::exception& e) {
        x.failed = true;
        x.error = e.what();
        return x;
    }
    x.pages.reserve(static_cast<std::size_t>(pages));
    for (int p = 0; p < pages; ++p) {
        if (cancel.load()) {
            x.cancelled = true;
            return x;
        }
        try {
            x.pages.push_back(TextPage::build(doc, p, SearchSession::textOptions())->text());
        } catch (const std::exception& e) {
            spdlog::warn("folder search: {} page {}: {}", path.toStdString(), p, e.what());
            x.pages.emplace_back();
        }
    }
    return x;
}

struct Entry {
    QString path;     // абсолютный, с «/»
    std::string key;  // он же в UTF-8 — ключ индекса
    qint64 mtime = 0;
};

}  // namespace

int FolderSearchResult::hitCount() const {
    int n = 0;
    for (const FolderFileHits& f : files) {
        n += static_cast<int>(f.pages.size());
    }
    return n;
}

FolderSearch::FolderSearch(StateDb& db, QObject* parent) : QObject(parent), db_(db) {
    qRegisterMetaType<ot::FolderSearchProgress>();
    qRegisterMetaType<ot::FolderFileHits>();
    qRegisterMetaType<ot::FolderSearchResult>();
}

QFuture<FolderSearchResult> FolderSearch::start(const QString& folder, const QString& query,
                                                const FolderSearchOptions& options) {
    return QtConcurrent::run([this, folder, query, options] { return run(folder, query, options); });
}

FolderSearchResult FolderSearch::run(const QString& folder, const QString& query,
                                     const FolderSearchOptions& options) {
    cancel_ = false;
    FolderSearchResult result;
    if (!db_.isOpen()) {
        result.cancelled = true;
        emit finished(result);
        return result;
    }
    QString root = QDir::cleanPath(QFileInfo(folder).absoluteFilePath());
    if (!root.endsWith(QLatin1Char('/'))) {
        root += QLatin1Char('/');
    }
    const std::string prefix = root.toStdString();

    // 1. Обход папки.
    std::vector<Entry> entries;
    {
        QDirIterator it(root, options.nameFilters, QDir::Files,
                        options.recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
        while (it.hasNext() && !cancel_.load()) {
            const QFileInfo info = it.nextFileInfo();
            Entry e;
            e.path = QDir::cleanPath(info.absoluteFilePath());
            e.key = e.path.toStdString();
            e.mtime = info.lastModified().toMSecsSinceEpoch();
            entries.push_back(std::move(e));
        }
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) { return a.key < b.key; });
    result.scanned = static_cast<int>(entries.size());
    const int total = result.scanned;

    // 2. Что извлекать: новые и изменённые (ключ — путь + mtime).
    std::vector<int> stale;
    {
        const std::lock_guard lock(db_.mutex());
        const TextIndex index(db_.handle());
        for (int i = 0; i < total; ++i) {
            if (!index.isFresh(entries[i].key, entries[i].mtime)) {
                stale.push_back(i);
            }
        }
    }
    std::atomic<int> done{total - static_cast<int>(stale.size())};
    emit progress({done.load(), total, QString()});

    // 3. Извлечение в своём пуле; запись в индекс — под замком подключения.
    std::mutex resultMutex;
    {
        QThreadPool pool;
        const int threads =
            options.threads > 0 ? options.threads : std::max(1, QThread::idealThreadCount() - 2);
        pool.setMaxThreadCount(threads);
        for (const int i : stale) {
            pool.start([this, &entries, &result, &resultMutex, &done, total, i] {
                if (cancel_.load()) {
                    return;
                }
                const Entry& e = entries[i];
                Extracted x = extractFile(e.path, cancel_);
                if (x.cancelled) {
                    return;
                }
                if (!x.failed) {
                    try {
                        const std::lock_guard lock(db_.mutex());
                        TextIndex index(db_.handle());
                        if (x.encrypted) {
                            index.markEncrypted(e.key, e.mtime);
                        } else {
                            index.upsert(e.key, e.mtime, x.pages);
                        }
                    } catch (const std::exception& err) {
                        x.failed = true;
                        x.error = err.what();
                    }
                }
                {
                    const std::lock_guard lock(resultMutex);
                    ++result.extracted;
                    if (x.failed) {
                        spdlog::warn("folder search: {} skipped: {}", e.key, x.error);
                        result.failed.push_back(e.path);
                    }
                }
                emit progress({++done, total, e.path});
            });
        }
        pool.waitForDone();
    }
    result.cancelled = cancel_.load();
    result.failed.sort();

    // 4. Запрос по индексу внутри папки; удалённые с диска — из индекса (если обход был полным).
    std::vector<IndexHit> hits;
    std::vector<std::string> encrypted;
    QSet<QString> walked;
    for (const Entry& e : entries) {
        walked.insert(e.path);
    }
    try {
        const std::lock_guard lock(db_.mutex());
        TextIndex index(db_.handle());
        if (!result.cancelled) {
            for (const std::string& path : index.files(prefix)) {
                const QString p = QString::fromStdString(path);
                const bool inScope = options.recursive || !p.mid(root.size()).contains(QLatin1Char('/'));
                if (inScope && !walked.contains(p)) {
                    index.remove(path);
                }
            }
        }
        hits = index.query(query.toStdString(), prefix);
        encrypted = index.encryptedFiles(prefix);
    } catch (const std::exception& e) {
        spdlog::warn("folder search: index query failed: {}", e.what());
    }
    for (const std::string& path : encrypted) {
        const QString p = QString::fromStdString(path);
        if (walked.contains(p)) {
            result.encrypted.push_back(p);
        }
    }
    for (const IndexHit& h : hits) {
        const QString p = QString::fromStdString(h.path);
        if (!walked.contains(p)) {
            continue;  // вне обхода (нерекурсивный поиск, отмена)
        }
        if (result.files.isEmpty() || result.files.back().path != p) {
            if (!result.files.isEmpty()) {
                emit fileFound(result.files.back());
            }
            result.files.push_back({p, {}});
        }
        result.files.back().pages.push_back({h.page, QString::fromStdString(h.snippet)});
    }
    if (!result.files.isEmpty()) {
        emit fileFound(result.files.back());
    }
    emit finished(result);
    return result;
}

}  // namespace ot
