#include "ui/shell/home/files_model.h"

#include <QLocale>

#include "services/recent.h"
#include "services/thumbnails.h"
#include "ui/shell/home/recent_model.h"

namespace ot::ui {

FilesModel::FilesModel(RecentFiles& recent, ThumbnailCache& thumbs, QObject* parent)
    : QFileSystemModel(parent), recent_(recent) {
    setReadOnly(true);
    setFilter(QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot);
    setNameFilterDisables(false);  // не-PDF не показываются вовсе
    setNameFilterText(QString());
    connect(&thumbs, &ThumbnailCache::ready, this, [this](const QString& path) {
        const QModelIndex i = index(path);
        if (i.isValid()) {
            emit dataChanged(i, i);
        }
    });
    connect(&recent_, &RecentFiles::changed, this, [this] {
        if (rowCount(index(rootPath())) > 0) {
            emit dataChanged(index(0, 0, index(rootPath())),
                             index(rowCount(index(rootPath())) - 1, 0, index(rootPath())));
        }
    });
}

void FilesModel::setNameFilterText(const QString& text) {
    const QString t = text.trimmed();
    setNameFilters({t.isEmpty() ? QStringLiteral("*.pdf") : QStringLiteral("*%1*.pdf").arg(t)});
}

QVariant FilesModel::data(const QModelIndex& index, int role) const {
    switch (role) {
        case home::PathRole:
            return filePath(index);
        case home::DetailRole:
            return isDir(index)
                       ? QVariant()
                       : QVariant(QLocale().toString(lastModified(index).date(), QLocale::ShortFormat));
        case home::IsDirRole:
            return isDir(index);
        case home::PinnedRole:
            return false;
        case home::FavoriteRole: {
            if (isDir(index)) {
                return false;
            }
            const auto e = recent_.find(filePath(index));
            return e.has_value() && e->favorite;
        }
        default:
            return QFileSystemModel::data(index, role);
    }
}

}  // namespace ot::ui
