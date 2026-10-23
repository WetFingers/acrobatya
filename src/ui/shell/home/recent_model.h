// Модели Главной (эталон 3.18; view-29, view-31): общие роли карточки (путь, подпись, папка, закреплён, в
// избранном) — для RecentModel (недавние или избранное из services/recent.h) и FilesModel (папка с *.pdf).
// Миниатюры модель не держит: делегат берёт их у ThumbnailCache при отрисовке, модель по сигналу ready
// обновляет строку с этим путём.
#pragma once

#include <QAbstractListModel>

#include "services/recent.h"

namespace ot {
class ThumbnailCache;
}

namespace ot::ui {

namespace home {

enum Role : int {
    PathRole = Qt::UserRole + 1,
    DetailRole,    // строка под именем: дата открытия или изменения, число страниц
    IsDirRole,     // папка (Файлы)
    PinnedRole,    // закреплён в недавних
    FavoriteRole,  // в избранном
    OpenedRole,    // QDateTime — сортировка недавних
};

}  // namespace home

class RecentModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum class Source { Recent, Favorites };

    RecentModel(RecentFiles& recent, ThumbnailCache& thumbs, Source source, QObject* parent = nullptr);

    Source source() const { return source_; }
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QModelIndex indexOf(const QString& path) const;
    const RecentEntry* entry(int row) const;
    void reload();

private:
    RecentFiles& recent_;
    Source source_;
    QList<RecentEntry> entries_;
};

}  // namespace ot::ui
