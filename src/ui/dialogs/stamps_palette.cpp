#include "ui/dialogs/stamps_palette.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "ui/theme/theme.h"

namespace ot::ui {

namespace {

constexpr int kIdRole = Qt::UserRole + 1;

struct KnownStamp {
    const char* id;
    const char* title;
};

// Подписи своих штампов (переводятся; надпись на самом штампе — данные stamps.json).
constexpr KnownStamp kKnownStamps[] = {
    {"standard/approved", QT_TRANSLATE_NOOP("ot::ui::StampsPalette", "Утверждено")},
    {"standard/not-approved", QT_TRANSLATE_NOOP("ot::ui::StampsPalette", "Не утверждено")},
    {"standard/draft", QT_TRANSLATE_NOOP("ot::ui::StampsPalette", "Черновик")},
    {"standard/final", QT_TRANSLATE_NOOP("ot::ui::StampsPalette", "Окончательный")},
    {"standard/confidential", QT_TRANSLATE_NOOP("ot::ui::StampsPalette", "Конфиденциально")},
    {"standard/not-for-distribution", QT_TRANSLATE_NOOP("ot::ui::StampsPalette", "Не для распространения")},
    {"standard/for-comment", QT_TRANSLATE_NOOP("ot::ui::StampsPalette", "Для комментариев")},
    {"sign-here/sign-here", QT_TRANSLATE_NOOP("ot::ui::StampsPalette", "Подписать здесь")},
    {"sign-here/initial-here", QT_TRANSLATE_NOOP("ot::ui::StampsPalette", "Инициалы здесь")},
    {"sign-here/witness", QT_TRANSLATE_NOOP("ot::ui::StampsPalette", "Свидетель")},
    {"dynamic/approved", QT_TRANSLATE_NOOP("ot::ui::StampsPalette", "Утверждено (имя, дата и время)")},
    {"dynamic/received", QT_TRANSLATE_NOOP("ot::ui::StampsPalette", "Получено (дата и время)")},
    {"dynamic/reviewed", QT_TRANSLATE_NOOP("ot::ui::StampsPalette", "Проверено (имя и дата)")},
};

constexpr const char* kUserCategory = "user:";

// Миниатюры по id и времени файла (пользовательские меняются).
QImage cachedThumbnail(const stamps::StampCatalog& catalog, const stamps::StampEntry& e, QSize box) {
    static QHash<QString, QImage> cache;
    const QString key = e.id + QLatin1Char('|') +
                        (e.builtIn ? QString() : QString::number(QFileInfo(e.path).lastModified().toMSecsSinceEpoch())) +
                        QLatin1Char('|') + QString::number(box.width());
    auto it = cache.find(key);
    if (it == cache.end()) {
        it = cache.insert(key, stamps::thumbnail(catalog.pdf(e), 0, box));
    }
    return *it;
}

}  // namespace

QString StampsPalette::categoryTitle(const QString& category) {
    if (category == QLatin1String(kFavorites)) {
        return QCoreApplication::translate("ot::ui::StampsPalette", "Избранное");
    }
    if (category == QLatin1String(stamps::kStandard)) {
        return QCoreApplication::translate("ot::ui::StampsPalette", "Стандартные");
    }
    if (category == QLatin1String(stamps::kSignHere)) {
        return QCoreApplication::translate("ot::ui::StampsPalette", "Подписать здесь");
    }
    if (category == QLatin1String(stamps::kDynamic)) {
        return QCoreApplication::translate("ot::ui::StampsPalette", "Динамические");
    }
    return category.startsWith(QLatin1String(kUserCategory)) ? category.mid(5) : category;
}

QString StampsPalette::stampTitle(const stamps::StampEntry& entry) {
    for (const KnownStamp& k : kKnownStamps) {
        if (entry.id == QLatin1String(k.id)) {
            return QCoreApplication::translate("ot::ui::StampsPalette", k.title);
        }
    }
    return entry.caption;
}

StampsPalette::StampsPalette(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("stampsPalette"));
    setWindowTitle(tr("Штампы"));
    const Metrics& m = Theme::sizes();
    resize(m.dialogWidth, m.propsDialogHeight);
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(m.grid * 4, m.grid * 4, m.grid * 4, m.grid * 4);
    column->setSpacing(m.grid * 3);
    auto* row = new QHBoxLayout();
    row->setSpacing(m.grid * 3);
    categories_ = new QListWidget(this);
    categories_->setObjectName(QStringLiteral("stampCategories"));
    categories_->setAccessibleName(tr("Категории штампов"));
    categories_->setFixedWidth(m.dialogNav);
    row->addWidget(categories_);
    stamps_ = new QListWidget(this);
    stamps_->setObjectName(QStringLiteral("stampList"));
    stamps_->setAccessibleName(tr("Штампы"));
    stamps_->setViewMode(QListView::IconMode);
    stamps_->setResizeMode(QListView::Adjust);
    stamps_->setMovement(QListView::Static);
    stamps_->setWordWrap(true);
    stamps_->setSpacing(m.grid * 2);
    const QSize thumb(m.thumbWidth, m.thumbWidth * 2 / 5);
    stamps_->setIconSize(thumb);
    stamps_->setGridSize(QSize(thumb.width() + m.grid * 4, thumb.height() + m.grid * 8));
    row->addWidget(stamps_, 1);
    column->addLayout(row, 1);
    note_ = new QLabel(this);
    note_->setObjectName(QStringLiteral("stampNote"));
    note_->setWordWrap(true);
    note_->setVisible(false);
    column->addWidget(note_);

    auto* actions = new QHBoxLayout();
    actions->setSpacing(m.grid * 2);
    create_ = new QPushButton(tr("&Создать…"), this);
    create_->setObjectName(QStringLiteral("stampCreate"));
    create_->setToolTip(tr("Свой штамп из картинки PNG или JPEG или из первой страницы PDF"));
    clipboard_ = new QPushButton(tr("Из &буфера обмена"), this);
    clipboard_->setObjectName(QStringLiteral("stampClipboard"));
    clipboard_->setToolTip(tr("Поставить картинку из буфера обмена как штамп"));
    favorite_ = new QPushButton(tr("В &избранное"), this);
    favorite_->setObjectName(QStringLiteral("stampFavorite"));
    rename_ = new QPushButton(tr("&Переименовать…"), this);
    rename_->setObjectName(QStringLiteral("stampRename"));
    delete_ = new QPushButton(tr("&Удалить"), this);
    delete_->setObjectName(QStringLiteral("stampDelete"));
    identity_ = new QPushButton(tr("I&dentity…"), this);
    identity_->setObjectName(QStringLiteral("stampIdentity"));
    identity_->setToolTip(tr("Имя, должность и организация для динамических штампов"));
    for (QPushButton* b : {create_, clipboard_, favorite_, rename_, delete_, identity_}) {
        actions->addWidget(b);
    }
    actions->addStretch(1);
    column->addLayout(actions);
    auto* buttons = new QDialogButtonBox(this);
    QPushButton* choose = buttons->addButton(tr("&Выбрать"), QDialogButtonBox::AcceptRole);
    choose->setObjectName(QStringLiteral("stampChoose"));
    choose->setDefault(true);
    buttons->addButton(QDialogButtonBox::Close);
    connect(buttons, &QDialogButtonBox::accepted, this, [this] {
        const QString id = selectedStamp();
        if (!id.isEmpty()) {
            emit stampChosen(id);
            accept();
        }
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    column->addWidget(buttons);

    connect(categories_, &QListWidget::currentRowChanged, this, [this] { fillStamps(); });
    connect(stamps_, &QListWidget::itemSelectionChanged, this, [this] { updateButtons(); });
    connect(stamps_, &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        emit stampChosen(item->data(kIdRole).toString());
        accept();
    });
    connect(create_, &QPushButton::clicked, this, &StampsPalette::createStamp);
    connect(clipboard_, &QPushButton::clicked, this, &StampsPalette::useClipboard);
    connect(favorite_, &QPushButton::clicked, this, &StampsPalette::toggleFavorite);
    connect(rename_, &QPushButton::clicked, this, &StampsPalette::renameStamp);
    connect(delete_, &QPushButton::clicked, this, &StampsPalette::deleteStamp);
    connect(identity_, &QPushButton::clicked, this, [this] { emit identityRequested(); });
    reload();
    showCategory(QLatin1String(stamps::kStandard));
}

void StampsPalette::reload() {
    const QString category = currentCategory();
    fillCategories();
    showCategory(category.isEmpty() ? QString::fromLatin1(stamps::kStandard) : category);
}

void StampsPalette::fillCategories() {
    const QSignalBlocker block(categories_);
    categories_->clear();
    QStringList ids{QLatin1String(kFavorites), QLatin1String(stamps::kStandard), QLatin1String(stamps::kSignHere),
                    QLatin1String(stamps::kDynamic)};
    for (const QString& c : catalog_.userCategories()) {
        ids << QLatin1String(kUserCategory) + c;
    }
    for (const QString& id : ids) {
        auto* item = new QListWidgetItem(categoryTitle(id), categories_);
        item->setData(kIdRole, id);
    }
}

void StampsPalette::showCategory(const QString& category) {
    for (int i = 0; i < categories_->count(); ++i) {
        if (categories_->item(i)->data(kIdRole).toString() == category) {
            categories_->setCurrentRow(i);
            fillStamps();
            return;
        }
    }
    if (categories_->count() > 1) {
        categories_->setCurrentRow(1);
    }
    fillStamps();
}

QString StampsPalette::currentCategory() const {
    const QListWidgetItem* item = categories_->currentItem();
    return item != nullptr ? item->data(kIdRole).toString() : QString();
}

void StampsPalette::fillStamps() {
    stamps_->clear();
    const QString category = currentCategory();
    std::vector<stamps::StampEntry> entries;
    if (category == QLatin1String(kFavorites)) {
        for (const QString& id : catalog_.favorites()) {
            if (auto e = catalog_.find(id)) {
                entries.push_back(std::move(*e));
            }
        }
    } else if (category.startsWith(QLatin1String(kUserCategory))) {
        const QString name = category.mid(5);
        for (stamps::StampEntry& e : catalog_.user()) {
            if (e.category == name) {
                entries.push_back(std::move(e));
            }
        }
    } else {
        for (stamps::StampEntry& e : catalog_.builtIn()) {
            if (e.category == category) {
                entries.push_back(std::move(e));
            }
        }
    }
    for (const stamps::StampEntry& e : entries) {
        auto* item = new QListWidgetItem(stamps_);
        item->setText(stampTitle(e));
        item->setData(kIdRole, e.id);
        item->setIcon(QIcon(QPixmap::fromImage(cachedThumbnail(catalog_, e, stamps_->iconSize()))));
        item->setToolTip(stampTitle(e));
    }
    if (entries.empty() && category == QLatin1String(kFavorites)) {
        note_->setText(tr("Избранных штампов нет: выберите штамп и нажмите «В избранное»."));
        note_->setVisible(true);
    } else {
        note_->setVisible(false);
    }
    updateButtons();
}

void StampsPalette::selectStamp(const QString& id) {
    for (int i = 0; i < stamps_->count(); ++i) {
        if (stamps_->item(i)->data(kIdRole).toString() == id) {
            stamps_->setCurrentRow(i);
            return;
        }
    }
}

QString StampsPalette::selectedStamp() const {
    const QListWidgetItem* item = stamps_->currentItem();
    return item != nullptr && item->isSelected() ? item->data(kIdRole).toString() : QString();
}

std::optional<stamps::StampEntry> StampsPalette::selectedEntry() const {
    const QString id = selectedStamp();
    return id.isEmpty() ? std::nullopt : catalog_.find(id);
}

void StampsPalette::updateButtons() {
    const std::optional<stamps::StampEntry> e = selectedEntry();
    const bool user = e && !e->builtIn;
    rename_->setEnabled(user);
    delete_->setEnabled(user);
    favorite_->setEnabled(e.has_value());
    favorite_->setText(e && catalog_.isFavorite(e->id) ? tr("Убрать из &избранного") : tr("В &избранное"));
}

std::optional<std::pair<QString, QString>> StampsPalette::askName(const QString& name) {
    QStringList categories = catalog_.userCategories();
    if (categories.isEmpty()) {
        categories << tr("Мои штампы");
    }
    if (namePrompt_) {
        return namePrompt_(name, categories);
    }
    QDialog d(this);
    d.setWindowTitle(tr("Свой штамп"));
    auto* form = new QFormLayout(&d);
    auto* category = new QComboBox(&d);
    category->setEditable(true);
    category->addItems(categories);
    form->addRow(tr("&Категория:"), category);
    auto* edit = new QLineEdit(name, &d);
    form->addRow(tr("&Имя:"), edit);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &d);
    connect(buttons, &QDialogButtonBox::accepted, &d, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &d, &QDialog::reject);
    form->addRow(buttons);
    if (d.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    return std::make_pair(category->currentText(), edit->text());
}

void StampsPalette::createStamp() {
    const QString file = filePrompt_ ? filePrompt_()
                                     : QFileDialog::getOpenFileName(this, tr("Создать штамп"), QString(),
                                                                    tr("Картинки и PDF (*.png *.jpg *.jpeg *.pdf)"));
    if (file.isEmpty()) {
        return;
    }
    const auto named = askName(QFileInfo(file).completeBaseName());
    if (!named) {
        return;
    }
    QString error;
    const std::optional<stamps::StampEntry> e = catalog_.createFromFile(file, 0, named->first, named->second, &error);
    if (!e) {
        note_->setText(tr("Не удалось создать штамп из «%1».").arg(QFileInfo(file).fileName()));
        note_->setVisible(true);
        return;
    }
    fillCategories();
    showCategory(QLatin1String(kUserCategory) + e->category);
    selectStamp(e->id);
}

void StampsPalette::useClipboard() {
    const QImage image = QApplication::clipboard()->image();
    if (image.isNull()) {
        note_->setText(tr("В буфере обмена нет картинки: скопируйте её и нажмите кнопку снова."));
        note_->setVisible(true);
        return;
    }
    emit clipboardStampChosen(image);
    accept();
}

void StampsPalette::toggleFavorite() {
    const std::optional<stamps::StampEntry> e = selectedEntry();
    if (!e) {
        return;
    }
    catalog_.setFavorite(e->id, !catalog_.isFavorite(e->id));
    if (currentCategory() == QLatin1String(kFavorites)) {
        fillStamps();
    } else {
        updateButtons();
    }
}

void StampsPalette::renameStamp() {
    const std::optional<stamps::StampEntry> e = selectedEntry();
    if (!e || e->builtIn) {
        return;
    }
    const auto named = askName(e->caption);
    if (!named) {
        return;
    }
    const std::optional<stamps::StampEntry> renamed = catalog_.rename(e->id, named->second, named->first);
    if (!renamed) {
        note_->setText(tr("Не удалось переименовать штамп."));
        note_->setVisible(true);
        return;
    }
    fillCategories();
    showCategory(QLatin1String(kUserCategory) + renamed->category);
    selectStamp(renamed->id);
}

void StampsPalette::deleteStamp() {
    const std::optional<stamps::StampEntry> e = selectedEntry();
    if (!e || e->builtIn) {
        return;
    }
    const QString title = stampTitle(*e);
    const bool yes = confirm_ ? confirm_(title)
                              : QMessageBox::question(this, tr("Удалить штамп"),
                                                      tr("Удалить штамп «%1»? Файл штампа будет удалён.").arg(title),
                                                      QMessageBox::Yes | QMessageBox::No,
                                                      QMessageBox::No) == QMessageBox::Yes;
    if (!yes || !catalog_.remove(e->id)) {
        return;
    }
    fillCategories();
    showCategory(QLatin1String(kUserCategory) + e->category);
}

}  // namespace ot::ui
