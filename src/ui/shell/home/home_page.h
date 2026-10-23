// Главная (эталон 3.18 «Главная», 6.7; view-29 локальная часть, view-31 недавние): слева — Недавние,
// Избранное, Файлы (Рабочий стол, Документы, Загрузки, «Открыть папку…» — QFileSystemModel с фильтром *.pdf)
// и Сканы; сверху — поиск по именам, сортировка, сетка/список, «Открыть файл…» и «Создать PDF ▾» (заглушки
// до 7.2 с тостом); в центре — карточки с миниатюрой первой страницы (ThumbnailCache, только видимые),
// контекстное меню (Открыть, Закрепить, В избранное, Убрать из списка, Показать в Проводнике); справа при
// выборе — «Краткие сведения». Пусто — «Откройте PDF (Ctrl+O) или перетащите файл сюда». Облачных разделов
// нет. Открыть файл и сообщить о заглушке решает окно (сигналы).
#pragma once

#include <QWidget>

class QComboBox;
class QLabel;
class QListView;
class QMenu;
class QPushButton;
class QSortFilterProxyModel;
class QStackedWidget;
class QToolButton;

namespace ot {
class RecentFiles;
class ThumbnailCache;
}  // namespace ot

namespace ot::ui {

class BriefPane;
class EmptyState;
class FilesModel;
class ListRow;
class RecentModel;
class SearchField;
class SegmentedBar;
class ThumbDelegate;
class ToolButton;

class HomePage : public QWidget {
    Q_OBJECT
public:
    enum class Section { Recent, Favorites, Files, Scans };

    HomePage(RecentFiles& recent, ThumbnailCache& thumbs, QWidget* parent = nullptr);
    ~HomePage() override;

    // %USERPROFILE%\Documents\АкроБатя\Сканы (имена папок — переводом; сканирование 8.2 пишет туда же).
    static QString scansFolder();

    void showRecent();
    void showFavorites();
    void showFolder(const QString& dir);
    void showScans();
    Section section() const { return section_; }
    QString folder() const;

    void setSearchText(const QString& text);
    void setGridMode(bool grid);
    void sortByName(bool byName);

    QListView* view() const { return view_; }
    int itemCount() const;  // карточек после поиска
    QString pathAt(int row) const;
    void select(int row);
    void toggleFavorite(int row);
    void togglePinned(int row);
    void removeFromRecent(int row);
    bool isEmptyShown() const;
    EmptyState* emptyState() const { return empty_; }
    BriefPane* brief() const { return brief_; }
    QMenu* createMenu() const { return createMenu_; }

signals:
    void openRequested(const QString& path);
    void openDialogRequested();
    void notifyLater(const QString& what, int stage);

protected:
    void changeEvent(QEvent* event) override;

private:
    ListRow* addNav(const QString& icon, Section section);
    void setSection(Section section, const QString& dir = {});
    void refresh();
    void onActivated(const QModelIndex& index);
    void onSelection();
    void contextMenu(const QPoint& pos);
    QModelIndex sourceIndex(int row) const;
    void retranslate();
    void applyMetrics();

    RecentFiles& recent_;
    ThumbnailCache& thumbs_;
    Section section_ = Section::Recent;
    QString dir_;

    QWidget* nav_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* filesCaption_ = nullptr;
    ListRow* navRecent_ = nullptr;
    ListRow* navFavorites_ = nullptr;
    ListRow* navDesktop_ = nullptr;
    ListRow* navDocuments_ = nullptr;
    ListRow* navDownloads_ = nullptr;
    ListRow* navOpenFolder_ = nullptr;
    ListRow* navScans_ = nullptr;

    QLabel* heading_ = nullptr;
    SearchField* search_ = nullptr;
    QComboBox* sort_ = nullptr;
    SegmentedBar* mode_ = nullptr;
    QPushButton* open_ = nullptr;
    QToolButton* create_ = nullptr;
    QMenu* createMenu_ = nullptr;
    QWidget* folderBar_ = nullptr;
    ToolButton* up_ = nullptr;
    QLabel* folderLabel_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QListView* view_ = nullptr;
    EmptyState* empty_ = nullptr;
    BriefPane* brief_ = nullptr;

    RecentModel* recentModel_ = nullptr;
    RecentModel* favoritesModel_ = nullptr;
    FilesModel* files_ = nullptr;
    QSortFilterProxyModel* proxy_ = nullptr;
    ThumbDelegate* delegate_ = nullptr;
};

}  // namespace ot::ui
