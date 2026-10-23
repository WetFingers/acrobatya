#include "ui/shell/command_palette.h"

#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QListWidget>
#include <QPainter>
#include <QVBoxLayout>

#include <algorithm>
#include <vector>

#include "services/commands.h"
#include "ui/theme/theme.h"
#include "ui/widgets/row_delegate.h"
#include "ui/widgets/search_field.h"

namespace ot::ui {

namespace {

constexpr int kIdRole = Qt::UserRole;
constexpr int kDetailRole = RowDelegate::kDetailRole;  // клавиша или «появится в версии 0.N»
constexpr int kRunnableRole = Qt::UserRole + 2;

}  // namespace

CommandPalette::CommandPalette(CommandRegistry& commands, QWidget* parent)
    : QDialog(parent, Qt::Dialog | Qt::FramelessWindowHint), commands_(commands) {
    setObjectName(QStringLiteral("commandPalette"));
    setWindowModality(Qt::WindowModal);
    auto* column = new QVBoxLayout(this);
    filter_ = new SearchField(this);
    filter_->setObjectName(QStringLiteral("paletteFilter"));
    filter_->installEventFilter(this);
    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("paletteList"));
    list_->setFrameShape(QFrame::NoFrame);
    list_->setItemDelegate(new RowDelegate(list_));
    list_->setMouseTracking(true);
    list_->setUniformItemSizes(true);
    list_->setFocusPolicy(Qt::NoFocus);  // ввод остаётся в поле
    column->addWidget(filter_);
    column->addWidget(list_);
    connect(filter_, &QLineEdit::textChanged, this, [this] { rebuild(); });
    connect(list_, &QListWidget::itemActivated, this, [this] { runCurrent(); });
    connect(list_, &QListWidget::itemClicked, this, [this] { runCurrent(); });
    retranslate();
    applyMetrics();
    connect(&Theme::instance(), &Theme::themeChanged, this, &CommandPalette::applyMetrics);
}

QString CommandPalette::plainTitle(const QString& title) {
    QString plain = title;
    // «&&» — буквальный амперсанд; одиночный «&» — мнемоника.
    plain.replace(QStringLiteral("&&"), QStringLiteral("\x01"));
    plain.remove(QLatin1Char('&'));
    plain.replace(QStringLiteral("\x01"), QStringLiteral("&"));
    plain.remove(QStringLiteral("…"));
    return plain.trimmed();
}

bool CommandPalette::matches(const Command& command, const QString& filter, int* rank) {
    const QString title = plainTitle(command.title);
    const QString wanted = filter.simplified();
    int result = 0;
    if (!title.startsWith(wanted, Qt::CaseInsensitive)) {
        result = 1;
        for (const QString& w : wanted.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            if (title.contains(w, Qt::CaseInsensitive)) {
                continue;
            }
            if (!command.id.contains(w, Qt::CaseInsensitive)) {
                return false;
            }
            result = 2;
        }
    }
    if (rank != nullptr) {
        *rank = result;
    }
    return true;
}

void CommandPalette::reset() {
    filter_->clear();  // textChanged → rebuild, но и при пустом поле реестр мог смениться
    rebuild();
}

void CommandPalette::setFilter(const QString& text) {
    if (filter_->text() == text) {
        rebuild();
    } else {
        filter_->setText(text);
    }
}

void CommandPalette::rebuild() {
    struct Row {
        const Command* command;
        int rank;
        int order;
    };
    std::vector<Row> rows;
    int order = 0;
    for (const Command* c : commands_.all()) {
        ++order;
        if (c->title.isEmpty() || c->id == QLatin1String("app.palette")) {
            continue;
        }
        // Команда режима — только пока режим включён (Esc «выйти из режима» вне режима не нужна).
        if (!c->when.isEmpty() && c->when != QLatin1String(CommandRegistry::kViewContext) &&
            !commands_.isContextActive(c->when)) {
            continue;
        }
        int rank = 0;
        if (matches(*c, filter_->text(), &rank)) {
            rows.push_back({c, rank, order});
        }
    }
    std::stable_sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.rank < b.rank; });
    list_->clear();
    for (const Row& r : rows) {
        const Command& c = *r.command;
        auto* item = new QListWidgetItem(plainTitle(c.title), list_);
        item->setData(kIdRole, c.id);
        const bool runnable = c.stage == 0 && commands_.isEnabled(c.id);
        item->setData(kRunnableRole, runnable);
        item->setData(RowDelegate::kDimmedRole, !runnable);
        QString detail;
        if (c.stage > 0) {
            detail = tr("появится в версии 0.%1").arg(c.stage);
        } else if (const QList<QKeySequence> keys = commands_.effectiveShortcuts(c.id); !keys.isEmpty()) {
            detail = keys.first().toString(QKeySequence::NativeText);
        }
        item->setData(kDetailRole, detail);
        item->setToolTip(c.id);
    }
    if (list_->count() > 0) {
        // Первая выполнимая — текущая (Enter сразу делает то, что видно сверху).
        int current = 0;
        for (int i = 0; i < list_->count(); ++i) {
            if (list_->item(i)->data(kRunnableRole).toBool()) {
                current = i;
                break;
            }
        }
        list_->setCurrentRow(current);
    }
}

QStringList CommandPalette::visibleIds() const {
    QStringList ids;
    for (int i = 0; i < list_->count(); ++i) {
        ids << list_->item(i)->data(kIdRole).toString();
    }
    return ids;
}

QString CommandPalette::currentId() const {
    const QListWidgetItem* item = list_->currentItem();
    return item != nullptr ? item->data(kIdRole).toString() : QString();
}

bool CommandPalette::runCurrent() {
    const QListWidgetItem* item = list_->currentItem();
    if (item == nullptr || !item->data(kRunnableRole).toBool()) {
        return false;
    }
    const QString id = item->data(kIdRole).toString();
    accept();  // сначала закрыть: команда может открыть свой диалог или сменить фокус
    const bool ok = commands_.trigger(id);
    if (ok) {
        emit commandTriggered(id);
    }
    return ok;
}

bool CommandPalette::eventFilter(QObject* watched, QEvent* event) {
    if (watched == filter_ && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        const int count = list_->count();
        const int row = list_->currentRow();
        const int page = std::max(1, Theme::sizes().paletteRows - 1);
        int next = -1;
        switch (key->key()) {
            case Qt::Key_Down:
                next = count > 0 ? (row + 1) % count : -1;
                break;
            case Qt::Key_Up:
                next = count > 0 ? (row - 1 + count) % count : -1;
                break;
            case Qt::Key_PageDown:
                next = count > 0 ? std::min(count - 1, row + page) : -1;
                break;
            case Qt::Key_PageUp:
                next = count > 0 ? std::max(0, row - page) : -1;
                break;
            case Qt::Key_Return:
            case Qt::Key_Enter:
                runCurrent();
                return true;
            case Qt::Key_Escape:
                reject();
                return true;
            default:
                return QDialog::eventFilter(watched, event);
        }
        if (next >= 0) {
            list_->setCurrentRow(next);
            list_->scrollToItem(list_->currentItem());
        }
        return true;
    }
    return QDialog::eventFilter(watched, event);
}

void CommandPalette::showEvent(QShowEvent* event) {
    placeOverParent();
    QDialog::showEvent(event);
    filter_->setFocus();
}

void CommandPalette::retranslate() {
    setWindowTitle(tr("Палитра команд"));
    filter_->setPlaceholderText(tr("Введите название команды"));
    filter_->setAccessibleName(tr("Фильтр команд"));
    list_->setAccessibleName(tr("Команды"));
}

void CommandPalette::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
        rebuild();
    } else if (event->type() == QEvent::ActivationChange && isVisible() && isWindow() && !isActiveWindow() &&
               QApplication::activeWindow() != nullptr) {
        reject();  // щелчок мимо палитры — закрыть, как всплывающее окно
    }
    QDialog::changeEvent(event);
}

// Окно без рамки: фон и волосяная рамка border — иначе белая палитра сливается с белой страницей.
void CommandPalette::paintEvent(QPaintEvent* event) {
    (void)event;
    QPainter painter(this);
    const Palette& c = Theme::colors();
    painter.fillRect(rect(), c.surface.color());
    painter.setPen(c.border.color());
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
}

void CommandPalette::applyMetrics() {
    const Metrics& m = Theme::sizes();
    if (auto* column = qobject_cast<QVBoxLayout*>(layout())) {
        column->setContentsMargins(m.grid * 2, m.grid * 2, m.grid * 2, m.grid * 2);
        column->setSpacing(m.grid * 2);
    }
    list_->setFixedHeight(m.row * m.paletteRows);
    setFixedWidth(m.paletteWidth);
    adjustSize();
}

void CommandPalette::placeOverParent() {
    const QWidget* host = parentWidget() != nullptr ? parentWidget()->window() : nullptr;
    if (host == nullptr) {
        return;
    }
    const Metrics& m = Theme::sizes();
    const QPoint topLeft = host->mapToGlobal(QPoint((host->width() - width()) / 2, m.globalBar + m.grid * 2));
    move(isWindow() ? topLeft : parentWidget()->mapFromGlobal(topLeft));
}

}  // namespace ot::ui
