#include "ui/shell/home/recent_model.h"

#include <QFileInfo>
#include <QLocale>

#include "services/thumbnails.h"

namespace ot::ui {

RecentModel::RecentModel(RecentFiles& recent, ThumbnailCache& thumbs, Source source, QObject* parent)
    : QAbstractListModel(parent), recent_(recent), source_(source) {
    connect(&recent_, &RecentFiles::changed, this, &RecentModel::reload);
    connect(&thumbs, &ThumbnailCache::ready, this, [this](const QString& path) {
        const QModelIndex index = indexOf(path);
        if (index.isValid()) {
            emit dataChanged(index, index);
        }
    });
    reload();
}

void RecentModel::reload() {
    beginResetModel();
    entries_ = source_ == Source::Recent ? recent_.recent() : recent_.favorites();
    endResetModel();
}

int RecentModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(entries_.size());
}

const RecentEntry* RecentModel::entry(int row) const {
    return row >= 0 && row < entries_.size() ? &entries_[row] : nullptr;
}

QModelIndex RecentModel::indexOf(const QString& path) const {
    for (int i = 0; i < entries_.size(); ++i) {
        if (entries_[i].path == path) {
            return index(i);
        }
    }
    return {};
}

QVariant RecentModel::data(const QModelIndex& index, int role) const {
    const RecentEntry* e = entry(index.row());
    if (e == nullptr) {
        return {};
    }
    switch (role) {
        case Qt::DisplayRole:
        case Qt::AccessibleTextRole:
            return QFileInfo(e->path).fileName();
        case Qt::ToolTipRole:
            return QFileInfo(e->path).absoluteFilePath();
        case home::PathRole:
            return e->path;
        case home::DetailRole: {
            const QDateTime when = e->openedAt.isValid() && e->openedAt.toMSecsSinceEpoch() > 0
                                       ? e->openedAt
                                       : QFileInfo(e->path).lastModified();
            return QLocale().toString(when.date(), QLocale::ShortFormat);
        }
        case home::IsDirRole:
            return false;
        case home::PinnedRole:
            return e->pinned;
        case home::FavoriteRole:
            return e->favorite;
        case home::OpenedRole:
            return e->openedAt;
        default:
            return {};
    }
}

}  // namespace ot::ui
