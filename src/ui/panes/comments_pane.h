// Панель «Комментарии» (эталон 3.4 «Панель «Комментарии»», 6.1 «Правая панель»; план 5.4; comment-18,
// comment-19): карточки 64 px (иконка типа цветом комментария, автор, дата, страница, текст, статус, число
// ответов) и ответы деревом под своим комментарием; список виртуальный (QTreeView над своей моделью, строки
// одной высоты — 5 000 комментариев без задержек). Сверху — поиск по тексту (/Contents комментариев и
// ответов), сортировка (страница, автор, дата, тип, статус; по убыванию) и фильтр (тип, автор, статус, цвет,
// «Только мои», «Непрочитанные», «С галочкой»; Ctrl+8 — сбросить), «…» — развернуть/свернуть все, экспорт и
// импорт FDF/XFDF, сводка (5.4) и экспорт измерений (5.5). Фильтр и поиск действуют и на страницу: скрытые
// не рисуются и не выбираются (DocumentView::setFilteredAnnots), открытые окна их записок закрываются.
//
// Щелчок по карточке (и смена текущей клавишами) — переход к комментарию и выделение на странице («Выбор
// объектов»), комментарий становится прочитанным; выделение на странице выбирает его карточку. «Прочитано» —
// локально (services/read_state.h): чужие непрочитанные — жирный автор и точка accent. Документ читается
// задачей (services/comments_ops.h) после каждой правки (undo/redo тоже) — выбор и развёрнутость
// сохраняются. Меню записи — Удалить (с ответами — подтверждение, ветка целиком), Свойства; ответы и статусы
// — 5.4 «Ответы и статусы».
#pragma once

#include <QPointer>
#include <QSet>
#include <QStringList>

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "core/annot/comments.h"
#include "services/comments_ops.h"
#include "services/read_state.h"
#include "ui/panes/nav_page.h"

class QLabel;
class QMenu;
class QPushButton;
class QStackedWidget;
class QTimer;
class QTreeView;

namespace ot::ui {

class EmptyState;
class SearchField;
class ToolButton;

namespace comments_pane {
class Model;
class Delegate;
}  // namespace comments_pane

class CommentsPane : public NavPage {
    Q_OBJECT
public:
    using Confirm = std::function<bool(const QString& text)>;

    explicit CommentsPane(QWidget* parent = nullptr);
    ~CommentsPane() override;

    QString paneId() const override { return QStringLiteral("comments"); }
    QString title() const override;
    void setDocument(DocumentView* view) override;
    void activated() override;
    DocumentView* document() const { return view_; }

    QTreeView* list() const { return list_; }
    SearchField* searchField() const { return search_; }
    ToolButton* sortButton() const { return sortButton_; }
    ToolButton* filterButton() const { return filterButton_; }
    ToolButton* moreButton() const { return moreButton_; }
    // Меню кнопок (заполняются при показе; тесты зовут fill* и берут действия без exec).
    QMenu* sortMenu() const { return sortMenu_; }
    QMenu* filterMenu() const { return filterMenu_; }
    QMenu* moreMenu() const { return moreMenu_; }
    void fillFilterMenu();
    void fillMoreMenu();
    QLabel* filterSummary() const { return summary_; }
    EmptyState* emptyState() const { return empty_; }
    bool isEmptyShown() const;

    // Дерево документа и показанные корни (в порядке списка).
    const annot::CommentTree& tree() const { return tree_; }
    std::vector<annot::AnnotRef> shownRoots() const;
    int shownCount() const { return static_cast<int>(shown_.size()); }
    // Дерево прочитано для текущего документа (после setDocument/правки — снова false до ответа задачи).
    bool isLoaded() const { return loaded_; }

    annot::CommentSort sort() const { return sort_; }
    bool descending() const { return descending_; }
    void setSort(annot::CommentSort sort, bool descending = false);
    // Фильтр без поиска (поиск — поле); effectiveFilter — с ним (страница, сводка 5.4).
    const annot::CommentFilter& filter() const { return filter_; }
    annot::CommentFilter effectiveFilter() const;
    void setFilter(annot::CommentFilter filter);
    // Ctrl+8: фильтр и поиск — сбросить.
    void clearFilter();
    bool filterActive() const { return effectiveFilter().active(); }
    void setSearchText(const QString& text);

    void expandAll();
    void collapseAll();
    // Выбрать карточку; activate — перейти к комментарию, выделить его на странице, отметить прочитанным.
    void selectComment(const annot::AnnotRef& ref, bool activate = true);
    std::optional<annot::AnnotRef> currentComment() const;
    // Запись дерева (корень или ответ) по ссылке.
    const annot::Comment* comment(const annot::AnnotRef& ref) const { return tree_.find(ref); }

    // «Прочитано» (локально): своё — прочитано, чужое — до первого щелчка или «Отметить прочитанным».
    bool isUnread(const annot::Comment& c) const;
    void setRead(const annot::AnnotRef& ref, bool read);
    comments::ReadState& readState() { return read_; }
    // Автор текущего пользователя (Identity, Установки → Комментирование).
    std::string me() const;
    QString pageLabel(int page) const;

    // Меню записи (правый щелчок по карточке): тест заполняет без exec.
    void fillRecordMenu(QMenu& menu, const annot::AnnotRef& ref);
    // Удалить запись с веткой; есть ответы — подтверждение (тесты подменяют setConfirm).
    void removeComment(const annot::AnnotRef& ref);
    void setConfirm(Confirm confirm) { confirm_ = std::move(confirm); }

    // Можно ли править комментарии документа (не только чтение, право /P «комментарии»).
    bool canAnnotate() const;

signals:
    // Список перестроен по свежему дереву (после чтения документа или смены фильтра/сортировки).
    void reloaded();
    void filterChanged();
    // Меню записи наполнено (5.4 «Ответы и статусы» добавляет свои пункты; прямым соединением).
    void recordMenuRequested(QMenu* menu, const ot::annot::AnnotRef& ref);
    // Меню «…» наполнено (5.4 FDF/XFDF и сводка добавляют пункты; прямым соединением).
    void moreMenuRequested(QMenu* menu);
    void currentCommentChanged(const ot::annot::AnnotRef& ref);

protected:
    void changeEvent(QEvent* event) override;

private:
    void reload();
    void scheduleReload();
    void applySnapshot(comments::Snapshot snapshot, quint64 generation);
    void refill();
    void updateOverlay();
    void updateSummary();
    void syncFromPage();
    void currentChanged();
    void showRecordMenu(const QPoint& pos);
    void retranslate();
    void fillSortMenu();
    // Ссылки развёрнутых веток и текущей карточки — чтобы пережить перестройку модели.
    QSet<QString> expandedKeys() const;
    void restoreExpanded(const QSet<QString>& keys, bool expandNew);

    QPointer<DocumentView> view_;
    std::vector<QMetaObject::Connection> connections_;
    annot::CommentTree tree_;
    std::vector<const annot::Comment*> shown_;
    comments_pane::Model* model_ = nullptr;
    comments_pane::Delegate* delegate_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QTreeView* list_ = nullptr;
    EmptyState* empty_ = nullptr;
    SearchField* search_ = nullptr;
    ToolButton* sortButton_ = nullptr;
    ToolButton* filterButton_ = nullptr;
    ToolButton* moreButton_ = nullptr;
    QMenu* sortMenu_ = nullptr;
    QMenu* filterMenu_ = nullptr;
    QMenu* moreMenu_ = nullptr;
    QLabel* summary_ = nullptr;
    QPushButton* clearButton_ = nullptr;
    QTimer* reloadTimer_ = nullptr;
    annot::CommentSort sort_ = annot::CommentSort::Page;
    bool descending_ = false;
    annot::CommentFilter filter_;
    QString searchText_;
    comments::ReadState read_;
    Confirm confirm_;
    QStringList labels_;
    quint64 generation_ = 0;
    bool dirty_ = true;
    bool loaded_ = false;
    bool syncing_ = false;
    bool firstFill_ = true;
};

}  // namespace ot::ui
