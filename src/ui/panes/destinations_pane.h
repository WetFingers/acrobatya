// Панель «Назначения» (эталон 3.1 «Навигация», 6.1; план 2.4; view-50): именованные назначения документа
// (/Names /Dests и /Dests каталога) — имя и страница, сортировка по столбцу, щелчок и Enter — переход.
// В полосе навигации кнопки нет — панель открывается правым щелчком по полосе (как в новом Acrobat) или из
// меню «Вид». Переименование и создание — 7.5.
#pragma once

#include <QPointer>

#include <vector>

#include "core/links/destinations.h"
#include "ui/panes/nav_page.h"

class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace ot::ui {

class EmptyState;

class DestinationsPane : public NavPage {
    Q_OBJECT
public:
    explicit DestinationsPane(QWidget* parent = nullptr);

    QString paneId() const override { return QStringLiteral("destinations"); }
    QString title() const override;
    void setDocument(DocumentView* view) override;
    void activated() override;

    QTreeWidget* tree() const { return tree_; }
    bool isEmptyShown() const;
    const std::vector<dests::Destination>& destinations() const { return dests_; }
    // Перейти к назначению строки дерева.
    void activate(QTreeWidgetItem* item);

signals:
    void listChanged();

protected:
    void changeEvent(QEvent* event) override;

private:
    void reload();
    void fill(const std::vector<dests::Destination>& list);
    void retranslate();

    QStackedWidget* stack_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    EmptyState* empty_ = nullptr;
    QPointer<DocumentView> view_;
    std::vector<dests::Destination> dests_;
    quint64 generation_ = 0;
    bool dirty_ = true;
};

}  // namespace ot::ui
