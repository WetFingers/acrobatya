#include "ui/widgets/search_field.h"

#include <QAction>
#include <QKeyEvent>

#include "ui/theme/icons.h"
#include "ui/theme/theme.h"

namespace ot::ui {

SearchField::SearchField(QWidget* parent) : QLineEdit(parent) {
    setPlaceholderText(tr("Поиск"));
    setAccessibleName(tr("Поиск"));
    addAction(Icons::icon(QStringLiteral("search"), IconTone::Muted), QLineEdit::LeadingPosition);
    clear_ = addAction(Icons::icon(QStringLiteral("x"), IconTone::Muted), QLineEdit::TrailingPosition);
    clear_->setToolTip(tr("Очистить"));
    clear_->setVisible(false);
    connect(clear_, &QAction::triggered, this, [this] {
        clear();
        emit cleared();
    });
    connect(this, &QLineEdit::textChanged, this,
            [this](const QString& t) { clear_->setVisible(!t.isEmpty()); });
    applyMetrics();
    connect(&Theme::instance(), &Theme::themeChanged, this, &SearchField::applyMetrics);
}

void SearchField::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        if (!text().isEmpty()) {
            if (event->modifiers() & Qt::ShiftModifier) {
                emit previousRequested(text());
            } else {
                emit searchRequested(text());
            }
        }
        event->accept();
        return;
    }
    if (event->key() == Qt::Key_Escape && !text().isEmpty()) {
        clear();
        emit cleared();
        event->accept();
        return;
    }
    QLineEdit::keyPressEvent(event);
}

void SearchField::applyMetrics() {
    const Metrics& m = Theme::sizes();
    setMinimumHeight(m.row - m.grid * 2);
}

}  // namespace ot::ui
