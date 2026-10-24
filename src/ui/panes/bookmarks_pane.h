// Панель «Закладки» (эталон 3.1 «Навигация», 6.1; план 2.4; view-11): дерево закладок, строится лениво —
// верхний уровень при показе, дочерние — при раскрытии узла (services/nav_ops.h: outline по пути); закладки
// с /Count > 0 раскрываются сами. Стиль и цвет — /F (жирный, курсив) и /C (чёрный — цвет темы). Щелчок и
// Enter — переход по назначению (PageView::activateLink: страница, точка, масштаб; внешний адрес и другой
// файл — сигнал вида, решает окно). Нет закладок — пустое состояние.
#pragma once

#include <QElapsedTimer>
#include <QPointer>
#include <QVariantList>

#include <vector>

#include "core/links/links.h"
#include "ui/panes/nav_page.h"

class QModelIndex;
class QStackedWidget;
class QStandardItem;
class QStandardItemModel;
class QTreeView;

namespace ot::ui {

class EmptyState;

class BookmarksPane : public NavPage {
    Q_OBJECT
public:
    enum Role { PathRole = Qt::UserRole + 1, ActionRole, PlaceholderRole };

    explicit BookmarksPane(QWidget* parent = nullptr);

    QString paneId() const override { return QStringLiteral("bookmarks"); }
    QString title() const override;
    void setDocument(DocumentView* view) override;
    void activated() override;

    QTreeView* tree() const { return tree_; }
    QStandardItemModel* model() const { return model_; }
    bool isEmptyShown() const;
    // Перейти по закладке строки (то же, что щелчок).
    void activate(const QModelIndex& index);
    // Страницы закладки (view-11, 2.6; контекстное меню «Печать страниц…»): от её страницы до страницы перед
    // следующей закладкой того же или более высокого уровня (последняя — до конца документа); закладка без
    // страницы документа — пусто. printBookmark — диалог «Печать» с ними.
    QList<int> bookmarkPages(const QModelIndex& index) const;
    void printBookmark(const QModelIndex& index);

signals:
    // Уровень загружен: path — узел (пусто — верхний уровень), count — закладок, ms — от запроса.
    void levelLoaded(const QVariantList& path, int count, qint64 ms);

protected:
    void changeEvent(QEvent* event) override;

private:
    void reload();
    void load(const std::vector<int>& path);
    void retranslate();
    int pageOf(const QModelIndex& index) const;

    QStackedWidget* stack_ = nullptr;
    QTreeView* tree_ = nullptr;
    QStandardItemModel* model_ = nullptr;
    EmptyState* empty_ = nullptr;
    QPointer<DocumentView> view_;
    std::vector<links::LinkAction> actions_;  // ActionRole — номер здесь
    quint64 generation_ = 0;
    bool dirty_ = true;
};

}  // namespace ot::ui
