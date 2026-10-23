#include "services/search_ops.h"

#include <QRegularExpression>

#include <spdlog/spdlog.h>

#include <exception>
#include <string>

#include "core/doc/document.h"
#include "core/links/attachments.h"
#include "core/links/outlines.h"
#include "core/text/stext.h"
#include "services/document_session.h"
#include "services/task_context.h"

namespace ot::search {

namespace {

bool isCancelled(const std::shared_ptr<Control>& control) {
    return control != nullptr && control->cancelled.load();
}

// Поиск по страницам одного документа: source даёт слой страницы.
Result searchPages(int pageCount, const Request& request, const SearchSession::PageSource& source,
                   const std::shared_ptr<Control>& control, const ProgressFn& progress,
                   const std::function<bool()>& aborted) {
    Result r;
    r.pageCount = pageCount;
    SearchSession s(request.needle.toStdString(), request.options);
    std::size_t reported = 0;
    std::vector<std::shared_ptr<const TextPage>> layers(static_cast<std::size_t>(pageCount));
    s.run(
        pageCount,
        [&](int page) {
            std::shared_ptr<const TextPage> layer = source(page);
            if (layer != nullptr && layer->hasText()) {
                ++r.pagesWithText;
            }
            if (request.snippets) {
                layers[static_cast<std::size_t>(page)] = layer;
            }
            return layer;
        },
        [&](const SearchProgress& p) {
            if (progress) {
                const std::vector<SearchHit>& all = s.hits();
                std::vector<SearchHit> fresh(all.begin() + static_cast<std::ptrdiff_t>(reported), all.end());
                reported = all.size();
                progress(p, fresh);
            }
        },
        [&] { return isCancelled(control) || (aborted && aborted()); });
    r.hits = s.hits();
    r.pagesDone = s.pagesDone();
    r.aborted = s.aborted();
    if (request.snippets) {
        for (const SearchHit& h : r.hits) {
            const auto& layer = layers[static_cast<std::size_t>(h.page)];
            r.snippets << (layer != nullptr
                               ? QString::fromStdString(searchSnippet(*layer, h, request.snippetRadius))
                               : QString());
        }
    }
    return r;
}

void collectBookmarks(Document& doc, const std::vector<int>& path, const Request& request,
                      QVector<BookmarkHit>& out, int depth) {
    if (depth > 64) {
        return;  // испорченное дерево с петлёй
    }
    for (const outline::Item& item : outline::children(doc, path)) {
        const QString title = QString::fromStdString(item.title);
        if (matches(title, request)) {
            BookmarkHit hit;
            hit.title = title;
            hit.page = item.action.kind == links::LinkKind::GoTo ? item.action.page : -1;
            for (const int i : item.path) {
                hit.path << i;
            }
            out << hit;
        }
        if (item.hasChildren) {
            collectBookmarks(doc, item.path, request, out, depth + 1);
        }
    }
}

}  // namespace

bool matches(const QString& text, const Request& request) {
    if (request.needle.isEmpty() || text.isEmpty()) {
        return false;
    }
    QString pattern = request.options.regex ? request.needle : QRegularExpression::escape(request.needle);
    if (request.options.wholeWord) {
        pattern = QStringLiteral("(?<![\\w])(?:%1)(?![\\w])").arg(pattern);
    }
    QRegularExpression::PatternOptions flags = QRegularExpression::UseUnicodePropertiesOption;
    if (!request.options.caseSensitive) {
        flags |= QRegularExpression::CaseInsensitiveOption;
    }
    const QRegularExpression re(pattern, flags);
    return re.isValid() && re.match(text).hasMatch();
}

namespace {

Result runIn(TaskContext& tc, const Request& request, const std::shared_ptr<Control>& control,
             const ProgressFn& progress) {
    if (isCancelled(control)) {
        Result r;
        r.pageCount = tc.document().pageCount();
        r.aborted = true;
        return r;
    }
    return searchPages(
        tc.document().pageCount(), request,
        [&tc](int page) { return tc.textPage(page, SearchSession::textOptions()); }, control, progress,
        [&tc] { return tc.aborted(); });
}

QVector<BookmarkHit> bookmarksIn(TaskContext& tc, const Request& request) {
    QVector<BookmarkHit> out;
    collectBookmarks(tc.document(), {}, request, out, 0);
    return out;
}

QVector<AttachmentHits> attachmentsIn(TaskContext& tc, const Request& request,
                                      const std::shared_ptr<Control>& control) {
    QVector<AttachmentHits> out;
    Document& doc = tc.document();
    for (const attachments::Attachment& a : attachments::list(doc)) {
        if (isCancelled(control) || tc.aborted()) {
            break;
        }
        if (!a.embedded || !attachments::isPdf(a.fileName)) {
            continue;
        }
        try {
            OpenResult opened = Document::openMemory(attachments::contents(doc, a), ".pdf");
            if (!opened.ok || opened.document == nullptr) {
                continue;  // битое или зашифрованное вложение
            }
            Document& inner = *opened.document;
            Result r = searchPages(
                inner.pageCount(), request,
                [&inner](int page) { return TextPage::build(inner, page, SearchSession::textOptions()); },
                control, {}, [&tc] { return tc.aborted(); });
            if (!r.hits.empty()) {
                out << AttachmentHits{QString::fromStdString(a.fileName), std::move(r.hits),
                                      std::move(r.snippets)};
            }
        } catch (const std::exception& e) {
            spdlog::warn("search: attachment {} skipped: {}", a.fileName, e.what());
        }
    }
    return out;
}

}  // namespace

QFuture<Result> run(DocumentSession& session, const Request& request, std::shared_ptr<Control> control,
                    ProgressFn progress) {
    return session.post(
        [request, control, progress](TaskContext& tc) { return runIn(tc, request, control, progress); },
        TaskPriority::Normal, QStringLiteral("Search"));
}

QFuture<QVector<BookmarkHit>> bookmarks(DocumentSession& session, const Request& request) {
    return session.post([request](TaskContext& tc) { return bookmarksIn(tc, request); }, TaskPriority::Normal,
                        QStringLiteral("Search bookmarks"));
}

QFuture<QVector<AttachmentHits>> attachments(DocumentSession& session, const Request& request,
                                             std::shared_ptr<Control> control) {
    return session.post([request, control](TaskContext& tc) { return attachmentsIn(tc, request, control); },
                        TaskPriority::Normal, QStringLiteral("Search attachments"));
}

QFuture<DocumentResults> runAll(DocumentSession& session, const Request& request, bool withBookmarks,
                                bool withAttachments, std::shared_ptr<Control> control) {
    return session.post(
        [request, withBookmarks, withAttachments, control](TaskContext& tc) {
            DocumentResults out;
            out.text = runIn(tc, request, control, {});
            if (withBookmarks && !isCancelled(control)) {
                out.bookmarks = bookmarksIn(tc, request);
            }
            if (withAttachments && !isCancelled(control)) {
                out.attachments = attachmentsIn(tc, request, control);
            }
            return out;
        },
        TaskPriority::Normal, QStringLiteral("Advanced search"));
}

}  // namespace ot::search
