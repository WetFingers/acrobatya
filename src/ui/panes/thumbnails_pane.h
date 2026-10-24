// Панель «Миниатюры страниц» (эталон 3.1 «Навигация», 6.1; план 2.4; view-10): QListView по страницам
// документа, миниатюра шириной 160 px (токен thumbWidth) рисуется лениво — только для строк, которые список
// рисует (видимых): display list страницы задачей документа, растр — в общем пуле рендера окна с низким
// приоритетом (ниже тайлов вида), свой небольшой кеш. Текущая страница вида — рамка accent и прокрутка к ней;
// видимая часть страницы — рамка «области просмотра»: её можно тащить (панорамирование вида). Щелчок —
// переход к странице; выделение множественное (Ctrl/Shift), перетаскивание — список страниц mime
// application/x-acrobatya-pages (приём — «Упорядочить страницы» 7.1). Правка страницы, структуры и видимость
// слоёв перерисовывают миниатюры.
#pragma once

#include <QHash>
#include <QImage>
#include <QList>
#include <QPointer>
#include <QRectF>
#include <QSet>
#include <QStringList>

#include <memory>

#include "ui/panes/nav_page.h"

class QListView;

namespace ot {
class RenderPool;
}

namespace ot::ui {

class ThumbnailsModel;
class ThumbnailsDelegate;

class ThumbnailsPane : public NavPage {
    Q_OBJECT
public:
    static constexpr char kMimeType[] = "application/x-acrobatya-pages";

    explicit ThumbnailsPane(QWidget* parent = nullptr);
    ~ThumbnailsPane() override;

    QString paneId() const override { return QStringLiteral("thumbnails"); }
    QString title() const override;
    void setDocument(DocumentView* view) override;
    void activated() override;

    QListView* list() const { return list_; }
    DocumentView* document() const { return view_; }
    // Подписи под миниатюрами (логические номера /PageLabels, 2.4 «Строка состояния»); пусто — 1, 2, 3…
    void setPageLabels(const QStringList& labels);
    QString pageLabel(int page) const;

    // Для делегата и тестов.
    QImage thumbnail(int page) const { return images_.value(page); }
    bool hasThumbnail(int page) const { return images_.contains(page); }
    void request(int page);                                   // миниатюра нужна (строка рисуется)
    QList<int> requestedPages() const { return requested_; }  // журнал запросов (тест «только видимые»)
    void clearRequestLog() { requested_.clear(); }
    int currentPage() const { return current_; }
    // Выделенные миниатюры (Ctrl/Shift+щелчок) — страницы с нуля по возрастанию; их же получает вкладка
    // (DocumentView::setSelectedPages) для «Печать → Выделенные страницы».
    QList<int> selectedPages() const;
    // Страницы пункта «Печать страниц…» контекстного меню (view-10, 2.6): выделенные, а щелчок вне выделения
    // — одна эта страница; printPages — диалог «Печать» с ними («Выделенные страницы»).
    QList<int> pagesForMenu(int page) const;
    void printPages(const QList<int>& pages);
    // Видимая во виде часть страницы — доли страницы (0…1); пусто — страница не видна.
    QRectF visibleFrame(int page) const { return frames_.value(page); }
    QSizeF thumbSize(int page) const;  // логические px миниатюры
    QRect thumbRect(int page) const;   // миниатюра в координатах вьюпорта списка; пусто — строка не видна

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void rebind();
    void resetAll();
    void dropPage(int page);
    void submit(int page, quint64 generation);
    void cancelInvisible();
    void syncCurrent(int page);
    void updateFrames();
    RenderPool* pool();

    QListView* list_ = nullptr;
    ThumbnailsModel* model_ = nullptr;
    ThumbnailsDelegate* delegate_ = nullptr;
    QPointer<DocumentView> view_;
    std::unique_ptr<RenderPool> ownPool_;
    QHash<int, QImage> images_;
    QHash<int, quint64> jobs_;  // страница → задача пула
    QSet<int> pending_;         // запрошен display list или растр
    QSet<int> failed_;
    QList<int> requested_;
    QHash<int, QRectF> frames_;
    QStringList labels_;
    quint64 generation_ = 0;
    int current_ = -1;
    bool dirty_ = true;  // документ сменился, а панель не видна — перечитать при показе
    bool framesQueued_ = false;
    // Перетаскивание рамки области просмотра.
    int dragPage_ = -1;
    QPointF dragLast_;
};

}  // namespace ot::ui
