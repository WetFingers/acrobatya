#include "services/read_state.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>

#include <mutex>

#include "services/sql.h"
#include "services/state_db.h"

namespace ot::comments {

ReadState::ReadState() : ReadState(StateDb::shared()) {}

ReadState::ReadState(StateDb& db) : db_(&db) {}

QString ReadState::documentKey(const std::string& idKey, const QString& path) {
    if (!idKey.empty()) {
        return QString::fromStdString(idKey);
    }
    if (path.isEmpty()) {
        return {};
    }
    const QString normal = QDir::cleanPath(QDir::fromNativeSeparators(path)).toCaseFolded();
    return QString::fromLatin1(QCryptographicHash::hash(normal.toUtf8(), QCryptographicHash::Sha256).toHex());
}

QString ReadState::commentKey(const annot::AnnotRef& ref) {
    if (!ref.nm.empty()) {
        return QString::fromStdString(ref.nm);
    }
    return QStringLiteral("#%1:%2").arg(ref.page).arg(ref.objnum);
}

void ReadState::setDocument(const QString& documentKey) {
    if (documentKey == doc_ && !doc_.isEmpty()) {
        return;
    }
    doc_ = documentKey;
    reload();
}

void ReadState::reload() {
    marks_.clear();
    if (doc_.isEmpty() || db_ == nullptr || !db_->isOpen()) {
        return;
    }
    std::lock_guard lock(db_->mutex());
    SqlStatement st(db_->handle(), "SELECT nm, read FROM comments_read WHERE doc = ?1");
    st.text(1, doc_);
    while (st.step() == SQLITE_ROW) {
        marks_.insert(st.columnText(0), st.columnInt(1) != 0);
    }
}

std::optional<bool> ReadState::stored(const annot::AnnotRef& ref) const {
    const auto it = marks_.constFind(commentKey(ref));
    if (it == marks_.constEnd()) {
        return std::nullopt;
    }
    return *it;
}

bool ReadState::isRead(const annot::Comment& comment, const std::string& me) const {
    if (const auto mark = stored(comment.ref)) {
        return *mark;
    }
    return comment.author == me;
}

void ReadState::setRead(const annot::AnnotRef& ref, bool read) {
    setRead(std::vector<annot::AnnotRef>{ref}, read);
}

void ReadState::setRead(const std::vector<annot::AnnotRef>& refs, bool read) {
    for (const annot::AnnotRef& ref : refs) {
        marks_.insert(commentKey(ref), read);
    }
    if (doc_.isEmpty() || db_ == nullptr || !db_->isOpen() || refs.empty()) {
        return;
    }
    std::lock_guard lock(db_->mutex());
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    for (const annot::AnnotRef& ref : refs) {
        SqlStatement(
            db_->handle(),
            "INSERT INTO comments_read(doc, nm, read, marked_at) VALUES(?1, ?2, ?3, ?4) "
            "ON CONFLICT(doc, nm) DO UPDATE SET read = excluded.read, marked_at = excluded.marked_at")
            .text(1, doc_)
            .text(2, commentKey(ref))
            .integer(3, read ? 1 : 0)
            .integer(4, now)
            .run();
    }
}

}  // namespace ot::comments
