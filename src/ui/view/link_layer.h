// LinkLayer — ссылки страниц в PageView (план 2.2b; эталон 3.1 «Навигация», 6.9; view-13): списки ссылок по
// страницам — задачей документа (services/link_ops.h → core/links/links.h), заранее для видимых страниц;
// хит-тест под курсором (курсор-указатель и переход по щелчку — ToolController у «Выделения», «Руки» и
// «Панорамирования»); фокус клавиатуры Tab/Shift+Tab по ссылкам видимых страниц (порядок — сверху вниз, слева
// направо), рамка фокуса — этот слой; Enter — переход. Сам переход и сигнал linkActivated —
// PageView::activateLink.
#pragma once

#include <QHash>
#include <QMetaType>
#include <QPointF>
#include <QSet>

#include <optional>
#include <vector>

#include "core/links/links.h"
#include "ui/view/overlay.h"

Q_DECLARE_METATYPE(ot::links::LinkAction)

namespace ot::ui {

class PageView;

class LinkLayer : public Overlay {
public:
    using LinkInfo = links::LinkInfo;
    static constexpr double kHitTolerancePx = 1.0;  // ссылка ловится чуть шире рамки (px экрана)

    explicit LinkLayer(PageView& view) : view_(view) {}

    // Ссылки страницы: загружены — указатель, нет — запрос (придут позже, вид перерисуется) и nullptr.
    const std::vector<LinkInfo>* links(int page);
    bool hasLinks(int page) const { return links_.contains(page); }
    void prefetch(const std::vector<int>& pages);
    // Ссылка под точкой вьюпорта (только загруженные страницы; страница под курсором запрашивается).
    std::optional<LinkInfo> linkAt(QPointF viewportPoint);

    // Фокус клавиатуры: следующая/предыдущая ссылка видимых страниц. false — дальше ссылок нет, фокус снят.
    bool focusNext(bool forward);
    const LinkInfo* focused() const;
    void clearFocus();

    // Документ сменился — списков и фокуса нет; правка страницы — её список заново.
    void documentChanged();
    void pageChanged(int page);

    void paintPage(QPainter& painter, const PageView& view, int page) override;

private:
    // Порядок Tab на странице: индексы списка сверху вниз, слева направо.
    static std::vector<int> tabOrder(const std::vector<LinkInfo>& list);

    PageView& view_;
    QHash<int, std::vector<LinkInfo>> links_;
    QSet<int> requested_;
    QHash<int, quint64> generation_;
    quint64 epoch_ = 0;
    int focusPage_ = -1;
    int focusIndex_ = -1;  // индекс в links_[focusPage_]
};

}  // namespace ot::ui
