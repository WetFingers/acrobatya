#include "ui/shell/read_mode.h"

#include <QApplication>
#include <QEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QSettings>
#include <QStackedWidget>

#include "core/pages/labels.h"
#include "services/commands.h"
#include "services/document_session.h"
#include "ui/shell/document_view.h"
#include "ui/shell/global_bar.h"
#include "ui/shell/main_window.h"
#include "ui/shell/message_bar.h"
#include "ui/shell/nav_pane.h"
#include "ui/shell/status_bar.h"
#include "ui/shell/tool_panel.h"
#include "ui/theme/theme.h"
#include "ui/view/page_view.h"
#include "ui/widgets/tool_button.h"

namespace ot::ui {

// ---------------------------------------------------------------- ReadModeBar

ReadModeBar::ReadModeBar(QWidget* parent) : FloatingBar(parent) {
    setObjectName(QStringLiteral("readModeBar"));
    addButton(QStringLiteral("prev"), QStringLiteral("chevron-left"), QString(), false);
    page_ = new QLineEdit(this);
    page_->setObjectName(QStringLiteral("readPageField"));
    page_->setAlignment(Qt::AlignCenter);
    addWidget(QStringLiteral("page"), page_);
    total_ = new QLabel(this);
    addWidget(QStringLiteral("total"), total_);
    addButton(QStringLiteral("next"), QStringLiteral("chevron-right"), QString(), false);
    addSeparator();
    addButton(QStringLiteral("zoomOut"), QStringLiteral("minus"), QString(), false);
    zoom_ = new QLabel(this);
    zoom_->setObjectName(QStringLiteral("readZoom"));
    zoom_->setAlignment(Qt::AlignCenter);
    addWidget(QStringLiteral("zoom"), zoom_);
    addButton(QStringLiteral("zoomIn"), QStringLiteral("plus"), QString(), false);
    addSeparator();
    addButton(QStringLiteral("exit"), QStringLiteral("minimize"), QString(), false);
    const Metrics& m = Theme::sizes();
    page_->setFixedWidth(m.grid * 12);
    zoom_->setMinimumWidth(m.grid * 12);

    connect(this, &FloatingBar::triggered, this, [this](const QString& id) {
        if (id == QLatin1String("exit")) {
            emit exitRequested();
            return;
        }
        if (view_ == nullptr) {
            return;
        }
        PageView& v = view_->view();
        if (id == QLatin1String("prev")) {
            v.previousPage();
        } else if (id == QLatin1String("next")) {
            v.nextPage();
        } else if (id == QLatin1String("zoomOut")) {
            v.zoomOut();
        } else if (id == QLatin1String("zoomIn")) {
            v.zoomIn();
        }
    });
    connect(page_, &QLineEdit::returnPressed, this, [this] { goToInput(page_->text()); });
    retranslate();
}

void ReadModeBar::attach(DocumentView* view) {
    if (view_ != nullptr) {
        disconnect(&view_->view(), nullptr, this, nullptr);
    }
    view_ = view;
    labels_.clear();
    if (view_ != nullptr) {
        labels_ = StatusBar::useLogicalNumbers() ? view_->session().summary().pageLabels : QStringList();
        connect(&view_->view(), &PageView::currentPageChanged, this, &ReadModeBar::refresh);
        connect(&view_->view(), &PageView::zoomChanged, this, &ReadModeBar::refresh);
        connect(&view_->view(), &PageView::layoutChanged, this, &ReadModeBar::refresh);
    }
    refresh();
}

bool ReadModeBar::goToInput(const QString& text) {
    if (view_ == nullptr) {
        return false;
    }
    std::vector<std::string> labels;
    for (const QString& l : std::as_const(labels_)) {
        labels.push_back(l.toStdString());
    }
    PageView& v = view_->view();
    const std::optional<int> target = pages::parsePageInput(labels, text.toStdString(), v.pageCount());
    if (target) {
        v.goToPage(*target);
    }
    v.setFocus(Qt::OtherFocusReason);
    refresh();
    return target.has_value();
}

void ReadModeBar::refresh() {
    if (view_ == nullptr) {
        page_->clear();
        total_->clear();
        zoom_->clear();
        return;
    }
    const PageView& v = view_->view();
    std::vector<std::string> labels;
    for (const QString& l : std::as_const(labels_)) {
        labels.push_back(l.toStdString());
    }
    if (!page_->hasFocus()) {
        page_->setText(QString::fromStdString(pages::pageLabel(labels, v.currentPage())));
    }
    total_->setText(tr("из %1").arg(v.pageCount()));
    zoom_->setText(tr("%1 %").arg(qRound(v.zoomPercent())));
    button(QStringLiteral("prev"))->setEnabled(v.currentPage() > 0);
    button(QStringLiteral("next"))->setEnabled(v.currentPage() + 1 < v.pageCount());
    adjustSize();
}

void ReadModeBar::retranslate() {
    setAccessibleName(tr("Режим чтения"));
    button(QStringLiteral("prev"))->setLabel(tr("Предыдущая страница"));
    button(QStringLiteral("next"))->setLabel(tr("Следующая страница"));
    button(QStringLiteral("zoomOut"))->setLabel(tr("Уменьшить"));
    button(QStringLiteral("zoomIn"))->setLabel(tr("Увеличить"));
    button(QStringLiteral("exit"))->setLabel(tr("Выйти из режима чтения (Esc)"));
    page_->setAccessibleName(tr("Номер страницы"));
    refresh();
}

void ReadModeBar::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    FloatingBar::changeEvent(event);
}

// ---------------------------------------------------------------- MainWindow: режим чтения

void MainWindow::setReadMode(bool on) {
    if (on == readMode_) {
        return;
    }
    DocumentView* view = currentDocument();
    if (on && view == nullptr) {
        return;
    }
    if (on) {
        menuBarShown_ = menuBar()->isVisible();
    }
    readMode_ = on;
    commands_->setContextActive(QLatin1String(kReadModeContext), on);
    globalBar_->setVisible(!on);
    menuBar()->setVisible(!on && menuBarShown_);
    applyPanels();
    if (on) {
        readBar_->attach(view);
        readBar_->show();
        placeReadBar();
        view->setFocus(Qt::OtherFocusReason);
        qApp->installEventFilter(this);  // Esc — раньше вида (MainWindow::eventFilter)
    } else {
        qApp->removeEventFilter(this);
        readBar_->hide();
        readBar_->attach(nullptr);
    }
    updateActions();
    emit readModeChanged(on);
}

// Полоса режима чтения — по центру у нижнего края документа.
void MainWindow::placeReadBar() {
    if (!readMode_ || readBar_ == nullptr) {
        return;
    }
    readBar_->adjustSize();
    const QSize s = readBar_->sizeHint();
    const QRect area = center_->rect();
    const int margin = Theme::sizes().grid * 4;
    readBar_->setGeometry(area.left() + (area.width() - s.width()) / 2,
                          area.bottom() - margin - s.height() + 1, s.width(), s.height());
    readBar_->raise();
}

// Видимость панелей по F8, режиму чтения и Главной (Shift+F4 — свернуть, не спрятать; F4 — своё состояние).
void MainWindow::applyPanels() {
    const bool home = isHomeShown();
    const bool hide = panelsHidden_ || readMode_;
    toolPanel_->setVisible(!hide && !home);
    navPane_->setShown(!hide && !home);
    for (int i = 0; i < documentCount(); ++i) {
        DocumentView* d = document(i);
        if (QWidget* bar = d->floatingBar()) {
            bar->setVisible(!hide);
        }
        MessageStack& stack = d->messageStack();
        stack.setVisible(!readMode_ && !stack.bars().isEmpty());
    }
    updateStatusVisibility();
}

}  // namespace ot::ui
