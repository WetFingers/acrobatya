// Страница правой панели навигации (эталон 6.1 «Правая панель», 6.3; план 2.4): миниатюры, закладки, слои,
// вложения, назначения; комментарии (5.4), подписи (4.6), поля (6.3) — пустые состояния «появится в 0.N»,
// пока их сессии не заменят страницу настоящей. Страница показывает текущую вкладку (setDocument; nullptr —
// нет документа) и читает документ только задачами DocumentSession (services/nav_ops.h); видна — одна за раз.
#pragma once

#include <QWidget>

namespace ot::ui {

class DocumentView;

class NavPage : public QWidget {
    Q_OBJECT
public:
    using QWidget::QWidget;

    virtual QString paneId() const = 0;
    virtual QString title() const = 0;
    // Текущая вкладка окна; страница перечитывает её, когда видна (activated), иначе — при показе.
    virtual void setDocument(DocumentView* view) = 0;
    // Страница стала видимой (панель открыта на ней) — время прочитать отложенное.
    virtual void activated() {}
};

}  // namespace ot::ui
