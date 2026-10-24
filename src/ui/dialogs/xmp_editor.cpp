#include "ui/dialogs/xmp_editor.h"

#include <QDialogButtonBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "services/props_ops.h"
#include "ui/theme/theme.h"
#include "ui/widgets/property_sheet.h"
#include "ui/widgets/row_delegate.h"

#ifdef PropertySheet  // windows.h, включённый после property_sheet.h (unity)
#undef PropertySheet
#endif

namespace ot::ui {

XmpEditor::XmpEditor(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("xmpEditor"));
    const Metrics& m = Theme::sizes();
    resize(m.dialogWidth, m.dialogHeight);
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, m.grid * 4);
    column->setSpacing(0);
    auto* body = new QHBoxLayout();
    body->setSpacing(0);
    column->addLayout(body, 1);

    nav_ = new QListWidget(this);
    nav_->setObjectName(QStringLiteral("xmpCategories"));
    nav_->setFixedWidth(m.dialogNav);
    nav_->setItemDelegate(new RowDelegate(nav_));
    for (int i = 0; i < 4; ++i) {
        nav_->addItem(QString());
    }
    body->addWidget(nav_);
    pages_ = new QStackedWidget(this);
    body->addWidget(pages_, 1);
    connect(nav_, &QListWidget::currentRowChanged, pages_, &QStackedWidget::setCurrentIndex);

    // Описание
    auto* description = new PropertySheet();
    description->setObjectName(QStringLiteral("xmpDescription"));
    title_ = new QLineEdit(description);
    title_->setObjectName(QStringLiteral("xmpTitle"));
    author_ = new QLineEdit(description);
    subject_ = new QLineEdit(description);
    keywords_ = new QLineEdit(description);
    description->addSection(tr("Описание"));
    description->addRow(tr("&Название:"), title_);
    description->addRow(tr("&Автор:"), author_);
    description->addRow(tr("&Тема:"), subject_);
    description->addRow(tr("&Ключевые слова:"), keywords_);
    pages_->addWidget(description);

    // Пользовательские
    auto* customPage = new QWidget();
    auto* customColumn = new QVBoxLayout(customPage);
    customColumn->setContentsMargins(m.grid * 4, m.grid * 4, m.grid * 4, m.grid * 2);
    customColumn->addWidget(new QLabel(tr("Свои свойства (/Info и pdfx: в XMP):"), customPage));
    custom_ = new QTableWidget(0, 2, customPage);
    custom_->setObjectName(QStringLiteral("xmpCustom"));
    custom_->setHorizontalHeaderLabels({tr("Имя"), tr("Значение")});
    custom_->horizontalHeader()->setStretchLastSection(true);
    custom_->verticalHeader()->hide();
    custom_->setSelectionBehavior(QAbstractItemView::SelectRows);
    customColumn->addWidget(custom_, 1);
    auto* buttons = new QHBoxLayout();
    add_ = new QPushButton(tr("&Добавить"), customPage);
    connect(add_, &QPushButton::clicked, this, [this] {
        addCustom(tr("Свойство"), QString());
        custom_->editItem(custom_->item(custom_->rowCount() - 1, 0));
    });
    remove_ = new QPushButton(tr("&Удалить"), customPage);
    connect(remove_, &QPushButton::clicked, this, [this] {
        if (custom_->currentRow() >= 0) {
            custom_->removeRow(custom_->currentRow());
        }
    });
    buttons->addWidget(add_);
    buttons->addWidget(remove_);
    buttons->addStretch(1);
    customColumn->addLayout(buttons);
    pages_->addWidget(customPage);

    // Дополнительно
    advanced_ = new QTreeWidget();
    advanced_->setObjectName(QStringLiteral("xmpAdvanced"));
    advanced_->setColumnCount(2);
    advanced_->setHeaderLabels({tr("Свойство"), tr("Значение")});
    advanced_->header()->setStretchLastSection(true);
    pages_->addWidget(advanced_);

    // XMP
    raw_ = new QPlainTextEdit();
    raw_->setObjectName(QStringLiteral("xmpRaw"));
    raw_->setReadOnly(true);
    raw_->setLineWrapMode(QPlainTextEdit::NoWrap);
    pages_->addWidget(raw_);

    auto* box = new QDialogButtonBox(this);
    box->setContentsMargins(m.grid * 4, m.grid * 2, m.grid * 4, 0);
    ok_ = box->addButton(QDialogButtonBox::Ok);
    cancel_ = box->addButton(QDialogButtonBox::Cancel);
    ok_->setDefault(true);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);
    column->addWidget(box);
    nav_->setCurrentRow(0);
    retranslate();
}

void XmpEditor::load(const std::string& packet, const QString& title, const QString& author,
                     const QString& subject, const QString& keywords,
                     const QList<std::pair<QString, QString>>& custom, bool readOnly) {
    title_->setText(title);
    author_->setText(author);
    subject_->setText(subject);
    keywords_->setText(keywords);
    custom_->setRowCount(0);
    for (const auto& [name, value] : custom) {
        addCustom(name, value);
    }
    advanced_->clear();
    QTreeWidgetItem* group = nullptr;
    for (const props::XmpProperty& p : props::xmpProperties(packet)) {
        if (group == nullptr || group->data(0, Qt::UserRole).toString() != p.ns) {
            group = new QTreeWidgetItem(advanced_);
            group->setText(0, p.prefix);
            group->setToolTip(0, p.ns);
            group->setData(0, Qt::UserRole, p.ns);
            group->setExpanded(true);
        }
        auto* item = new QTreeWidgetItem(group);
        item->setText(0, p.prefix + QLatin1Char(':') + p.name);
        item->setText(1, p.value);
    }
    advanced_->expandAll();
    advanced_->resizeColumnToContents(0);
    raw_->setPlainText(packet.empty()
                           ? tr("В документе нет пакета XMP: он будет создан при сохранении свойств.")
                           : QString::fromStdString(packet));
    for (QLineEdit* e : {title_, author_, subject_, keywords_}) {
        e->setReadOnly(readOnly);
    }
    custom_->setEditTriggers(readOnly ? QAbstractItemView::NoEditTriggers
                                      : QAbstractItemView::AllEditTriggers);
    add_->setEnabled(!readOnly);
    remove_->setEnabled(!readOnly);
}

void XmpEditor::showCategory(Category category) { nav_->setCurrentRow(category); }

QString XmpEditor::title() const { return title_->text().trimmed(); }

QString XmpEditor::author() const { return author_->text().trimmed(); }

QString XmpEditor::subject() const { return subject_->text().trimmed(); }

QString XmpEditor::keywords() const { return keywords_->text().trimmed(); }

QList<std::pair<QString, QString>> XmpEditor::customProperties() const {
    QList<std::pair<QString, QString>> out;
    for (int r = 0; r < custom_->rowCount(); ++r) {
        const QTableWidgetItem* k = custom_->item(r, 0);
        const QTableWidgetItem* v = custom_->item(r, 1);
        const QString key = k != nullptr ? k->text().trimmed() : QString();
        if (!key.isEmpty()) {
            out << std::make_pair(key, v != nullptr ? v->text() : QString());
        }
    }
    return out;
}

void XmpEditor::addCustom(const QString& name, const QString& value) {
    const int row = custom_->rowCount();
    custom_->insertRow(row);
    custom_->setItem(row, 0, new QTableWidgetItem(name));
    custom_->setItem(row, 1, new QTableWidgetItem(value));
}

void XmpEditor::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QDialog::changeEvent(event);
}

void XmpEditor::retranslate() {
    setWindowTitle(tr("Дополнительные метаданные"));
    nav_->item(Description)->setText(tr("Описание"));
    nav_->item(Custom)->setText(tr("Пользовательские"));
    nav_->item(Advanced)->setText(tr("Дополнительно"));
    nav_->item(Raw)->setText(tr("Пакет XMP"));
    ok_->setText(tr("ОК"));
    cancel_->setText(tr("Отмена"));
}

}  // namespace ot::ui
