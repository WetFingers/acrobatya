#include "ui/shell/home/home_page.h"

#include <QComboBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QPushButton>
#include <QSortFilterProxyModel>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>

#include "services/recent.h"
#include "services/thumbnails.h"
#include "ui/shell/home/brief_pane.h"
#include "ui/shell/home/files_model.h"
#include "ui/shell/home/recent_model.h"
#include "ui/shell/home/thumb_delegate.h"
#include "ui/theme/theme.h"
#include "ui/widgets/empty_state.h"
#include "ui/widgets/list_row.h"
#include "ui/widgets/search_field.h"
#include "ui/widgets/segmented_bar.h"
#include "ui/widgets/tool_button.h"

namespace ot::ui {

namespace {

// Недавние и избранное: закреплённые — первыми, затем по дате открытия (новые сверху) или по имени.
class HomeProxy : public QSortFilterProxyModel {
public:
    using QSortFilterProxyModel::QSortFilterProxyModel;
    void setByName(bool byName) {
        byName_ = byName;
        invalidate();
    }
    bool byName() const { return byName_; }

protected:
    bool lessThan(const QModelIndex& left, const QModelIndex& right) const override {
        const bool pl = left.data(home::PinnedRole).toBool();
        const bool pr = right.data(home::PinnedRole).toBool();
        if (pl != pr) {
            return pl;
        }
        if (byName_) {
            return QString::localeAwareCompare(left.data().toString(), right.data().toString()) < 0;
        }
        return left.data(home::OpenedRole).toDateTime() > right.data(home::OpenedRole).toDateTime();
    }

private:
    bool byName_ = false;
};

}  // namespace

HomePage::HomePage(RecentFiles& recent, ThumbnailCache& thumbs, QWidget* parent)
    : QWidget(parent), recent_(recent), thumbs_(thumbs) {
    setObjectName(QStringLiteral("homePage"));
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);

    // Слева — разделы.
    nav_ = new QWidget(this);
    nav_->setObjectName(QStringLiteral("homeNav"));
    nav_->setAttribute(Qt::WA_StyledBackground);
    auto* navColumn = new QVBoxLayout(nav_);
    title_ = new QLabel(nav_);
    title_->setObjectName(QStringLiteral("homeTitle"));
    navColumn->addWidget(title_);
    navRecent_ = addNav(QStringLiteral("clock"), Section::Recent);
    navFavorites_ = addNav(QStringLiteral("star"), Section::Favorites);
    filesCaption_ = new QLabel(nav_);
    filesCaption_->setObjectName(QStringLiteral("homeCaption"));
    navColumn->addWidget(filesCaption_);
    navDesktop_ = addNav(QStringLiteral("monitor"), Section::Files);
    navDocuments_ = addNav(QStringLiteral("folder"), Section::Files);
    navDownloads_ = addNav(QStringLiteral("download"), Section::Files);
    navOpenFolder_ = addNav(QStringLiteral("folder-open"), Section::Files);
    navScans_ = addNav(QStringLiteral("scan-text"), Section::Scans);
    navColumn->addStretch(1);
    connect(navRecent_, &ListRow::clicked, this, &HomePage::showRecent);
    connect(navFavorites_, &ListRow::clicked, this, &HomePage::showFavorites);
    connect(navDesktop_, &ListRow::clicked, this,
            [this] { showFolder(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)); });
    connect(navDocuments_, &ListRow::clicked, this,
            [this] { showFolder(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)); });
    connect(navDownloads_, &ListRow::clicked, this,
            [this] { showFolder(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)); });
    connect(navOpenFolder_, &ListRow::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(this, tr("Открыть папку"), folder());
        if (!dir.isEmpty()) {
            showFolder(dir);
        }
    });
    connect(navScans_, &ListRow::clicked, this, &HomePage::showScans);
    row->addWidget(nav_);

    // В центре — поиск, кнопки и карточки.
    auto* center = new QWidget(this);
    center->setObjectName(QStringLiteral("homeCenter"));
    auto* column = new QVBoxLayout(center);
    auto* top = new QHBoxLayout();
    heading_ = new QLabel(center);
    heading_->setObjectName(QStringLiteral("homeHeading"));
    top->addWidget(heading_);
    top->addStretch(1);
    search_ = new SearchField(center);
    search_->setObjectName(QStringLiteral("homeSearch"));
    connect(search_, &QLineEdit::textChanged, this, &HomePage::setSearchText);
    top->addWidget(search_);
    sort_ = new QComboBox(center);
    sort_->setObjectName(QStringLiteral("homeSort"));
    sort_->addItem(QString());
    sort_->addItem(QString());
    connect(sort_, &QComboBox::currentIndexChanged, this, [this](int i) { sortByName(i == 1); });
    top->addWidget(sort_);
    mode_ = new SegmentedBar(center);
    mode_->setObjectName(QStringLiteral("homeMode"));
    mode_->addSegment(QString(), QStringLiteral("layout-grid"), QString());
    mode_->addSegment(QString(), QStringLiteral("list"), QString());
    mode_->setCurrentIndex(0);
    connect(mode_, &SegmentedBar::currentChanged, this, [this](int i) { setGridMode(i == 0); });
    top->addWidget(mode_);
    open_ = new QPushButton(center);
    open_->setObjectName(QStringLiteral("homeOpen"));
    open_->setProperty("primary", true);
    connect(open_, &QPushButton::clicked, this, &HomePage::openDialogRequested);
    top->addWidget(open_);
    create_ = new QToolButton(center);
    create_->setObjectName(QStringLiteral("homeCreate"));
    create_->setPopupMode(QToolButton::InstantPopup);
    create_->setToolButtonStyle(Qt::ToolButtonTextOnly);
    createMenu_ = new QMenu(create_);
    createMenu_->setObjectName(QStringLiteral("homeCreateMenu"));
    create_->setMenu(createMenu_);
    top->addWidget(create_);
    column->addLayout(top);

    folderBar_ = new QWidget(center);
    auto* folderRow = new QHBoxLayout(folderBar_);
    folderRow->setContentsMargins(0, 0, 0, 0);
    up_ = new ToolButton(QStringLiteral("chevron-up"), QString(), folderBar_);
    up_->setObjectName(QStringLiteral("homeUp"));
    connect(up_, &QToolButton::clicked, this, [this] {
        QDir dir(folder());
        if (dir.cdUp()) {
            showFolder(dir.absolutePath());
        }
    });
    folderLabel_ = new QLabel(folderBar_);
    folderLabel_->setObjectName(QStringLiteral("homeFolder"));
    folderRow->addWidget(up_);
    folderRow->addWidget(folderLabel_, 1);
    folderBar_->hide();
    column->addWidget(folderBar_);

    stack_ = new QStackedWidget(center);
    view_ = new QListView(stack_);
    view_->setObjectName(QStringLiteral("homeCards"));
    view_->setFrameShape(QFrame::NoFrame);
    view_->setMouseTracking(true);
    view_->setSelectionMode(QAbstractItemView::SingleSelection);
    view_->setContextMenuPolicy(Qt::CustomContextMenu);
    view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    delegate_ = new ThumbDelegate(thumbs_, this);
    view_->setItemDelegate(delegate_);
    connect(view_, &QListView::activated, this, &HomePage::onActivated);
    connect(view_, &QListView::customContextMenuRequested, this, &HomePage::contextMenu);
    stack_->addWidget(view_);
    empty_ = new EmptyState(QStringLiteral("inbox"), QString(), QString(), stack_);
    empty_->setObjectName(QStringLiteral("homeEmpty"));
    connect(empty_, &EmptyState::actionTriggered, this, &HomePage::openDialogRequested);
    stack_->addWidget(empty_);
    column->addWidget(stack_, 1);
    row->addWidget(center, 1);

    // Справа — «Краткие сведения».
    brief_ = new BriefPane(thumbs_, this);
    brief_->hide();
    connect(brief_, &BriefPane::openRequested, this, &HomePage::openRequested);
    connect(brief_, &BriefPane::printRequested, this, [this] { emit notifyLater(tr("Печать"), 2); });
    connect(brief_, &BriefPane::combineRequested, this,
            [this] { emit notifyLater(tr("Объединить файлы"), 7); });
    row->addWidget(brief_);

    recentModel_ = new RecentModel(recent_, thumbs_, RecentModel::Source::Recent, this);
    favoritesModel_ = new RecentModel(recent_, thumbs_, RecentModel::Source::Favorites, this);
    files_ = new FilesModel(recent_, thumbs_, this);
    auto* proxy = new HomeProxy(this);
    proxy->setFilterCaseSensitivity(Qt::CaseInsensitive);
    proxy->setSortCaseSensitivity(Qt::CaseInsensitive);
    proxy->setDynamicSortFilter(true);
    proxy_ = proxy;
    for (QAbstractItemModel* m :
         {static_cast<QAbstractItemModel*>(proxy_), static_cast<QAbstractItemModel*>(files_)}) {
        connect(m, &QAbstractItemModel::modelReset, this, &HomePage::refresh);
        connect(m, &QAbstractItemModel::rowsInserted, this, &HomePage::refresh);
        connect(m, &QAbstractItemModel::rowsRemoved, this, &HomePage::refresh);
        connect(m, &QAbstractItemModel::layoutChanged, this, &HomePage::refresh);
    }
    connect(files_, &QFileSystemModel::directoryLoaded, this, &HomePage::refresh);

    connect(&Theme::instance(), &Theme::themeChanged, this, &HomePage::applyMetrics);
    retranslate();
    applyMetrics();
    setGridMode(true);
    setFocusProxy(view_);  // фокус Главной — карточки (не первая кнопка глобальной панели)
    showRecent();
}

HomePage::~HomePage() = default;

QString HomePage::scansFolder() {
    const QString docs = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QDir(docs).filePath(tr("АкроБатя") + QLatin1Char('/') + tr("Сканы"));
}

ListRow* HomePage::addNav(const QString& icon, Section section) {
    auto* r = new ListRow(QString(), icon, nav_);
    r->setProperty("homeSection", static_cast<int>(section));
    nav_->layout()->addWidget(r);
    return r;
}

void HomePage::showRecent() { setSection(Section::Recent); }

void HomePage::showFavorites() { setSection(Section::Favorites); }

void HomePage::showFolder(const QString& dir) {
    setSection(section_ == Section::Scans && dir.startsWith(scansFolder()) ? Section::Scans : Section::Files,
               dir);
}

void HomePage::showScans() { setSection(Section::Scans, scansFolder()); }

QString HomePage::folder() const { return dir_; }

void HomePage::setSection(Section section, const QString& dir) {
    section_ = section;
    dir_ = QDir::cleanPath(dir);
    brief_->hide();
    brief_->clear();
    const bool files = section == Section::Files || section == Section::Scans;
    folderBar_->setVisible(files);
    sort_->setVisible(!files);
    if (files) {
        view_->setModel(files_);
        files_->setNameFilterText(search_->text());
        if (QFileInfo(dir_).isDir()) {
            files_->setRootPath(dir_);
            view_->setRootIndex(files_->index(dir_));
        } else {
            view_->setRootIndex(files_->index(QString()));  // папки нет (Сканы до первого скана)
        }
        folderLabel_->setText(QDir::toNativeSeparators(dir_));
    } else {
        proxy_->setSourceModel(section == Section::Recent ? recentModel_ : favoritesModel_);
        proxy_->setFilterFixedString(search_->text());
        proxy_->sort(0);
        view_->setModel(proxy_);
    }
    connect(view_->selectionModel(), &QItemSelectionModel::currentChanged, this, &HomePage::onSelection,
            Qt::UniqueConnection);
    connect(view_->selectionModel(), &QItemSelectionModel::selectionChanged, this, &HomePage::onSelection,
            Qt::UniqueConnection);
    const std::pair<ListRow*, bool> marks[] = {
        {navRecent_, section == Section::Recent},
        {navFavorites_, section == Section::Favorites},
        {navDesktop_,
         files && dir_ == QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation))},
        {navDocuments_, files && dir_ == QDir::cleanPath(QStandardPaths::writableLocation(
                                             QStandardPaths::DocumentsLocation))},
        {navDownloads_, files && dir_ == QDir::cleanPath(QStandardPaths::writableLocation(
                                             QStandardPaths::DownloadLocation))},
        {navScans_, section == Section::Scans},
    };
    for (const auto& [rowWidget, on] : marks) {
        rowWidget->setSelected(on);
    }
    retranslate();
    refresh();
}

void HomePage::setSearchText(const QString& text) {
    if (search_->text() != text) {
        search_->setText(text);  // сигнал textChanged вернёт сюда
        return;
    }
    if (section_ == Section::Files || section_ == Section::Scans) {
        files_->setNameFilterText(text);
    } else {
        proxy_->setFilterFixedString(text);
    }
    refresh();
}

void HomePage::setGridMode(bool grid) {
    delegate_->setMode(grid ? ThumbDelegate::Mode::Grid : ThumbDelegate::Mode::List);
    view_->setViewMode(grid ? QListView::IconMode : QListView::ListMode);
    view_->setWrapping(grid);
    view_->setFlow(grid ? QListView::LeftToRight : QListView::TopToBottom);
    view_->setResizeMode(QListView::Adjust);
    view_->setMovement(QListView::Static);
    view_->setUniformItemSizes(true);
    view_->setSpacing(0);
    if (mode_->currentIndex() != (grid ? 0 : 1)) {
        mode_->setCurrentIndex(grid ? 0 : 1);
    }
    view_->doItemsLayout();
}

void HomePage::sortByName(bool byName) {
    static_cast<HomeProxy*>(proxy_)->setByName(byName);
    proxy_->sort(0);
    if (sort_->currentIndex() != (byName ? 1 : 0)) {
        sort_->setCurrentIndex(byName ? 1 : 0);
    }
}

int HomePage::itemCount() const {
    if (view_->model() == files_) {
        return QFileInfo(dir_).isDir() ? files_->rowCount(view_->rootIndex()) : 0;
    }
    return proxy_->rowCount();
}

QModelIndex HomePage::sourceIndex(int row) const {
    if (view_->model() == nullptr) {
        return {};
    }
    return view_->model()->index(row, 0, view_->rootIndex());
}

QString HomePage::pathAt(int row) const { return sourceIndex(row).data(home::PathRole).toString(); }

void HomePage::select(int row) { view_->setCurrentIndex(sourceIndex(row)); }

void HomePage::toggleFavorite(int row) {
    const QModelIndex i = sourceIndex(row);
    if (i.isValid() && !i.data(home::IsDirRole).toBool()) {
        recent_.setFavorite(i.data(home::PathRole).toString(), !i.data(home::FavoriteRole).toBool());
    }
}

void HomePage::togglePinned(int row) {
    const QModelIndex i = sourceIndex(row);
    if (i.isValid() && section_ == Section::Recent) {
        recent_.setPinned(i.data(home::PathRole).toString(), !i.data(home::PinnedRole).toBool());
    }
}

void HomePage::removeFromRecent(int row) {
    const QModelIndex i = sourceIndex(row);
    if (i.isValid() && section_ == Section::Recent) {
        recent_.remove(i.data(home::PathRole).toString());
    }
}

bool HomePage::isEmptyShown() const { return stack_->currentWidget() == empty_; }

void HomePage::refresh() {
    const bool empty = itemCount() == 0;
    stack_->setCurrentWidget(empty ? static_cast<QWidget*>(empty_) : view_);
    if (!empty) {
        return;
    }
    brief_->hide();
    if (!search_->text().isEmpty()) {
        empty_->setText(tr("Ничего не найдено"));
        empty_->setActionText(QString());
    } else if (section_ == Section::Recent) {
        empty_->setText(tr("Откройте PDF (Ctrl+O) или перетащите файл сюда"));
        empty_->setActionText(tr("Открыть файл…"));
    } else if (section_ == Section::Favorites) {
        empty_->setText(tr("Отметьте файл звёздочкой — он появится здесь"));
        empty_->setActionText(QString());
    } else if (section_ == Section::Scans) {
        empty_->setText(tr("Сканов пока нет"));
        empty_->setActionText(QString());
    } else {
        empty_->setText(tr("В этой папке нет PDF"));
        empty_->setActionText(QString());
    }
}

void HomePage::onActivated(const QModelIndex& index) {
    const QString path = index.data(home::PathRole).toString();
    if (index.data(home::IsDirRole).toBool()) {
        showFolder(path);
    } else if (!path.isEmpty()) {
        emit openRequested(path);
    }
}

void HomePage::onSelection() {
    const QModelIndex i = view_->currentIndex();
    if (!i.isValid() || i.data(home::IsDirRole).toBool() || !view_->selectionModel()->isSelected(i)) {
        brief_->hide();
        return;
    }
    const QString path = i.data(home::PathRole).toString();
    brief_->setFile(path, recent_.find(path));
    brief_->show();
}

void HomePage::contextMenu(const QPoint& pos) {
    const QModelIndex i = view_->indexAt(pos);
    if (!i.isValid() || i.data(home::IsDirRole).toBool()) {
        return;
    }
    const int row = i.row();
    const QString path = i.data(home::PathRole).toString();
    QMenu menu(this);
    menu.addAction(tr("&Открыть"), this, [this, path] { emit openRequested(path); });
    if (section_ == Section::Recent) {
        menu.addAction(i.data(home::PinnedRole).toBool() ? tr("&Открепить") : tr("&Закрепить наверху"), this,
                       [this, row] { togglePinned(row); });
    }
    menu.addAction(i.data(home::FavoriteRole).toBool() ? tr("Убрать из &избранного") : tr("В &избранное"),
                   this, [this, row] { toggleFavorite(row); });
    if (section_ == Section::Recent) {
        menu.addAction(tr("&Убрать из списка"), this, [this, row] { removeFromRecent(row); });
    }
    menu.addSeparator();
    menu.addAction(tr("Показать в &Проводнике"), this,
                   [this] { emit notifyLater(tr("Показать в Проводнике"), 2); });
    menu.exec(view_->viewport()->mapToGlobal(pos));
}

void HomePage::retranslate() {
    title_->setText(tr("Главная"));
    filesCaption_->setText(tr("Файлы"));
    navRecent_->setText(tr("Недавние"));
    navFavorites_->setText(tr("Избранное"));
    navDesktop_->setText(tr("Рабочий стол"));
    navDocuments_->setText(tr("Документы"));
    navDownloads_->setText(tr("Загрузки"));
    navOpenFolder_->setText(tr("Открыть папку…"));
    navScans_->setText(tr("Сканы"));
    switch (section_) {
        case Section::Recent:
            heading_->setText(tr("Недавние"));
            break;
        case Section::Favorites:
            heading_->setText(tr("Избранное"));
            break;
        case Section::Files:
            heading_->setText(QFileInfo(dir_).fileName().isEmpty() ? dir_ : QFileInfo(dir_).fileName());
            break;
        case Section::Scans:
            heading_->setText(tr("Сканы"));
            break;
    }
    search_->setPlaceholderText(tr("Поиск по имени"));
    sort_->setItemText(0, tr("По дате открытия"));
    sort_->setItemText(1, tr("По имени"));
    sort_->setAccessibleName(tr("Сортировка"));
    mode_->segment(0)->setToolTip(tr("Сетка"));
    mode_->segment(0)->setAccessibleName(tr("Сетка"));
    mode_->segment(1)->setToolTip(tr("Список"));
    mode_->segment(1)->setAccessibleName(tr("Список"));
    open_->setText(tr("Открыть файл…"));
    create_->setText(tr("Создать PDF"));
    up_->setLabel(tr("Вверх"));
    view_->setAccessibleName(tr("Файлы"));
    createMenu_->clear();
    const std::pair<QString, int> items[] = {
        {tr("Из файла…"), 7},        {tr("Из нескольких файлов…"), 7}, {tr("Со сканера…"), 8},
        {tr("Из буфера обмена"), 7}, {tr("Пустая страница"), 7},       {tr("Веб-страница…"), 7},
    };
    for (const auto& [text, stage] : items) {
        const QString what = QString(text).remove(QStringLiteral("…"));
        createMenu_->addAction(text, this, [this, what, stage] { emit notifyLater(what, stage); });
    }
}

void HomePage::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
        refresh();
    }
    QWidget::changeEvent(event);
}

void HomePage::applyMetrics() {
    const Metrics& m = Theme::sizes();
    nav_->setFixedWidth(m.homeNav);
    nav_->layout()->setContentsMargins(m.grid * 3, m.grid * 6, m.grid * 3, m.grid * 4);
    nav_->layout()->setSpacing(0);
    title_->setFont(homeTitleFont(m));
    title_->setContentsMargins(m.grid, 0, 0, m.grid * 4);
    filesCaption_->setContentsMargins(m.grid, m.grid * 4, 0, m.grid);
    heading_->setFont(panelTitleFont(m));
    search_->setFixedWidth(m.searchWidth);
    if (auto* column = qobject_cast<QVBoxLayout*>(view_->parentWidget()->parentWidget()->layout())) {
        column->setContentsMargins(m.grid * 6, m.grid * 6, m.grid * 6, m.grid * 2);
        column->setSpacing(m.grid * 3);
    }
}

}  // namespace ot::ui
