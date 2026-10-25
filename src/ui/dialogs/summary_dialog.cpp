#include "ui/dialogs/summary_dialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFuture>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "services/comments_ops.h"
#include "ui/panes/comment_actions.h"
#include "ui/panes/comments_pane.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/theme/theme.h"
#include "ui/widgets/toast.h"

namespace ot::ui {

SummaryDialog::SummaryDialog(MainWindow& window, DocumentView& view, CommentsPane* pane, QWidget* parent)
    : QDialog(parent), window_(window), view_(&view), pane_(pane) {
    setObjectName(QStringLiteral("summaryDialog"));
    setWindowTitle(tr("Сводка комментариев"));
    const Metrics& m = Theme::sizes();
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(m.grid * 4, m.grid * 4, m.grid * 4, m.grid * 4);
    column->setSpacing(m.grid * 3);
    auto* form = new QFormLayout();
    form->setSpacing(m.grid * 2);

    auto* layouts = new QVBoxLayout();
    auto* layoutGroup = new QButtonGroup(this);
    const char* layoutNames[] = {
        QT_TR_NOOP("Документ и комментарии на одной странице с &выносками"),
        QT_TR_NOOP("Только &комментарии"),
        QT_TR_NOOP("Документ и комментарии на &отдельных страницах"),
    };
    for (int i = 0; i < 3; ++i) {
        layouts_[i] = new QRadioButton(tr(layoutNames[i]), this);
        layouts_[i]->setObjectName(QStringLiteral("summaryLayout%1").arg(i));
        layoutGroup->addButton(layouts_[i], i);
        layouts->addWidget(layouts_[i]);
    }
    layouts_[0]->setChecked(true);
    form->addRow(tr("Макет:"), layouts);

    auto* sortRow = new QHBoxLayout();
    sort_ = new QComboBox(this);
    sort_->setObjectName(QStringLiteral("summarySort"));
    sort_->addItems({tr("По странице"), tr("По автору"), tr("По дате"), tr("По типу"), tr("По статусу")});
    descending_ = new QCheckBox(tr("По &убыванию"), this);
    descending_->setObjectName(QStringLiteral("summaryDescending"));
    sortRow->addWidget(sort_, 1);
    sortRow->addWidget(descending_);
    auto* sortLabel = new QLabel(tr("&Сортировка:"), this);
    sortLabel->setBuddy(sort_);
    form->addRow(sortLabel, sortRow);
    if (pane_ != nullptr) {
        sort_->setCurrentIndex(static_cast<int>(pane_->sort()));
        descending_->setChecked(pane_->descending());
    }

    visibleOnly_ = new QCheckBox(tr("Только &видимые в панели «Комментарии»"), this);
    visibleOnly_->setObjectName(QStringLiteral("summaryVisibleOnly"));
    visibleOnly_->setChecked(pane_ != nullptr && pane_->filterActive());
    visibleOnly_->setEnabled(pane_ != nullptr);
    form->addRow(tr("Комментарии:"), visibleOnly_);

    const int pages = std::max(1, view.session().pageCount());
    auto* pagesRow = new QHBoxLayout();
    allPages_ = new QRadioButton(tr("&Все"), this);
    allPages_->setObjectName(QStringLiteral("summaryAllPages"));
    allPages_->setChecked(true);
    range_ = new QRadioButton(tr("&С"), this);
    range_->setObjectName(QStringLiteral("summaryRange"));
    from_ = new QSpinBox(this);
    from_->setObjectName(QStringLiteral("summaryFrom"));
    from_->setRange(1, pages);
    to_ = new QSpinBox(this);
    to_->setObjectName(QStringLiteral("summaryTo"));
    to_->setRange(1, pages);
    to_->setValue(pages);
    from_->setAccessibleName(tr("С страницы"));
    to_->setAccessibleName(tr("По страницу"));
    pagesRow->addWidget(allPages_);
    pagesRow->addWidget(range_);
    pagesRow->addWidget(from_);
    pagesRow->addWidget(new QLabel(tr("по"), this));
    pagesRow->addWidget(to_);
    pagesRow->addStretch(1);
    form->addRow(tr("Страницы:"), pagesRow);
    const auto syncRange = [this] {
        from_->setEnabled(range_->isChecked());
        to_->setEnabled(range_->isChecked());
    };
    connect(range_, &QRadioButton::toggled, this, syncRange);
    syncRange();

    fontSize_ = new QSpinBox(this);
    fontSize_->setObjectName(QStringLiteral("summaryFontSize"));
    fontSize_->setRange(6, 16);
    fontSize_->setValue(9);
    fontSize_->setSuffix(tr(" пт"));
    form->addRow(tr("&Кегль:"), fontSize_);
    column->addLayout(form);

    auto* note =
        new QLabel(tr("Сводка откроется новым документом «Без имени» — исходный файл не меняется."), this);
    note->setWordWrap(true);
    note->setObjectName(QStringLiteral("summaryNote"));
    column->addWidget(note);
    column->addStretch(1);

    auto* buttons = new QDialogButtonBox(this);
    create_ = buttons->addButton(tr("Создать &PDF"), QDialogButtonBox::AcceptRole);
    create_->setObjectName(QStringLiteral("summaryCreate"));
    create_->setDefault(true);
    print_ = buttons->addButton(tr("&Печать…"), QDialogButtonBox::ActionRole);
    print_->setObjectName(QStringLiteral("summaryPrint"));
    buttons->addButton(QDialogButtonBox::Cancel)->setText(tr("Отмена"));
    connect(create_, &QPushButton::clicked, this, [this] { build(false); });
    connect(print_, &QPushButton::clicked, this, [this] { build(true); });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    column->addWidget(buttons);
}

SummaryDialog* SummaryDialog::openFor(MainWindow& window, DocumentView& view, CommentsPane* pane) {
    auto* dialog = new SummaryDialog(window, view, pane, &window);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->open();
    return dialog;
}

QRadioButton* SummaryDialog::layoutButton(annot::SummaryLayout layout) const {
    return layouts_[static_cast<int>(layout)];
}

annot::SummaryOptions SummaryDialog::options() const {
    annot::SummaryOptions o;
    for (int i = 0; i < 3; ++i) {
        if (layouts_[i]->isChecked()) {
            o.layout = static_cast<annot::SummaryLayout>(i);
        }
    }
    o.sort = static_cast<annot::CommentSort>(std::max(0, sort_->currentIndex()));
    o.descending = descending_->isChecked();
    if (visibleOnly_->isChecked() && pane_ != nullptr) {
        o.filter = pane_->effectiveFilter();
        // «Непрочитанные» — по копии отметок панели (предикат идёт в потоке документа).
        const comments::ReadState read = pane_->readState();
        const std::string me = pane_->me();
        o.isUnread = [read, me](const annot::Comment& c) { return !read.isRead(c, me); };
    }
    if (range_->isChecked()) {
        o.firstPage = std::min(from_->value(), to_->value()) - 1;
        o.lastPage = std::max(from_->value(), to_->value()) - 1;
    }
    o.fontSize = static_cast<float>(fontSize_->value());
    const QString docTitle = view_ != nullptr ? view_->title() : QString();
    o.texts.title = tr("Сводка комментариев: %1").arg(docTitle).toStdString();
    QStringList labels = view_ != nullptr ? view_->session().summary().pageLabels : QStringList();
    o.texts.pageTitle = [labels](int page) {
        const QString label = page - 1 < labels.size() && !labels.value(page - 1).isEmpty()
                                  ? labels.value(page - 1)
                                  : QString::number(page);
        return QCoreApplication::translate("ot::ui::SummaryDialog", "Страница %1").arg(label).toStdString();
    };
    o.texts.typeName = [](const annot::Comment& c) { return comment_actions::typeTitle(c).toStdString(); };
    o.texts.stateName = [](annot::ReviewState s) { return comment_actions::stateTitle(s).toStdString(); };
    o.texts.noComments = tr("Комментариев нет").toStdString();
    return o;
}

void SummaryDialog::build(bool print) {
    if (busy_ || view_ == nullptr) {
        return;
    }
    busy_ = true;
    setEnabled(false);
    const QPointer<SummaryDialog> self(this);
    MainWindow* window = &window_;
    comments::summary(view_->session(), options())
        .then(&window_, [self, window, print](QFuture<annot::SummaryResult> f) {
            DocumentView* result = nullptr;
            try {
                const annot::SummaryResult r = f.result();
                result = window->openUntitled(QByteArray::fromStdString(r.pdf));
            } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): ниже — сообщение
            }
            if (result == nullptr) {
                Toast::notify(window, QCoreApplication::translate("ot::ui::SummaryDialog",
                                                                  "Сводку создать не удалось."));
            } else if (print) {
                window->showPrintDialog();
            }
            if (self != nullptr) {
                emit self->created(result);
                self->accept();
            }
        });
}

}  // namespace ot::ui
