#include "ui/dialogs/search_window.h"

#include <QAbstractTextDocumentLayout>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCompleter>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QStandardPaths>
#include <QStringListModel>
#include <QStyledItemDelegate>
#include <QTextDocument>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <exception>

#include "services/document_session.h"
#include "services/folder_search.h"
#include "services/search_ops.h"
#include "services/state_db.h"
#include "ui/panes/find_bar.h"
#include "ui/shell/document_tabs.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shell/nav_pane.h"
#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/view/page_view.h"

namespace ot::ui {

namespace {

constexpr int kRowRole = Qt::UserRole + 1;     // индекс строки rows_ (−1 — узел)
constexpr int kMarkedRole = Qt::UserRole + 2;  // контекст с метками U+0002 / U+0003
constexpr int kPathRole = Qt::UserRole + 3;    // путь файла узла

QString plainContext(QString text) {
    text.remove(QChar(2));
    text.remove(QChar(3));
    return text;
}

// Контекст с совпадением жирным: метки U+0002 / U+0003 → <b>…</b> поверх экранированного текста.
QString markedHtml(const QString& marked) {
    QString html;
    QString part;
    for (const QChar c : marked) {
        if (c == QChar(2) || c == QChar(3)) {
            html += part.toHtmlEscaped();
            part.clear();
            html += c == QChar(2) ? QStringLiteral("<b>") : QStringLiteral("</b>");
        } else {
            part += c;
        }
    }
    return html + part.toHtmlEscaped();
}

// Строка контекста: текст с жирным совпадением (QTextDocument) поверх обычного фона строки дерева.
class SnippetDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        const QString marked = index.data(kMarkedRole).toString();
        if (marked.isEmpty()) {
            QStyledItemDelegate::paint(painter, option, index);
            return;
        }
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        opt.text.clear();
        QStyle* style = opt.widget != nullptr ? opt.widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
        const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &opt, opt.widget);
        QTextDocument doc;
        doc.setDefaultFont(opt.font);
        doc.setDocumentMargin(0);
        doc.setHtml(markedHtml(marked));
        painter->save();
        painter->translate(textRect.left(), textRect.top() + (textRect.height() - doc.size().height()) / 2);
        painter->setClipRect(QRect(0, 0, textRect.width(), textRect.height()));
        QAbstractTextDocumentLayout::PaintContext ctx;
        ctx.palette = opt.palette;
        ctx.palette.setColor(QPalette::Text, (opt.state & QStyle::State_Selected)
                                                 ? opt.palette.color(QPalette::HighlightedText)
                                                 : opt.palette.color(QPalette::Text));
        doc.documentLayout()->draw(painter, ctx);
        painter->restore();
    }
};

QString csvField(const QString& value) {
    if (value.contains(QLatin1Char(';')) || value.contains(QLatin1Char('"')) ||
        value.contains(QLatin1Char('\n')) || value.contains(QLatin1Char('\r'))) {
        QString quoted = value;
        quoted.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QLatin1Char('"') + quoted + QLatin1Char('"');
    }
    return value;
}

}  // namespace

SearchWindow::SearchWindow(MainWindow& window) : QDialog(&window, Qt::Window), window_(window) {
    setObjectName(QStringLiteral("searchWindow"));
    const Metrics& m = Theme::sizes();
    resize(m.dialogWidth, m.dialogHeight);

    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(m.grid * 4, m.grid * 4, m.grid * 4, m.grid * 4);
    column->setSpacing(m.grid * 2);
    auto* form = new QGridLayout();
    form->setHorizontalSpacing(m.grid * 2);
    form->setVerticalSpacing(m.grid * 2);
    column->addLayout(form);

    whatLabel_ = new QLabel(this);
    form->addWidget(whatLabel_, 0, 0);
    query_ = new QLineEdit(this);
    query_->setObjectName(QStringLiteral("searchQuery"));
    query_->setClearButtonEnabled(true);
    whatLabel_->setBuddy(query_);
    suggestModel_ = new QStringListModel(this);
    completer_ = new QCompleter(suggestModel_, this);
    completer_->setCaseSensitivity(Qt::CaseInsensitive);
    completer_->setCompletionMode(QCompleter::UnfilteredPopupCompletion);
    query_->setCompleter(completer_);
    connect(query_, &QLineEdit::textEdited, this,
            [this](const QString& text) { suggestModel_->setStringList(suggestions(text)); });
    connect(query_, &QLineEdit::returnPressed, this, [this] {
        if (!completer_->popup()->isVisible()) {
            start();
        }
    });
    form->addWidget(query_, 0, 1, 1, 3);
    find_ = new QPushButton(this);
    find_->setObjectName(QStringLiteral("searchFind"));
    find_->setDefault(true);
    connect(find_, &QPushButton::clicked, this, &SearchWindow::start);
    form->addWidget(find_, 0, 4);
    stop_ = new QPushButton(this);
    stop_->setObjectName(QStringLiteral("searchStop"));
    stop_->setEnabled(false);
    connect(stop_, &QPushButton::clicked, this, &SearchWindow::stop);
    form->addWidget(stop_, 0, 5);

    whereLabel_ = new QLabel(this);
    form->addWidget(whereLabel_, 1, 0);
    scope_ = new QComboBox(this);
    scope_->setObjectName(QStringLiteral("searchScope"));
    scope_->addItems({QString(), QString(), QString()});
    whereLabel_->setBuddy(scope_);
    connect(scope_, &QComboBox::currentIndexChanged, this, &SearchWindow::updateScopeControls);
    form->addWidget(scope_, 1, 1);
    folder_ = new QLineEdit(this);
    folder_->setObjectName(QStringLiteral("searchFolder"));
    form->addWidget(folder_, 1, 2, 1, 2);
    browse_ = new QPushButton(this);
    connect(browse_, &QPushButton::clicked, this, &SearchWindow::browse);
    form->addWidget(browse_, 1, 4);
    recursive_ = new QCheckBox(this);
    recursive_->setChecked(true);
    form->addWidget(recursive_, 2, 2, 1, 2);

    auto* options = new QHBoxLayout();
    options->setSpacing(m.grid * 4);
    wholeWord_ = new QCheckBox(this);
    caseSensitive_ = new QCheckBox(this);
    bookmarks_ = new QCheckBox(this);
    attachments_ = new QCheckBox(this);
    for (QCheckBox* box : {wholeWord_, caseSensitive_, bookmarks_, attachments_}) {
        options->addWidget(box);
    }
    options->addStretch(1);
    form->addLayout(options, 3, 1, 1, 5);
    form->setColumnStretch(2, 1);

    tree_ = new QTreeWidget(this);
    tree_->setObjectName(QStringLiteral("searchResults"));
    tree_->setHeaderHidden(true);
    tree_->setColumnCount(1);
    tree_->setUniformRowHeights(true);
    tree_->setItemDelegate(new SnippetDelegate(tree_));
    tree_->setIconSize(QSize(m.iconRow, m.iconRow));
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        const int row = item->data(0, kRowRole).toInt();
        if (row >= 0 && row < rows_.size()) {
            openRow(rows_[row]);
        }
    });
    column->addWidget(tree_, 1);

    auto* bottom = new QHBoxLayout();
    status_ = new QLabel(this);
    status_->setObjectName(QStringLiteral("searchStatus"));
    bottom->addWidget(status_, 1);
    export_ = new QPushButton(this);
    export_->setObjectName(QStringLiteral("searchExport"));
    export_->setEnabled(false);
    connect(export_, &QPushButton::clicked, this, &SearchWindow::exportDialog);
    bottom->addWidget(export_);
    close_ = new QPushButton(this);
    connect(close_, &QPushButton::clicked, this, &QDialog::close);
    bottom->addWidget(close_);
    column->addLayout(bottom);

    retranslate();
    setScope(window_.currentDocument() != nullptr ? Scope::Current : Scope::Folder);
    folder_->setText(
        QDir::toNativeSeparators(window_.currentDocument() != nullptr
                                     ? QFileInfo(window_.currentDocument()->path()).absolutePath()
                                     : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)));
}

SearchWindow::~SearchWindow() {
    stop();
    if (folderFuture_.isRunning()) {
        folderFuture_.waitForFinished();  // поиск по папке держит FolderSearch — дождаться до удаления
    }
}

void SearchWindow::setQuery(const QString& text) { query_->setText(text); }

void SearchWindow::setScope(Scope s) { scope_->setCurrentIndex(static_cast<int>(s)); }

SearchWindow::Scope SearchWindow::scope() const { return static_cast<Scope>(scope_->currentIndex()); }

void SearchWindow::setFolder(const QString& folder) { folder_->setText(QDir::toNativeSeparators(folder)); }

QString SearchWindow::folder() const { return QDir::fromNativeSeparators(folder_->text().trimmed()); }

void SearchWindow::setRecursive(bool on) { recursive_->setChecked(on); }

void SearchWindow::setWholeWord(bool on) { wholeWord_->setChecked(on); }

void SearchWindow::setCaseSensitive(bool on) { caseSensitive_->setChecked(on); }

void SearchWindow::setIncludeBookmarks(bool on) { bookmarks_->setChecked(on); }

void SearchWindow::setIncludeAttachments(bool on) { attachments_->setChecked(on); }

void SearchWindow::updateScopeControls() {
    const bool folder = scope() == Scope::Folder;
    folder_->setEnabled(folder);
    browse_->setEnabled(folder);
    recursive_->setEnabled(folder);
    // Индекс папки: всегда целые слова без регистра и диакритики, только текст страниц.
    for (QCheckBox* box : {wholeWord_, caseSensitive_, bookmarks_, attachments_}) {
        box->setEnabled(!folder);
        box->setToolTip(folder
                            ? tr("Для папки — только по тексту страниц, целыми словами, без учёта регистра")
                            : QString());
    }
}

void SearchWindow::browse() {
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Папка для поиска"), folder());
    if (!dir.isEmpty()) {
        setFolder(dir);
    }
}

QStringList SearchWindow::suggestions(const QString& text) const {
    return FolderSearch::suggest(StateDb::shared(), text);
}

void SearchWindow::start() {
    stop();
    if (folderFuture_.isRunning()) {
        folderFuture_.waitForFinished();
    }
    const QString needle = query_->text().trimmed();
    tree_->clear();
    rows_.clear();
    files_ = 0;
    hits_ = 0;
    filesDone_ = 0;
    filesTotal_ = 0;
    stopped_ = false;
    running_ = 0;
    controls_.clear();
    const quint64 generation = ++generation_;
    searched_ = needle;
    if (needle.isEmpty()) {
        updateStatus();
        return;
    }
    SearchOptions options;
    options.wholeWord = wholeWord_->isChecked();
    options.caseSensitive = caseSensitive_->isChecked();
    options.ignoreDiacritics = !options.caseSensitive;
    if (scope() == Scope::Folder) {
        options = {};
        options.wholeWord = true;
        options.ignoreDiacritics = true;
        searchedOptions_ = options;
        const QString dir = folder();
        if (dir.isEmpty() || !QFileInfo(dir).isDir()) {
            status_->setText(tr("Папка не найдена."));
            return;
        }
        folderSearch_ = std::make_unique<FolderSearch>(StateDb::shared());
        FolderSearch* fs = folderSearch_.get();
        connect(fs, &FolderSearch::progress, this, [this, generation](const FolderSearchProgress& p) {
            if (generation == generation_) {
                filesDone_ = p.filesDone;
                filesTotal_ = p.filesTotal;
                updateStatus();
            }
        });
        connect(fs, &FolderSearch::fileFound, this, [this, generation](const FolderFileHits& hits) {
            if (generation == generation_) {
                addFolderFile(hits);
            }
        });
        connect(fs, &FolderSearch::finished, this, [this, generation](const FolderSearchResult& result) {
            if (generation == generation_) {
                finishFolder(result);
            }
        });
        FolderSearchOptions fo;
        fo.recursive = recursive_->isChecked();
        running_ = 1;
        folderFuture_ = fs->start(dir, needle, fo);
    } else {
        searchedOptions_ = options;
        QList<DocumentView*> targets;
        if (scope() == Scope::Current) {
            if (DocumentView* view = window_.currentDocument()) {
                targets << view;
            }
        } else {
            for (int i = 0; i < window_.documentCount(); ++i) {
                targets << window_.document(i);
            }
        }
        if (targets.isEmpty()) {
            status_->setText(tr("Нет открытых документов."));
            return;
        }
        running_ = static_cast<int>(targets.size());
        for (DocumentView* view : targets) {
            runDocument(view, options);
        }
    }
    updateStatus();
}

void SearchWindow::runDocument(DocumentView* view, const SearchOptions& options) {
    search::Request request;
    request.needle = searched_;
    request.options = options;
    request.snippets = true;
    auto control = std::make_shared<search::Control>();
    controls_.push_back(control);
    const QString path = view->path();
    const quint64 id = view->id();
    const quint64 generation = generation_;
    QPointer<SearchWindow> self(this);
    search::runAll(view->session(), request, bookmarks_->isChecked(), attachments_->isChecked(), control)
        .then(this,
              [self, generation, path, id](const search::DocumentResults& r) {
                  if (self != nullptr && generation == self->generation_) {
                      self->addDocumentResult(path, id, r.text, r.bookmarks, r.attachments);
                      self->doneOne();
                  }
              })
        .onFailed(this,
                  [self, generation] {
                      if (self != nullptr && generation == self->generation_) {
                          self->doneOne();  // неверное выражение или документ закрыт
                      }
                  })
        .onCanceled(this, [self, generation] {
            if (self != nullptr && generation == self->generation_) {
                self->doneOne();
            }
        });
}

QTreeWidgetItem* SearchWindow::fileItem(const QString& path, const QString& title) {
    auto* item = new QTreeWidgetItem(tree_);
    item->setText(0, title);
    item->setToolTip(0, QDir::toNativeSeparators(path));
    item->setIcon(0, Icons::icon(QStringLiteral("file-text")));
    item->setData(0, kRowRole, -1);
    item->setData(0, kPathRole, path);
    return item;
}

void SearchWindow::addMarked(const QString& path, const QString& title, Row::Kind kind) {
    Row row;
    row.kind = kind;
    row.path = path;
    rows_ << row;
    QTreeWidgetItem* file = fileItem(path, title);
    file->setData(0, kRowRole, static_cast<int>(rows_.size()) - 1);
    file->setForeground(0, Theme::colors().textMuted.color());
    if (kind == Row::Kind::NoText) {
        // «Распознать текст» — сессия 8.1: кнопка есть, серая.
        auto* child = new QTreeWidgetItem(file);
        child->setData(0, kRowRole, static_cast<int>(rows_.size()) - 1);
        auto* ocr = new QToolButton(tree_);
        ocr->setObjectName(QStringLiteral("searchOcr"));
        ocr->setText(tr("Распознать текст"));
        ocr->setToolTip(tr("«Распознать текст» появится в версии 0.8"));
        ocr->setEnabled(false);
        tree_->setItemWidget(child, 0, ocr);
        file->setExpanded(true);
    }
}

void SearchWindow::addDocumentResult(const QString& path, quint64 session, const search::Result& result,
                                     const QVector<search::BookmarkHit>& bookmarks,
                                     const QVector<search::AttachmentHits>& attachments) {
    const QString name = QFileInfo(path).fileName();
    if (result.noText()) {
        addMarked(path, tr("%1 — нет текста, распознать?").arg(name), Row::Kind::NoText);
        return;
    }
    if (result.hits.empty() && bookmarks.isEmpty() && attachments.isEmpty()) {
        return;
    }
    int count = 0;
    QTreeWidgetItem* file = fileItem(path, name);
    auto addHits = [&](QTreeWidgetItem* parent, const std::vector<SearchHit>& hits,
                       const QStringList& snippets, Row::Kind kind, const QString& detail) {
        QTreeWidgetItem* pageItem = nullptr;
        int lastPage = -1;
        for (std::size_t i = 0; i < hits.size(); ++i) {
            const SearchHit& h = hits[i];
            if (pageItem == nullptr || h.page != lastPage) {
                pageItem = new QTreeWidgetItem(parent);
                pageItem->setText(0, tr("Страница %1").arg(h.page + 1));
                pageItem->setExpanded(true);
                lastPage = h.page;
            }
            Row row;
            row.kind = kind;
            row.path = path;
            row.page = h.page;
            row.context =
                i < static_cast<std::size_t>(snippets.size()) ? snippets[static_cast<int>(i)] : QString();
            row.detail = detail;
            row.session = session;
            row.firstChar = h.firstChar;
            rows_ << row;
            pageItem->setData(0, kRowRole, static_cast<int>(rows_.size()) - 1);
            auto* leaf = new QTreeWidgetItem(pageItem);
            leaf->setText(0, plainContext(row.context));
            leaf->setData(0, kMarkedRole, row.context);
            leaf->setData(0, kRowRole, static_cast<int>(rows_.size()) - 1);
            ++count;
        }
    };
    addHits(file, result.hits, result.snippets, Row::Kind::Text, QString());
    if (!bookmarks.isEmpty()) {
        auto* node = new QTreeWidgetItem(file);
        node->setText(0, tr("Закладки"));
        node->setIcon(0, Icons::icon(QStringLiteral("bookmark")));
        for (const search::BookmarkHit& b : bookmarks) {
            Row row;
            row.kind = Row::Kind::Bookmark;
            row.path = path;
            row.page = b.page;
            row.detail = b.title;
            row.session = session;
            rows_ << row;
            auto* leaf = new QTreeWidgetItem(node);
            leaf->setText(0, b.page >= 0 ? tr("%1 — стр. %2").arg(b.title).arg(b.page + 1) : b.title);
            leaf->setData(0, kRowRole, static_cast<int>(rows_.size()) - 1);
            ++count;
        }
        node->setExpanded(true);
    }
    for (const search::AttachmentHits& a : attachments) {
        auto* node = new QTreeWidgetItem(file);
        node->setText(0, tr("Вложение «%1»").arg(a.fileName));
        node->setIcon(0, Icons::icon(QStringLiteral("paperclip")));
        addHits(node, a.hits, a.snippets, Row::Kind::Attachment, a.fileName);
        node->setExpanded(true);
    }
    file->setText(0, tr("%1 (%2)").arg(name).arg(count));
    file->setExpanded(true);
    ++files_;
    hits_ += count;
}

void SearchWindow::addFolderFile(const FolderFileHits& hits) {
    const QString name = QFileInfo(hits.path).fileName();
    QTreeWidgetItem* file = fileItem(hits.path, tr("%1 (%2)").arg(name).arg(hits.pages.size()));
    for (const FolderPageHit& p : hits.pages) {
        Row row;
        row.path = hits.path;
        row.page = p.page;
        row.context = p.snippet;
        rows_ << row;
        auto* pageItem = new QTreeWidgetItem(file);
        pageItem->setText(0, tr("Страница %1").arg(p.page + 1));
        pageItem->setData(0, kRowRole, static_cast<int>(rows_.size()) - 1);
        auto* leaf = new QTreeWidgetItem(pageItem);
        leaf->setText(0, plainContext(p.snippet));
        leaf->setData(0, kMarkedRole, p.snippet);
        leaf->setData(0, kRowRole, static_cast<int>(rows_.size()) - 1);
        pageItem->setExpanded(true);
    }
    file->setExpanded(true);
    ++files_;
    hits_ += static_cast<int>(hits.pages.size());
    export_->setEnabled(true);
}

void SearchWindow::finishFolder(const FolderSearchResult& result) {
    for (const QString& path : result.encrypted) {
        addMarked(path, tr("%1 — защищён паролем, пропущен").arg(QFileInfo(path).fileName()),
                  Row::Kind::Encrypted);
    }
    for (const QString& path : result.noText) {
        addMarked(path, tr("%1 — нет текста, распознать?").arg(QFileInfo(path).fileName()),
                  Row::Kind::NoText);
    }
    for (const QString& path : result.failed) {
        addMarked(path, tr("%1 — не открылся").arg(QFileInfo(path).fileName()), Row::Kind::Failed);
    }
    stopped_ = stopped_ || result.cancelled;
    doneOne();
}

void SearchWindow::doneOne() {
    running_ = std::max(0, running_ - 1);
    updateStatus();
    if (running_ == 0) {
        emit searchFinished();
    }
}

void SearchWindow::stop() {
    if (running_ == 0) {
        return;
    }
    stopped_ = true;
    for (const auto& control : controls_) {
        control->cancel();
    }
    if (folderSearch_ != nullptr) {
        folderSearch_->cancel();
    }
}

void SearchWindow::updateStatus() {
    const bool running = running_ > 0;
    find_->setEnabled(!running);
    stop_->setEnabled(running);
    export_->setEnabled(!running && !rows_.isEmpty());
    if (running) {
        status_->setText(filesTotal_ > 0 ? tr("Поиск… файлов: %1 из %2").arg(filesDone_).arg(filesTotal_)
                                         : tr("Поиск…"));
        return;
    }
    if (searched_.isEmpty()) {
        status_->clear();
    } else if (hits_ == 0) {
        status_->setText(stopped_ ? tr("Остановлено. Ничего не найдено.") : tr("Ничего не найдено."));
    } else {
        const QString found = tr("Найдено совпадений: %1 · документов: %2").arg(hits_).arg(files_);
        status_->setText(stopped_ ? tr("Остановлено. %1").arg(found) : found);
    }
}

QByteArray SearchWindow::toCsv(const QVector<Row>& rows) {
    QString out = QString(QChar(0xFEFF));  // BOM: Excel читает UTF-8
    out += tr("Файл;Путь;Страница;Где;Контекст") + QStringLiteral("\r\n");
    for (const Row& r : rows) {
        QString where;
        switch (r.kind) {
            case Row::Kind::Text:
                where = tr("текст");
                break;
            case Row::Kind::Bookmark:
                where = tr("закладка «%1»").arg(r.detail);
                break;
            case Row::Kind::Attachment:
                where = tr("вложение «%1»").arg(r.detail);
                break;
            case Row::Kind::Encrypted:
                where = tr("защищён паролем — пропущен");
                break;
            case Row::Kind::Failed:
                where = tr("не открылся");
                break;
            case Row::Kind::NoText:
                where = tr("нет текста");
                break;
        }
        const QStringList fields = {QFileInfo(r.path).fileName(), QDir::toNativeSeparators(r.path),
                                    r.page >= 0 ? QString::number(r.page + 1) : QString(), where,
                                    plainContext(r.context)};
        QStringList quoted;
        for (const QString& f : fields) {
            quoted << csvField(f);
        }
        out += quoted.join(QLatin1Char(';')) + QStringLiteral("\r\n");
    }
    return out.toUtf8();
}

bool SearchWindow::exportCsv(const QString& path) const {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const QByteArray data = toCsv(rows_);
    return file.write(data) == data.size();
}

void SearchWindow::exportDialog() {
    const QString suggested = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
                                  .filePath(tr("Результаты поиска.csv"));
    const QString file =
        QFileDialog::getSaveFileName(this, tr("Экспорт результатов"), suggested, tr("Таблица CSV (*.csv)"));
    if (!file.isEmpty() && !exportCsv(file)) {
        status_->setText(tr("Не удалось записать «%1».").arg(QFileInfo(file).fileName()));
    }
}

void SearchWindow::openRow(const Row& row) {
    auto showSession = [this](quint64 id) -> DocumentView* {
        const int index = window_.tabs().indexOf(id);
        if (index < 0) {
            return nullptr;
        }
        window_.showDocument(index);
        return window_.currentDocument();
    };
    DocumentView* view = nullptr;
    if (row.session != 0) {
        view = showSession(row.session);
    }
    if (view == nullptr) {
        if (!window_.openFile(row.path, row.kind == Row::Kind::Text ? row.page : -1)) {
            return;
        }
        view = window_.currentDocument();
    }
    if (view == nullptr) {
        return;
    }
    switch (row.kind) {
        case Row::Kind::Text:
            view->findBar().find(searched_, searchedOptions_, row.page,
                                 row.session != 0 ? row.firstChar : -1);
            break;
        case Row::Kind::Bookmark:
            if (row.page >= 0) {
                view->view().goToPage(row.page);
            }
            break;
        case Row::Kind::Attachment:
            window_.navPane().showPane(QStringLiteral("attachments"));
            break;
        default:
            break;
    }
    window_.activateWindow();
}

void SearchWindow::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QDialog::changeEvent(event);
}

void SearchWindow::closeEvent(QCloseEvent* event) {
    stop();
    QDialog::closeEvent(event);
}

void SearchWindow::retranslate() {
    setWindowTitle(tr("Расширенный поиск"));
    whatLabel_->setText(tr("&Что искать:"));
    whereLabel_->setText(tr("&Где искать:"));
    query_->setPlaceholderText(tr("Слово или фраза"));
    scope_->setItemText(0, tr("В текущем документе"));
    scope_->setItemText(1, tr("Во всех открытых документах"));
    scope_->setItemText(2, tr("В папке"));
    browse_->setText(tr("&Обзор…"));
    recursive_->setText(tr("Включая &подпапки"));
    wholeWord_->setText(tr("Только &целые слова"));
    caseSensitive_->setText(tr("С учётом &регистра"));
    bookmarks_->setText(tr("Включить &закладки"));
    attachments_->setText(tr("Включить &вложения"));
    find_->setText(tr("&Найти"));
    stop_->setText(tr("&Стоп"));
    export_->setText(tr("&Экспорт CSV…"));
    close_->setText(tr("Закрыть"));
    updateScopeControls();
    updateStatus();
}

}  // namespace ot::ui
