#include "ui/shell/preferences/preferences_dialog.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "services/settings.h"
#include "ui/shell/preferences/commenting_page.h"
#include "ui/shell/preferences/documents_page.h"
#include "ui/shell/preferences/editing_page.h"
#include "ui/shell/preferences/general_page.h"
#include "ui/shell/preferences/measuring_page.h"
#include "ui/shell/preferences/page_display_page.h"
#include "ui/shell/preferences/preferences_page.h"
#include "ui/shell/preferences/print_page.h"
#include "ui/shell/preferences/signatures_page.h"
#include "ui/theme/theme.h"
#include "ui/widgets/empty_state.h"
#include "ui/widgets/row_delegate.h"

namespace ot::ui {

namespace {

// Категория будущего этапа: пустое состояние «появится в версии 0.N».
class StubPreferencesPage : public PreferencesPage {
public:
    StubPreferencesPage(QString id, int stage, QWidget* parent = nullptr)
        : PreferencesPage(parent), id_(std::move(id)), stage_(stage) {
        setObjectName(id_ + QStringLiteral("Page"));
        auto* column = new QVBoxLayout(this);
        column->setContentsMargins(0, 0, 0, 0);
        empty_ = new EmptyState(QStringLiteral("settings"), QString(), QString(), this);
        column->addWidget(empty_);
        retranslate();
    }
    QString categoryId() const override { return id_; }
    int stage() const override { return stage_; }
    QString title() const override {
        // Названия категорий — по русскому Acrobat (эталон 3.18).
        static const std::pair<const char*, const char*> kTitles[] = {
            {"commenting", QT_TRANSLATE_NOOP("ot::ui::PreferencesDialog", "Комментирование")},
            {"forms", QT_TRANSLATE_NOOP("ot::ui::PreferencesDialog", "Формы")},
            {"editing", QT_TRANSLATE_NOOP("ot::ui::PreferencesDialog", "Редактирование содержимого")},
            {"trust", QT_TRANSLATE_NOOP("ot::ui::PreferencesDialog", "Диспетчер доверия")},
            {"search", QT_TRANSLATE_NOOP("ot::ui::PreferencesDialog", "Поиск")},
            {"reading", QT_TRANSLATE_NOOP("ot::ui::PreferencesDialog", "Чтение")},
            {"ocr", QT_TRANSLATE_NOOP("ot::ui::PreferencesDialog", "Сканирование и OCR")},
            {"convert", QT_TRANSLATE_NOOP("ot::ui::PreferencesDialog", "Конвертация")},
            {"print", QT_TRANSLATE_NOOP("ot::ui::PreferencesDialog", "Печать")},
            {"hotkeys", QT_TRANSLATE_NOOP("ot::ui::PreferencesDialog", "Горячие клавиши")},
            {"quickbar", QT_TRANSLATE_NOOP("ot::ui::PreferencesDialog", "Панель быстрых действий")},
            {"about", QT_TRANSLATE_NOOP("ot::ui::PreferencesDialog", "О программе")},
        };
        for (const auto& [id, title] : kTitles) {
            if (id_ == QLatin1String(id)) {
                return QCoreApplication::translate("ot::ui::PreferencesDialog", title);
            }
        }
        return id_;
    }
    void retranslate() {
        empty_->setText(
            QCoreApplication::translate("ot::ui::PreferencesDialog", "Раздел «%1» появится в версии 0.%2.")
                .arg(title())
                .arg(stage_));
    }

protected:
    void changeEvent(QEvent* event) override {
        if (event->type() == QEvent::LanguageChange) {
            retranslate();
        }
        PreferencesPage::changeEvent(event);
    }

private:
    QString id_;
    int stage_;
    EmptyState* empty_ = nullptr;
};

}  // namespace

PreferencesDialog::PreferencesDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("preferencesDialog"));
    setWindowModality(Qt::WindowModal);
    setSizeGripEnabled(true);

    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("preferencesCategories"));
    list_->setFrameShape(QFrame::NoFrame);
    list_->setSelectionMode(QAbstractItemView::SingleSelection);
    list_->setItemDelegate(new RowDelegate(list_));
    list_->setMouseTracking(true);

    title_ = new QLabel(this);
    title_->setObjectName(QStringLiteral("preferencesTitle"));
    stack_ = new QStackedWidget(this);
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("preferencesScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setWidget(stack_);

    buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons_->setObjectName(QStringLiteral("preferencesButtons"));
    connect(buttons_, &QDialogButtonBox::accepted, this, &PreferencesDialog::accept);
    connect(buttons_, &QDialogButtonBox::rejected, this, &PreferencesDialog::reject);

    auto* right = new QVBoxLayout();
    right->addWidget(title_);
    right->addWidget(scroll, 1);
    auto* body = new QHBoxLayout();
    body->setContentsMargins(0, 0, 0, 0);
    body->addWidget(list_);
    body->addLayout(right, 1);
    auto* column = new QVBoxLayout(this);
    column->addLayout(body, 1);
    column->addWidget(buttons_);

    // Порядок категорий — эталон 3.18. Сессия-владелец заменяет заглушку своей страницей здесь.
    addPage(new GeneralPage());
    addPage(new PageDisplayPage());
    addPage(new DocumentsPage());
    addPage(new CommentingPage());
    addPage(new MeasuringPage());
    addPage(new StubPreferencesPage(QStringLiteral("forms"), 6));
    addPage(new EditingPage());
    addPage(new SignaturesPage());
    addPage(new StubPreferencesPage(QStringLiteral("trust"), 7));
    addPage(new StubPreferencesPage(QStringLiteral("search"), 9));
    addPage(new StubPreferencesPage(QStringLiteral("reading"), 8));
    addPage(new StubPreferencesPage(QStringLiteral("ocr"), 8));
    addPage(new StubPreferencesPage(QStringLiteral("convert"), 7));
    addPage(new PrintPage());
    addPage(new StubPreferencesPage(QStringLiteral("hotkeys"), 9));
    addPage(new StubPreferencesPage(QStringLiteral("quickbar"), 9));
    addPage(new StubPreferencesPage(QStringLiteral("about"), 9));

    connect(list_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0 || row >= static_cast<int>(pages_.size())) {
            return;
        }
        stack_->setCurrentIndex(row);
        title_->setText(pages_[static_cast<size_t>(row)]->title());
    });
    list_->setCurrentRow(0);
    retranslate();
    applyMetrics();
    connect(&Theme::instance(), &Theme::themeChanged, this, &PreferencesDialog::applyMetrics);
    reload();
}

void PreferencesDialog::addPage(PreferencesPage* page) {
    pages_.push_back(page);
    stack_->addWidget(page);
    auto* item = new QListWidgetItem(page->title(), list_);
    item->setData(Qt::UserRole, page->categoryId());
}

QStringList PreferencesDialog::categoryIds() const {
    QStringList ids;
    for (const PreferencesPage* p : pages_) {
        ids << p->categoryId();
    }
    return ids;
}

PreferencesPage* PreferencesDialog::page(const QString& categoryId) const {
    for (PreferencesPage* p : pages_) {
        if (p->categoryId() == categoryId) {
            return p;
        }
    }
    return nullptr;
}

QString PreferencesDialog::currentCategory() const {
    const int row = list_->currentRow();
    return row >= 0 && row < static_cast<int>(pages_.size()) ? pages_[static_cast<size_t>(row)]->categoryId()
                                                             : QString();
}

bool PreferencesDialog::showCategory(const QString& categoryId) {
    for (size_t i = 0; i < pages_.size(); ++i) {
        if (pages_[i]->categoryId() == categoryId) {
            list_->setCurrentRow(static_cast<int>(i));
            return true;
        }
    }
    return false;
}

void PreferencesDialog::reload() {
    const Settings settings;
    for (PreferencesPage* p : pages_) {
        p->load(settings);
    }
}

void PreferencesDialog::applyAll() {
    {
        Settings settings;
        for (PreferencesPage* p : pages_) {
            p->apply(settings);
        }
        settings.sync();
    }
    emit applied();
}

void PreferencesDialog::accept() {
    applyAll();
    QDialog::accept();
}

void PreferencesDialog::showEvent(QShowEvent* event) {
    reload();  // правки, сделанные мимо диалога (F8, клавиши), — в поля
    QDialog::showEvent(event);
}

void PreferencesDialog::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QDialog::changeEvent(event);
}

void PreferencesDialog::retranslate() {
    setWindowTitle(tr("Установки"));
    for (size_t i = 0; i < pages_.size(); ++i) {
        list_->item(static_cast<int>(i))->setText(pages_[i]->title());
    }
    const int row = list_->currentRow();
    if (row >= 0 && row < static_cast<int>(pages_.size())) {
        title_->setText(pages_[static_cast<size_t>(row)]->title());
    }
    list_->setAccessibleName(tr("Категории"));
    buttons_->button(QDialogButtonBox::Ok)->setText(tr("ОК"));
    buttons_->button(QDialogButtonBox::Cancel)->setText(tr("Отмена"));
}

void PreferencesDialog::applyMetrics() {
    const Metrics& m = Theme::sizes();
    resize(m.dialogWidth, m.dialogHeight);
    setMinimumSize(m.dialogWidth * 3 / 4, m.dialogHeight * 3 / 4);
    list_->setFixedWidth(m.dialogNav);
    title_->setFont(panelTitleFont(m));
    title_->setContentsMargins(m.grid * 3, m.grid * 2, m.grid * 3, 0);
    if (auto* column = qobject_cast<QVBoxLayout*>(layout())) {
        column->setContentsMargins(m.grid * 4, m.grid * 4, m.grid * 4, m.grid * 4);
        column->setSpacing(m.grid * 3);
    }
    for (int i = 0; i < list_->count(); ++i) {
        list_->item(i)->setSizeHint(QSize(0, m.row));
    }
}

}  // namespace ot::ui
