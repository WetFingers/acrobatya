#include "ui/shell/status_bar.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QScrollBar>
#include <QTimer>

#include "core/pages/labels.h"
#include "services/background_tasks.h"
#include "services/document_session.h"
#include "services/settings.h"
#include "ui/shell/document_view.h"
#include "ui/theme/theme.h"
#include "ui/view/page_view.h"
#include "ui/widgets/chip.h"
#include "ui/widgets/segmented_bar.h"
#include "ui/widgets/tool_button.h"
#include "ui/widgets/zoom_box.h"

namespace ot::ui {

namespace {

// Сегменты раскладки: Одна, Непрерывно, Разворот (непрерывный — «прокрутка по две», как в новом Acrobat).
int statusSegmentOf(PageMode mode) {
    switch (mode) {
        case PageMode::SinglePage:
            return 0;
        case PageMode::Continuous:
            return 1;
        case PageMode::Facing:
        case PageMode::FacingContinuous:
            return 2;
    }
    return 1;
}

constexpr PageMode kStatusModes[] = {PageMode::SinglePage, PageMode::Continuous, PageMode::FacingContinuous};

QWidget* statusSeparator(QWidget* parent) {
    auto* line = new QWidget(parent);
    line->setObjectName(QStringLiteral("statusSeparator"));
    line->setAttribute(Qt::WA_StyledBackground);
    line->setFixedWidth(1);
    return line;
}

}  // namespace

StatusBar::StatusBar(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("statusBar"));
    setAttribute(Qt::WA_StyledBackground);
    row_ = new QHBoxLayout(this);

    docGroup_ = new QWidget(this);
    auto* doc = new QHBoxLayout(docGroup_);
    doc->setContentsMargins(0, 0, 0, 0);
    pageCaption_ = new QLabel(docGroup_);
    page_ = new QLineEdit(docGroup_);
    page_->setObjectName(QStringLiteral("pageField"));
    page_->setAlignment(Qt::AlignCenter);
    page_->installEventFilter(this);
    total_ = new QLabel(docGroup_);
    total_->setObjectName(QStringLiteral("pageTotal"));
    prev_ = new ToolButton(QStringLiteral("chevron-left"), QString(), docGroup_);
    next_ = new ToolButton(QStringLiteral("chevron-right"), QString(), docGroup_);
    zoom_ = new ZoomBox(docGroup_);
    layout_ = new SegmentedBar(docGroup_);
    layout_->setObjectName(QStringLiteral("layoutBar"));
    layout_->addSegment(QString(), QStringLiteral("file"));
    layout_->addSegment(QString(), QStringLiteral("gallery-vertical-end"));
    layout_->addSegment(QString(), QStringLiteral("book-open"));
    rotateCcw_ = new ToolButton(QStringLiteral("rotate-ccw"), QString(), docGroup_);
    rotateCw_ = new ToolButton(QStringLiteral("rotate-cw"), QString(), docGroup_);
    doc->addWidget(pageCaption_);
    doc->addWidget(page_);
    doc->addWidget(total_);
    doc->addWidget(prev_);
    doc->addWidget(next_);
    for (QWidget* w : {static_cast<QWidget*>(zoom_), static_cast<QWidget*>(layout_)}) {
        QWidget* sep = statusSeparator(docGroup_);
        separators_ << sep;
        doc->addWidget(sep);
        doc->addWidget(w);
    }
    QWidget* sep = statusSeparator(docGroup_);
    separators_ << sep;
    doc->addWidget(sep);
    doc->addWidget(rotateCcw_);
    doc->addWidget(rotateCw_);
    row_->addWidget(docGroup_);
    row_->addStretch(1);

    signed_ = new Chip(QString(), Tone::Ok, this);
    signed_->setObjectName(QStringLiteral("signedChip"));
    signed_->setIconName(QStringLiteral("badge-check"));
    pdfa_ = new Chip(QString(), Tone::Info, this);
    pdfa_->setObjectName(QStringLiteral("pdfaChip"));
    readOnly_ = new Chip(QString(), Tone::Neutral, this);
    readOnly_->setObjectName(QStringLiteral("readOnlyChip"));
    readOnly_->setIconName(QStringLiteral("lock"));
    for (Chip* c : {signed_, pdfa_, readOnly_}) {
        c->hide();
        row_->addWidget(c);
    }

    tasks_ = new QWidget(this);
    tasks_->setObjectName(QStringLiteral("tasksIndicator"));
    auto* t = new QHBoxLayout(tasks_);
    t->setContentsMargins(0, 0, 0, 0);
    taskLabel_ = new QLabel(tasks_);
    taskProgress_ = new QProgressBar(tasks_);
    taskProgress_->setTextVisible(false);
    taskProgress_->setRange(0, 100);
    taskCancel_ = new ToolButton(QStringLiteral("x"), QString(), tasks_);
    t->addWidget(taskLabel_);
    t->addWidget(taskProgress_);
    t->addWidget(taskCancel_);
    row_->addWidget(tasks_);
    tasks_->hide();

    connect(page_, &QLineEdit::returnPressed, this, [this] { goToInput(page_->text()); });
    connect(prev_, &QToolButton::clicked, this, [this] {
        if (view_ != nullptr) {
            view_->view().previousPage();
        }
    });
    connect(next_, &QToolButton::clicked, this, [this] {
        if (view_ != nullptr) {
            view_->view().nextPage();
        }
    });
    connect(zoom_, &ZoomBox::zoomRequested, this, [this](double percent) {
        if (view_ != nullptr) {
            view_->view().setZoomPercent(static_cast<float>(percent));
        }
    });
    connect(zoom_, &ZoomBox::fitRequested, this, [this](ZoomBox::Fit fit) {
        if (view_ != nullptr) {
            view_->view().fit(fit == ZoomBox::Fit::Page    ? FitMode::Page
                              : fit == ZoomBox::Fit::Width ? FitMode::Width
                                                           : FitMode::Visible);
        }
    });
    connect(layout_, &SegmentedBar::currentChanged, this, [this](int i) {
        if (view_ != nullptr && i >= 0 && i < 3 && statusSegmentOf(view_->view().pageMode()) != i) {
            view_->view().setPageMode(kStatusModes[i]);
        }
    });
    connect(rotateCcw_, &QToolButton::clicked, this, [this] {
        if (view_ != nullptr) {
            view_->view().rotateView(-90);
        }
    });
    connect(rotateCw_, &QToolButton::clicked, this, [this] {
        if (view_ != nullptr) {
            view_->view().rotateView(90);
        }
    });
    BackgroundTasks& bg = BackgroundTasks::shared();
    connect(&bg, &BackgroundTasks::changed, this, &StatusBar::refreshTasks);
    connect(taskCancel_, &QToolButton::clicked, this, [] {
        const QList<BackgroundTasks::Task> list = BackgroundTasks::shared().tasks();
        if (!list.isEmpty()) {
            BackgroundTasks::shared().cancel(list.front().id);
        }
    });

    retranslate();
    applyMetrics();
    connect(&Theme::instance(), &Theme::themeChanged, this, &StatusBar::applyMetrics);
    setDocument(nullptr);
    refreshTasks();
}

bool StatusBar::useLogicalNumbers() { return Settings().get(settings::kUseLogicalPageNumbers); }

void StatusBar::setDocument(DocumentView* view) {
    if (view_ != nullptr) {
        disconnect(&view_->view(), nullptr, this, nullptr);
        disconnect(&view_->session(), nullptr, this, nullptr);
    }
    view_ = view;
    docGroup_->setVisible(view_ != nullptr);
    if (view_ != nullptr) {
        PageView& v = view_->view();
        connect(&v, &PageView::currentPageChanged, this, &StatusBar::refreshPage);
        connect(&v, &PageView::layoutChanged, this, [this] {
            refreshPage();
            refreshLayout();
        });
        connect(&v, &PageView::zoomChanged, this, &StatusBar::refreshZoom);
        // Структурная правка и переоткрытие меняют число страниц и их логические номера.
        connect(&view_->session(), &DocumentSession::structureChanged, this, &StatusBar::refreshAll,
                Qt::QueuedConnection);
        connect(&view_->session(), &DocumentSession::saved, this, &StatusBar::refreshAll,
                Qt::QueuedConnection);
    }
    refreshAll();
}

void StatusBar::refreshAll() {
    labels_ = view_ != nullptr ? view_->session().summary().pageLabels : QStringList();
    refreshPage();
    refreshZoom();
    refreshLayout();
    refreshIndicators();
}

std::vector<std::string> StatusBar::labels() const {
    std::vector<std::string> out;
    if (useLogicalNumbers()) {
        out.reserve(static_cast<std::size_t>(labels_.size()));
        for (const QString& l : labels_) {
            out.push_back(l.toStdString());
        }
    }
    return out;
}

void StatusBar::refreshPage() {
    if (view_ == nullptr) {
        return;
    }
    const PageView& v = view_->view();
    const int count = v.pageCount();
    const int current = v.currentPage();
    const std::string label = pages::pageLabel(labels(), current);
    if (!page_->hasFocus()) {
        page_->setText(QString::fromStdString(label));
    }
    const bool logical = label != std::to_string(current + 1);
    total_->setText(logical ? tr("(%1 из %2)").arg(current + 1).arg(count) : tr("из %1").arg(count));
    prev_->setEnabled(current > 0);
    next_->setEnabled(current + 1 < count);
}

void StatusBar::refreshZoom() {
    if (view_ != nullptr) {
        zoom_->setZoomPercent(view_->view().zoomPercent());
    }
}

void StatusBar::refreshLayout() {
    if (view_ == nullptr) {
        return;
    }
    const QSignalBlocker block(layout_);
    layout_->setCurrentIndex(statusSegmentOf(view_->view().pageMode()));
}

void StatusBar::refreshIndicators() {
    const OpenSummary s = view_ != nullptr ? view_->session().summary() : OpenSummary{};
    signed_->setVisible(view_ != nullptr && s.isSigned);
    pdfa_->setVisible(view_ != nullptr && s.pdfa);
    readOnly_->setVisible(view_ != nullptr && view_->isViewOnly());
}

void StatusBar::refreshTasks() {
    const QList<BackgroundTasks::Task> list = BackgroundTasks::shared().tasks();
    tasks_->setVisible(!list.isEmpty());
    if (list.isEmpty()) {
        return;
    }
    const BackgroundTasks::Task& t = list.front();
    taskLabel_->setText(list.size() > 1 ? tr("%1 (ещё %2)").arg(t.title).arg(list.size() - 1) : t.title);
    if (t.progress < 0) {
        taskProgress_->setRange(0, 0);  // бегущая полоса
    } else {
        taskProgress_->setRange(0, 100);
        taskProgress_->setValue(t.progress);
    }
    taskProgress_->setAccessibleName(t.title);
    taskCancel_->setEnabled(static_cast<bool>(t.cancel));
}

void StatusBar::focusPageField() {
    if (view_ == nullptr) {
        return;
    }
    page_->setFocus(Qt::ShortcutFocusReason);
    page_->selectAll();
}

bool StatusBar::goToInput(const QString& text) {
    if (view_ == nullptr) {
        return false;
    }
    PageView& v = view_->view();
    const std::optional<int> target =
        pages::parsePageInput(labels(), text.toStdString(), v.pageCount(), useLogicalNumbers());
    if (!target) {
        emit pageInputRejected(text);
        page_->clearFocus();
        refreshPage();
        page_->setToolTip(tr("Страницы «%1» нет в документе.").arg(text));
        return false;
    }
    page_->setToolTip(tr("Номер страницы (Ctrl+Shift+N)"));
    v.goToPage(*target);
    v.setFocus(Qt::OtherFocusReason);
    refreshPage();
    return true;
}

bool StatusBar::eventFilter(QObject* watched, QEvent* event) {
    if (watched == page_ && event->type() == QEvent::KeyPress &&
        static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape) {
        page_->clearFocus();
        refreshPage();
        if (view_ != nullptr) {
            view_->view().setFocus(Qt::OtherFocusReason);
        }
        return true;
    }
    if (watched == page_ && event->type() == QEvent::FocusOut) {
        QTimer::singleShot(0, this, &StatusBar::refreshPage);
    }
    return QWidget::eventFilter(watched, event);
}

void StatusBar::retranslate() {
    pageCaption_->setText(tr("Стр."));
    page_->setAccessibleName(tr("Номер страницы"));
    page_->setToolTip(tr("Номер страницы (Ctrl+Shift+N)"));
    prev_->setLabel(tr("Предыдущая страница"));
    next_->setLabel(tr("Следующая страница"));
    layout_->segment(0)->setToolTip(tr("Одна страница"));
    layout_->segment(1)->setToolTip(tr("Непрерывно"));
    layout_->segment(2)->setToolTip(tr("Разворот"));
    layout_->segment(0)->setAccessibleName(tr("Одна страница"));
    layout_->segment(1)->setAccessibleName(tr("Непрерывно"));
    layout_->segment(2)->setAccessibleName(tr("Разворот"));
    rotateCcw_->setLabel(tr("Повернуть вид против часовой стрелки"));
    rotateCw_->setLabel(tr("Повернуть вид по часовой стрелке"));
    signed_->setText(tr("Подписано"));
    pdfa_->setText(tr("PDF/A"));
    readOnly_->setText(tr("Только чтение"));
    taskCancel_->setLabel(tr("Отменить"));
    refreshPage();
    refreshTasks();
}

void StatusBar::applyMetrics() {
    const Metrics& m = Theme::sizes();
    setFixedHeight(m.statusBar);
    row_->setContentsMargins(m.grid * 2, 0, m.grid * 2, 1);  // верхняя рамка 1 px (QSS)
    row_->setSpacing(m.grid * 2);
    if (auto* doc = docGroup_->layout()) {
        doc->setSpacing(m.grid);
    }
    page_->setFixedWidth(m.grid * 14);
    page_->setFixedHeight(m.statusBar - m.grid * 2);
    taskProgress_->setFixedSize(m.grid * 24, m.grid * 2);
    for (QWidget* s : std::as_const(separators_)) {
        s->setFixedHeight(m.statusBar - m.grid * 3);
    }
    for (ToolButton* b : {prev_, next_, rotateCcw_, rotateCw_, taskCancel_}) {
        b->setIconSizeKind(ToolButton::IconSize::Row);
        b->setCompact(m.statusBar - m.grid * 2);
    }
}

void StatusBar::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QWidget::changeEvent(event);
}

}  // namespace ot::ui
