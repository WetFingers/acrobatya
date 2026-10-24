#include "ui/panes/attachments_pane.h"

#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFuture>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <exception>
#include <memory>

#include "services/document_session.h"
#include "services/nav_ops.h"
#include "services/settings.h"
#include "services/shell_launcher.h"
#include "ui/shell/document_view.h"
#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/widgets/empty_state.h"
#include "ui/widgets/toast.h"

namespace ot::ui {

namespace {

constexpr int kAttachIndexRole = Qt::UserRole + 1;

// Размер коротко, как в Проводнике: «48 Б», «12,4 КБ», «3,1 МБ».
QString attachSize(std::int64_t bytes) {
    if (bytes < 0) {
        return QStringLiteral("—");
    }
    constexpr double kKilo = 1024.0;
    if (bytes < 1024) {
        return AttachmentsPane::tr("%1 Б").arg(bytes);
    }
    if (bytes < 1024 * 1024) {
        return AttachmentsPane::tr("%1 КБ").arg(
            QLocale().toString(static_cast<double>(bytes) / kKilo, 'f', 1));
    }
    return AttachmentsPane::tr("%1 МБ").arg(
        QLocale().toString(static_cast<double>(bytes) / kKilo / kKilo, 'f', 1));
}

QIcon attachIcon(const QString& name, IconTone tone) {
    return QIcon(Icons::get(name, tone, Theme::sizes().iconRow));
}

}  // namespace

AttachmentsPane::AttachmentsPane(QWidget* parent) : NavPage(parent) {
    setObjectName(QStringLiteral("attachmentsPane"));
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);
    stack_ = new QStackedWidget(this);
    tree_ = new QTreeWidget(stack_);
    tree_->setObjectName(QStringLiteral("attachmentList"));
    tree_->setFrameShape(QFrame::NoFrame);
    tree_->setColumnCount(2);
    tree_->setRootIsDecorated(false);
    tree_->setUniformRowHeights(true);
    tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree_->header()->setStretchLastSection(false);
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    empty_ = new EmptyState(QStringLiteral("paperclip"), QString(), QString(), stack_);
    stack_->addWidget(tree_);
    stack_->addWidget(empty_);
    column->addWidget(stack_, 1);

    auto* bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("paneButtons"));
    auto* row = new QHBoxLayout(bar);
    const Metrics& m = Theme::sizes();
    row->setContentsMargins(m.grid * 2, m.grid * 2, m.grid * 2, m.grid * 2);
    row->setSpacing(m.grid * 2);
    open_ = new QPushButton(bar);
    open_->setObjectName(QStringLiteral("attachmentOpen"));
    open_->setIcon(Icons::icon(QStringLiteral("external-link")));
    save_ = new QPushButton(bar);
    save_->setObjectName(QStringLiteral("attachmentSave"));
    save_->setIcon(Icons::icon(QStringLiteral("download")));
    row->addWidget(open_);
    row->addWidget(save_);
    row->addStretch(1);
    column->addWidget(bar);

    connect(open_, &QPushButton::clicked, this, &AttachmentsPane::openCurrent);
    connect(save_, &QPushButton::clicked, this, &AttachmentsPane::saveCurrent);
    connect(tree_, &QTreeWidget::itemSelectionChanged, this, &AttachmentsPane::updateButtons);
    connect(tree_, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem*) {
        const int i = current();
        if (i >= 0 && canOpenHere(files_[static_cast<std::size_t>(i)])) {
            openCurrent();
        } else {
            saveCurrent();
        }
    });
    connect(&Theme::instance(), &Theme::themeChanged, this,
            [this] { fill(std::vector<attachments::Attachment>(files_)); });
    retranslate();
    updateButtons();
}

QString AttachmentsPane::title() const { return tr("Вложения"); }

bool AttachmentsPane::isEmptyShown() const { return stack_->currentWidget() == empty_; }

QString AttachmentsPane::tempFolder() {
    static const std::unique_ptr<QTemporaryDir> dir = [] {
        auto d = std::make_unique<QTemporaryDir>(QDir::tempPath() +
                                                 QStringLiteral("/AcroBatya-attachments-XXXXXX"));
        return d;
    }();
    return dir->isValid() ? dir->path() : QDir::tempPath();
}

bool AttachmentsPane::allowOpenNonPdf() { return Settings().get(settings::kAllowOpenAttachments); }

bool AttachmentsPane::canOpenHere(const attachments::Attachment& a) const {
    return canOpen(a) && (view_ == nullptr || !view_->isProtectedView());
}

bool AttachmentsPane::canOpen(const attachments::Attachment& a) {
    if (!a.embedded || attachments::isBlocked(a.fileName)) {
        return false;
    }
    return attachments::isPdf(a.fileName) || allowOpenNonPdf();
}

void AttachmentsPane::setDocument(DocumentView* view) {
    if (view == view_ && !dirty_) {
        return;
    }
    if (view_ != nullptr && view_ != view) {
        disconnect(view_, &DocumentView::protectedViewChanged, this, &AttachmentsPane::updateButtons);
        disconnect(&view_->session(), &DocumentSession::undoStateChanged, this,
                   &AttachmentsPane::documentEdited);
    }
    view_ = view;
    if (view != nullptr) {
        connect(view, &DocumentView::protectedViewChanged, this, &AttachmentsPane::updateButtons,
                Qt::UniqueConnection);
        connect(&view->session(), &DocumentSession::undoStateChanged, this, &AttachmentsPane::documentEdited,
                Qt::UniqueConnection);
    }
    dirty_ = true;
    ++generation_;
    if (isVisible()) {
        reload();
    } else {
        fill({});
    }
}

void AttachmentsPane::documentEdited() {
    dirty_ = true;
    if (isVisible()) {
        reload();
    }
}

void AttachmentsPane::activated() {
    if (dirty_) {
        reload();
    }
}

void AttachmentsPane::reload() {
    dirty_ = false;
    const quint64 generation = ++generation_;
    if (view_ == nullptr) {
        fill({});
        return;
    }
    nav::attachments(view_->session())
        .then(this,
              [this, generation](const std::vector<attachments::Attachment>& files) {
                  if (generation == generation_) {
                      fill(files);
                  }
              })
        .onFailed(this, [this, generation] {
            if (generation == generation_) {
                fill({});
            }
        });
}

void AttachmentsPane::fill(const std::vector<attachments::Attachment>& files) {
    files_ = files;
    tree_->clear();
    for (std::size_t i = 0; i < files.size(); ++i) {
        const attachments::Attachment& a = files[i];
        auto* item = new QTreeWidgetItem(tree_);
        const QString name = QString::fromStdString(a.fileName);
        item->setText(0, name);
        item->setText(1, attachSize(a.size));
        item->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        item->setData(0, kAttachIndexRole, static_cast<int>(i));
        const bool blocked = attachments::isBlocked(a.fileName);
        item->setIcon(0, attachIcon(blocked ? QStringLiteral("shield-alert") : QStringLiteral("file"),
                                    blocked ? IconTone::Warn : IconTone::Text));
        QStringList tip{name};
        if (!a.description.empty()) {
            tip << QString::fromStdString(a.description);
        }
        if (a.source == attachments::Source::Annotation) {
            tip << tr("Вложено в комментарий на странице %1").arg(a.page + 1);
        }
        if (a.modified >= 0) {
            tip << tr("Изменён: %1")
                       .arg(QLocale().toString(QDateTime::fromSecsSinceEpoch(a.modified),
                                               QLocale::ShortFormat));
        }
        if (blocked) {
            tip << tr("Файл такого типа может быть опасен: его можно только сохранить.");
        }
        item->setToolTip(0, tip.join(QLatin1Char('\n')));
        item->setData(0, Qt::AccessibleDescriptionRole, tip.mid(1).join(QStringLiteral(". ")));
    }
    stack_->setCurrentWidget(files.empty() ? static_cast<QWidget*>(empty_) : tree_);
    if (!files.empty()) {
        select(0);
    }
    updateButtons();
    emit listChanged();
    if (pendingOpen_) {
        const PendingOpen want = *pendingOpen_;
        pendingOpen_.reset();
        for (std::size_t i = 0; i < files_.size(); ++i) {
            const attachments::Attachment& a = files_[i];
            if (a.source == attachments::Source::Annotation && a.page == want.page &&
                a.objnum == want.objnum) {
                select(static_cast<int>(i));
                if (want.save) {
                    saveCurrent();
                } else if (canOpenHere(a)) {
                    openCurrent();
                } else {
                    saveCurrent();  // чёрный список и защищённый просмотр — только «Сохранить…»
                }
                break;
            }
        }
    }
}

void AttachmentsPane::openAnnotation(DocumentView* view, int page, int objnum, bool save) {
    pendingOpen_ = PendingOpen{page, objnum, save};
    if (view != view_) {
        setDocument(view);
    }
    reload();
}

int AttachmentsPane::current() const {
    const QList<QTreeWidgetItem*> items = tree_->selectedItems();
    return items.isEmpty() ? -1 : items.front()->data(0, kAttachIndexRole).toInt();
}

void AttachmentsPane::select(int index) {
    if (index >= 0 && index < tree_->topLevelItemCount()) {
        tree_->setCurrentItem(tree_->topLevelItem(index));
    }
}

void AttachmentsPane::updateButtons() {
    const int i = current();
    const bool has = i >= 0 && i < static_cast<int>(files_.size());
    const bool openable = has && canOpenHere(files_[static_cast<std::size_t>(i)]);
    open_->setEnabled(openable);
    save_->setEnabled(has && files_[static_cast<std::size_t>(i)].embedded);
    if (has && !openable) {
        const attachments::Attachment& a = files_[static_cast<std::size_t>(i)];
        open_->setToolTip(view_ != nullptr && view_->isProtectedView()
                              ? tr("Защищённый просмотр: вложения можно только сохранить.")
                          : attachments::isBlocked(a.fileName)
                              ? tr("Файлы такого типа не открываются — только «Сохранить…».")
                              : tr("Открытие вложений внешними программами выключено в Установках."));
    } else {
        open_->setToolTip(tr("Открыть вложение"));
    }
}

QString AttachmentsPane::extractToTemp(const attachments::Attachment& a) {
    static int counter = 0;
    const QString dir = tempFolder() + QStringLiteral("/%1").arg(++counter);
    QDir().mkpath(dir);
    const QString path =
        dir + QLatin1Char('/') + QString::fromStdString(attachments::safeFileName(a.fileName));
    try {
        nav::saveAttachment(view_->session(), a, path).waitForFinished();
    } catch (const std::exception&) {
        return {};
    }
    return path;
}

void AttachmentsPane::openCurrent() {
    const int i = current();
    if (view_ == nullptr || i < 0 || i >= static_cast<int>(files_.size())) {
        return;
    }
    const attachments::Attachment a = files_[static_cast<std::size_t>(i)];
    if (!canOpenHere(a)) {
        saveCurrent();  // чёрный список эталона 7.3 и защищённый просмотр — только «Сохранить как…»
        return;
    }
    const QString name = QString::fromStdString(attachments::safeFileName(a.fileName));
    if (attachments::isPdf(a.fileName)) {
        const QString path = extractToTemp(a);
        if (path.isEmpty()) {
            Toast::notify(window(), tr("Не удалось извлечь «%1».").arg(name), Tone::Danger);
            return;
        }
        emit openPdfRequested(path, name);
        return;
    }
    bool yes = false;
    if (confirm_) {
        yes = confirm_(name);
    } else {
        QMessageBox box(QMessageBox::Warning, tr("АкроБатя"),
                        tr("Открыть вложение «%1» внешней программой?").arg(name),
                        QMessageBox::Yes | QMessageBox::No, this);
        box.setInformativeText(
            tr("Открывайте вложения только из надёжных документов: файл может содержать "
               "вредоносный код."));
        box.button(QMessageBox::Yes)->setText(tr("&Открыть"));
        box.button(QMessageBox::No)->setText(tr("Отмена"));
        box.setDefaultButton(QMessageBox::No);
        box.setEscapeButton(QMessageBox::No);
        yes = box.exec() == QMessageBox::Yes;
    }
    if (!yes) {
        return;
    }
    const QString path = extractToTemp(a);
    if (path.isEmpty() || !ShellLauncher::instance().openFile(path)) {
        Toast::notify(window(), tr("Не удалось открыть «%1».").arg(name), Tone::Danger);
        return;
    }
    emit opened(path);
}

void AttachmentsPane::saveCurrent() {
    const int i = current();
    if (view_ == nullptr || i < 0 || i >= static_cast<int>(files_.size())) {
        return;
    }
    const attachments::Attachment a = files_[static_cast<std::size_t>(i)];
    if (!a.embedded) {
        return;
    }
    const QString name = QString::fromStdString(attachments::safeFileName(a.fileName));
    const QString suggested =
        QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).filePath(name);
    const QString path = chooser_ ? chooser_(suggested)
                                  : QFileDialog::getSaveFileName(this, tr("Сохранить вложение"), suggested);
    if (path.isEmpty()) {
        return;
    }
    nav::saveAttachment(view_->session(), a, path)
        .then(this,
              [this, path] {
                  Toast::notify(window(), tr("Сохранено: «%1».").arg(QFileInfo(path).fileName()), Tone::Ok);
                  emit saved(path);
              })
        .onFailed(this, [this, path] {
            Toast::notify(window(), tr("Не удалось сохранить «%1».").arg(QFileInfo(path).fileName()),
                          Tone::Danger);
        });
}

void AttachmentsPane::retranslate() {
    tree_->setHeaderLabels({tr("Имя"), tr("Размер")});
    tree_->setAccessibleName(tr("Вложения"));
    empty_->setText(tr("В документе нет вложений."));
    open_->setText(tr("&Открыть"));
    save_->setText(tr("&Сохранить…"));
    updateButtons();
}

void AttachmentsPane::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    NavPage::changeEvent(event);
}

}  // namespace ot::ui
