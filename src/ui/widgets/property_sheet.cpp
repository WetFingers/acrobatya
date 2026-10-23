#include "ui/widgets/property_sheet.h"

#include <QGridLayout>
#include <QLabel>

#include "ui/theme/theme.h"

namespace ot::ui {

PropertySheet::PropertySheet(QWidget* parent) : QWidget(parent) {
    grid_ = new QGridLayout(this);
    grid_->setColumnStretch(1, 1);
    applyMetrics();
    connect(&Theme::instance(), &Theme::themeChanged, this, &PropertySheet::applyMetrics);
}

QLabel* PropertySheet::addSection(const QString& title) {
    auto* label = new QLabel(title, this);
    label->setObjectName(QStringLiteral("propertySectionTitle"));
    grid_->addWidget(label, nextLine_++, 0, 1, 2);
    sections_.push_back(label);
    applyMetrics();
    return label;
}

QLabel* PropertySheet::addRow(const QString& label, QWidget* editor) {
    auto* caption = new QLabel(label, this);
    caption->setObjectName(QStringLiteral("propertyLabel"));
    caption->setBuddy(editor);
    editor->setParent(this);
    grid_->addWidget(caption, nextLine_, 0, Qt::AlignVCenter | Qt::AlignLeft);
    grid_->addWidget(editor, nextLine_, 1);
    ++nextLine_;
    ++rows_;
    editors_.push_back(editor);
    if (editor->accessibleName().isEmpty()) {
        editor->setAccessibleName(QString(label).remove(QLatin1Char('&')));
    }
    return caption;
}

void PropertySheet::addWidget(QWidget* widget) {
    widget->setParent(this);
    grid_->addWidget(widget, nextLine_++, 0, 1, 2);
}

QWidget* PropertySheet::editorAt(int row) const {
    return row >= 0 && row < rows_ ? editors_[static_cast<size_t>(row)] : nullptr;
}

void PropertySheet::applyMetrics() {
    const Metrics& m = Theme::sizes();
    grid_->setContentsMargins(m.grid * 3, m.grid * 2, m.grid * 3, m.grid * 2);
    grid_->setHorizontalSpacing(m.grid * 3);
    grid_->setVerticalSpacing(m.grid * 2);
    QFont title = uiFont(m);
    title.setWeight(QFont::DemiBold);
    for (QLabel* s : sections_) {
        s->setFont(title);
        s->setContentsMargins(0, m.grid * 2, 0, 0);
    }
}

}  // namespace ot::ui
