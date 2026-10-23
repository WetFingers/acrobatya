// «Файлы» Главной (эталон 3.18): QFileSystemModel папки (Рабочий стол, Документы, Загрузки, «Открыть папку…»,
// Сканы) — только папки и *.pdf, без правки; роли карточки — как у недавних (home::PathRole…), поиск по имени
// — фильтр имён «*текст*.pdf». Миниатюры PDF — у ThumbnailCache по отрисовке, модель обновляет строку по
// ready.
#pragma once

#include <QFileSystemModel>

namespace ot {
class RecentFiles;
class ThumbnailCache;
}  // namespace ot

namespace ot::ui {

class FilesModel : public QFileSystemModel {
    Q_OBJECT
public:
    FilesModel(RecentFiles& recent, ThumbnailCache& thumbs, QObject* parent = nullptr);

    // Поиск по имени: пусто — все *.pdf.
    void setNameFilterText(const QString& text);
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

private:
    RecentFiles& recent_;
};

}  // namespace ot::ui
