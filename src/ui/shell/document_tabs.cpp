#include "ui/shell/document_tabs.h"

#include <QDir>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QVariantMap>

#include <algorithm>

#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/widgets/tool_button.h"

namespace ot::ui {

namespace {

const QString kId = QStringLiteral("id");
const QString kModified = QStringLiteral("modified");

}  // namespace

DocumentTabs::DocumentTabs(QWidget* parent) : QTabBar(parent) {
    setObjectName(QStringLiteral("documentTabs"));
    setAccessibleName(tr("Вкладки документов"));
    setMovable(true);
    setTabsClosable(false);  // свои крестики — ToolButton по теме
    setUsesScrollButtons(true);
    setExpanding(false);
    setDrawBase(false);
    setDocumentMode(true);
    setElideMode(Qt::ElideMiddle);
    setFocusPolicy(Qt::TabFocus);
    applyMetrics();
    connect(&Theme::instance(), &Theme::themeChanged, this, [this] {
        applyMetrics();
        refreshIcons();
    });
}

int DocumentTabs::addDocument(quint64 id, const QString& title, const QString& path) {
    const int index = addTab(title);
    setTabData(index, QVariantMap{{kId, QVariant::fromValue(id)}, {kModified, false}});
    setTabToolTip(index, QDir::toNativeSeparators(path));
    setAccessibleTabName(index, title);
    auto* close = new ToolButton(QStringLiteral("x"), tr("Закрыть"), this);
    close->setObjectName(QStringLiteral("tabClose"));
    close->setIconSizeKind(ToolButton::IconSize::Row);
    close->setCompact(Theme::sizes().iconPanel);
    connect(close, &QToolButton::clicked, this, [this, close] {
        for (int i = 0; i < count(); ++i) {
            if (tabButton(i, QTabBar::RightSide) == close) {
                emit tabCloseRequested(i);
                return;
            }
        }
    });
    setTabButton(index, QTabBar::RightSide, close);
    refreshIcons();
    return index;
}

int DocumentTabs::indexOf(quint64 id) const {
    for (int i = 0; i < count(); ++i) {
        if (idAt(i) == id) {
            return i;
        }
    }
    return -1;
}

quint64 DocumentTabs::idAt(int index) const { return tabData(index).toMap().value(kId).toULongLong(); }

void DocumentTabs::setModified(int index, bool modified) {
    if (index < 0 || index >= count() || isModified(index) == modified) {
        return;
    }
    QVariantMap data = tabData(index).toMap();
    data.insert(kModified, modified);
    setTabData(index, data);
    refreshIcons();
}

bool DocumentTabs::isModified(int index) const { return tabData(index).toMap().value(kModified).toBool(); }

void DocumentTabs::selectNext() {
    if (count() > 0) {
        setCurrentIndex((currentIndex() + 1) % count());
    }
}

void DocumentTabs::selectPrevious() {
    if (count() > 0) {
        setCurrentIndex((currentIndex() - 1 + count()) % count());
    }
}

QSize DocumentTabs::tabSizeHint(int index) const {
    const Metrics& m = Theme::sizes();
    const QSize s = QTabBar::tabSizeHint(index);
    return {std::min(s.width(), m.tabMaxWidth), m.tabHeight};
}

// Не уже tabMinWidth: имя остаётся читаемым, лишние вкладки уходят за стрелки ◀ ▶.
QSize DocumentTabs::minimumTabSizeHint(int index) const {
    const Metrics& m = Theme::sizes();
    return {std::min(tabSizeHint(index).width(), m.tabMinWidth), m.tabHeight};
}

void DocumentTabs::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::MiddleButton) {
        const int index = tabAt(event->position().toPoint());
        if (index >= 0) {
            emit tabCloseRequested(index);
            event->accept();
            return;
        }
    }
    QTabBar::mouseReleaseEvent(event);
}

void DocumentTabs::tabInserted(int index) {
    QTabBar::tabInserted(index);
    updateGeometry();
}

void DocumentTabs::applyMetrics() {
    const Metrics& m = Theme::sizes();
    setFixedHeight(m.tabHeight);
    setIconSize(QSize(m.iconRow, m.iconRow));
    for (int i = 0; i < count(); ++i) {
        if (auto* close = qobject_cast<ToolButton*>(tabButton(i, QTabBar::RightSide))) {
            close->setCompact(m.iconPanel);
        }
    }
}

// Точка accent — «несохранено» (рисуется в цвете темы; смена темы перерисовывает).
QIcon DocumentTabs::dotIcon() const {
    const Metrics& m = Theme::sizes();
    const qreal dpr = devicePixelRatioF();
    QPixmap pm(QSize(m.iconRow, m.iconRow) * dpr);
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::colors().accent.color());
    const qreal d = m.grid * 2;
    p.drawEllipse(QRectF((m.iconRow - d) / 2.0, (m.iconRow - d) / 2.0, d, d));
    return QIcon(pm);
}

void DocumentTabs::refreshIcons() {
    for (int i = 0; i < count(); ++i) {
        setTabIcon(i, isModified(i) ? dotIcon() : Icons::icon(QStringLiteral("file-text"), IconTone::Muted));
    }
}

}  // namespace ot::ui
