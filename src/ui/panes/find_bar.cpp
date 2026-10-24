#include "ui/panes/find_bar.h"

#include <QAction>
#include <QEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>

#include <algorithm>
#include <exception>
#include <mutex>

#include "services/background_tasks.h"
#include "services/document_session.h"
#include "services/search_ops.h"
#include "ui/dialogs/replace_dialog.h"
#include "ui/theme/theme.h"
#include "ui/view/page_view.h"
#include "ui/widgets/chip.h"
#include "ui/widgets/search_field.h"
#include "ui/widgets/tone.h"
#include "ui/widgets/tool_button.h"

namespace ot::ui {

// Буфер между потоком документа и панелью: колбэк прогресса дописывает, таймер панели забирает.
struct FindBar::Shared {
    std::mutex mutex;
    std::vector<SearchHit> pending;
    SearchProgress progress;
};

FindBar::FindBar(DocumentSession& session, PageView& view, QWidget* parent)
    : FloatingBar(parent), session_(session), view_(view) {
    setObjectName(QStringLiteral("findBar"));
    field_ = new SearchField(this);
    field_->setObjectName(QStringLiteral("findField"));
    field_->installEventFilter(this);
    addWidget(QStringLiteral("field"), field_);
    counter_ = new QLabel(this);
    counter_->setObjectName(QStringLiteral("findCounter"));
    addWidget(QStringLiteral("counter"), counter_);
    state_ = new Chip(QString(), Tone::Warn, this);
    state_->setObjectName(QStringLiteral("findState"));
    addWidget(QStringLiteral("state"), state_);
    ocr_ = addButton(QStringLiteral("ocr"), QStringLiteral("scan-text"), QString(), false);
    ocr_->setEnabled(false);  // «Распознать текст» — сессия 8.1
    ocr_->hide();
    previous_ = addButton(QStringLiteral("previous"), QStringLiteral("chevron-up"), QString(), false);
    next_ = addButton(QStringLiteral("next"), QStringLiteral("chevron-down"), QString(), false);
    optionsButton_ = addButton(QStringLiteral("options"), QStringLiteral("ellipsis"), QString(), false);
    replace_ = addButton(QStringLiteral("replace"), QStringLiteral("replace"), QString(), false);
    replace_->setCheckable(true);
    replaceField_ = new SearchField(this);
    replaceField_->setObjectName(QStringLiteral("replaceField"));
    replaceField_->installEventFilter(this);
    addWidget(QStringLiteral("replaceField"), replaceField_);
    replaceOne_ = addButton(QStringLiteral("replaceOne"), QString(), QString(), false);
    replaceOne_->setLabelVisible(true);
    replaceAll_ = addButton(QStringLiteral("replaceAll"), QString(), QString(), false);
    replaceAll_->setLabelVisible(true);
    for (QWidget* w : std::initializer_list<QWidget*>{replaceField_, replaceOne_, replaceAll_}) {
        w->hide();
    }
    addSeparator();
    close_ = addButton(QStringLiteral("close"), QStringLiteral("x"), QString(), false);

    options_ = new QMenu(this);
    options_->setObjectName(QStringLiteral("findOptions"));
    wholeWord_ = options_->addAction(QString());
    caseSensitive_ = options_->addAction(QString());
    regex_ = options_->addAction(QString());
    for (QAction* a : {wholeWord_, caseSensitive_, regex_}) {
        a->setCheckable(true);
        connect(a, &QAction::toggled, this, [this] {
            if (!field_->text().isEmpty()) {
                startSearch();
            }
        });
    }
    options_->addSeparator();
    advanced_ = options_->addAction(QString());
    connect(advanced_, &QAction::triggered, this, [this] { emit advancedSearchRequested(field_->text()); });

    connect(this, &FloatingBar::triggered, this, [this](const QString& id) {
        if (id == QLatin1String("previous")) {
            findPrevious();
        } else if (id == QLatin1String("next")) {
            findNext();
        } else if (id == QLatin1String("options")) {
            options_->popup(optionsButton_->mapToGlobal(QPoint(0, optionsButton_->height())));
        } else if (id == QLatin1String("close")) {
            dismiss();
        } else if (id == QLatin1String("replace")) {
            setReplaceMode(!replaceMode_);
        } else if (id == QLatin1String("replaceOne")) {
            replaceCurrent();
        } else if (id == QLatin1String("replaceAll")) {
            replaceAllPreview();
        }
    });
    connect(field_, &SearchField::searchRequested, this, [this](const QString& text) {
        typing_.stop();
        if (text == query_ && searchedOptions_ == options() && !stale_ && !error_ && count() > 0) {
            findNext();  // Enter по той же строке — следующее
        } else {
            startSearch();
        }
    });
    connect(field_, &SearchField::previousRequested, this, [this] { findPrevious(); });
    connect(field_, &QLineEdit::textEdited, this, [this] {
        typing_.start();
        updateCounter();
    });
    connect(field_, &SearchField::cleared, this, [this] {
        cancelSearch();
        query_.clear();
        hits_.clear();
        current_ = -1;
        noText_ = false;
        error_ = false;
        view_.clearSearchHits();
        updateCounter();
    });

    typing_.setSingleShot(true);
    typing_.setInterval(kTypingDelayMs);
    connect(&typing_, &QTimer::timeout, this, [this] {
        if (field_->text().isEmpty()) {
            emit field_->cleared();
        } else {
            startSearch();
        }
    });
    poll_.setInterval(kPollMs);
    connect(&poll_, &QTimer::timeout, this, &FindBar::poll);
    taskDelay_.setSingleShot(true);
    taskDelay_.setInterval(kBackgroundTaskDelayMs);
    connect(&taskDelay_, &QTimer::timeout, this, [this] {
        if (!searching_ || task_ != 0) {
            return;
        }
        std::weak_ptr<search::Control> control = control_;
        task_ = BackgroundTasks::shared().start(tr("Поиск «%1»").arg(query_), [control] {
            if (auto c = control.lock()) {
                c->cancel();
            }
        });
    });
    // Правка документа: найденное устарело — заново, пока панель открыта.
    connect(&session_, &DocumentSession::documentChanged, this, [this] {
        stale_ = !query_.isEmpty();
        if (stale_ && isVisible()) {
            typing_.start();
        }
    });
    retranslate();
    updateCounter();
    hide();
}

FindBar::~FindBar() { cancelSearch(); }

void FindBar::activate() {
    show();
    raise();
    field_->setFocus(Qt::ShortcutFocusReason);
    field_->selectAll();
}

ReplaceDialog* FindBar::replaceDialog() const { return dialog_.data(); }

void FindBar::activateReplace() {
    setReplaceMode(true);
    activate();
    if (!field_->text().isEmpty()) {
        replaceField_->setFocus(Qt::ShortcutFocusReason);
        replaceField_->selectAll();
    }
}

void FindBar::setReplaceMode(bool on) {
    replaceMode_ = on;
    replace_->setChecked(on);
    for (QWidget* w : std::initializer_list<QWidget*>{replaceField_, replaceOne_, replaceAll_}) {
        w->setVisible(on);
    }
    updateCounter();
    adjustSize();
}

void FindBar::setReplaceBlocked(const QString& reason) {
    replaceBlocked_ = reason;
    retranslate();
    updateCounter();
}

void FindBar::replaceCurrent() {
    if (!replaceBlocked_.isEmpty() || replacing_ || field_->text().isEmpty()) {
        return;
    }
    if (hits_.empty() || current_ < 0 || stale_ || field_->text() != query_) {
        startSearch();  // сначала найти — «Заменить» по найденному
        return;
    }
    const SearchHit hit = hits_[static_cast<std::size_t>(current_)];
    replacing_ = true;
    updateCounter();
    session_.replaceHit(hit, query_, searchedOptions_, replaceField_->text())
        .then(this, [this, hit](QFuture<edit::ReplaceReport> done) {
            replacing_ = false;
            int count = 0;
            try {
                const edit::ReplaceReport report = done.result();
                count = report.replaced;
                if (count == 0) {
                    const edit::ReplaceSkip skip =
                        report.skipped.empty() ? edit::ReplaceSkip::NoGlyphs : report.skipped.front().skip;
                    emit replaceFailed(ReplaceDialog::skipText(skip));
                }
            } catch (const std::exception&) {
                emit replaceFailed(tr("Не удалось заменить текст"));
            }
            // Найденное устарело — заново с того же места (следующее совпадение).
            focusPage_ = hit.page;
            focusChar_ = hit.firstChar;
            stale_ = false;
            typing_.stop();
            find(field_->text(), searchedOptions_, hit.page, hit.firstChar);
            updateCounter();
            if (count > 0) {
                emit replaced(count);
            }
        });
}

void FindBar::replaceAllPreview() {
    if (!replaceBlocked_.isEmpty() || replacing_ || field_->text().isEmpty()) {
        return;
    }
    const QString needle = field_->text();
    const QString replacement = replaceField_->text();
    const SearchOptions opts = options();
    replacing_ = true;
    updateCounter();
    session_.findReplace(needle, opts, replacement)
        .then(this, [this, needle, replacement, opts](QFuture<std::vector<edit::ReplaceMatch>> done) {
            replacing_ = false;
            std::vector<edit::ReplaceMatch> matches;
            try {
                matches = done.result();
            } catch (const std::exception&) {
                emit replaceFailed(tr("Не удалось найти совпадения"));
                updateCounter();
                return;
            }
            updateCounter();
            if (dialog_ != nullptr) {
                dialog_->close();
            }
            auto* dialog = new ReplaceDialog(needle, replacement, std::move(matches), window());
            dialog->setAttribute(Qt::WA_DeleteOnClose);
            dialog_ = dialog;
            connect(dialog, &ReplaceDialog::replaceAllRequested, this,
                    [this, dialog, needle, replacement, opts] {
                        replacing_ = true;
                        const quint64 task = BackgroundTasks::shared().start(tr("Замена «%1»").arg(needle),
                                                                             std::function<void()>());
                        session_
                            .replaceAll(needle, opts, replacement,
                                        [task](const edit::ReplaceProgress& p) {
                                            if (p.pageCount > 0) {
                                                BackgroundTasks::shared().setProgress(
                                                    task, p.pagesDone * 100 / p.pageCount);
                                            }
                                        })
                            .then(this, [this, dialog, task](QFuture<edit::ReplaceReport> finished) {
                                replacing_ = false;
                                BackgroundTasks::shared().finish(task);
                                int count = 0;
                                try {
                                    const edit::ReplaceReport report = finished.result();
                                    count = report.replaced;
                                    if (dialog_ == dialog) {
                                        dialog->showReport(report);
                                    }
                                } catch (const std::exception&) {
                                    emit replaceFailed(tr("Не удалось заменить текст"));
                                }
                                stale_ = false;
                                startSearch();
                                if (count > 0) {
                                    emit replaced(count);
                                }
                            });
                    });
            dialog->open();
        });
}

void FindBar::find(const QString& text, std::optional<SearchOptions> opts, int focusPage, int focusChar) {
    show();
    raise();
    if (opts) {
        setOptions(*opts);
    }
    field_->setText(text);
    focusPage_ = focusPage;
    focusChar_ = focusChar;
    startSearch();
    focusPage_ = focusPage;  // startSearch сбрасывает — фокус нужен этому поиску
    focusChar_ = focusChar;
}

SearchOptions FindBar::options() const {
    SearchOptions o;
    o.wholeWord = wholeWord_->isChecked();
    o.caseSensitive = caseSensitive_->isChecked();
    o.regex = regex_->isChecked();
    o.ignoreDiacritics = !o.caseSensitive && !o.regex;  // «оттиск» находит и «отти́ск», как индекс папки
    return o;
}

void FindBar::setOptions(const SearchOptions& o) {
    const QSignalBlocker b1(wholeWord_);
    const QSignalBlocker b2(caseSensitive_);
    const QSignalBlocker b3(regex_);
    wholeWord_->setChecked(o.wholeWord);
    caseSensitive_->setChecked(o.caseSensitive);
    regex_->setChecked(o.regex);
}

void FindBar::startSearch() {
    typing_.stop();
    cancelSearch();
    const QString text = field_->text();
    query_ = text;
    searchedOptions_ = options();
    hits_.clear();
    current_ = -1;
    noText_ = false;
    error_ = false;
    stale_ = false;
    pagesDone_ = 0;
    pageCount_ = session_.pageCount();
    focusPage_ = -1;
    focusChar_ = -1;
    view_.clearSearchHits();
    if (text.isEmpty() || !session_.isOpen()) {
        updateCounter();
        return;
    }
    startPage_ = view_.currentPage();
    searching_ = true;
    const quint64 generation = ++generation_;
    shared_ = std::make_shared<Shared>();
    control_ = std::make_shared<search::Control>();
    search::Request request;
    request.needle = text;
    request.options = searchedOptions_;
    std::weak_ptr<Shared> sink = shared_;
    QPointer<FindBar> self(this);
    search::run(session_, request, control_,
                [sink](const SearchProgress& progress, const std::vector<SearchHit>& fresh) {
                    if (auto s = sink.lock()) {
                        const std::lock_guard lock(s->mutex);
                        s->pending.insert(s->pending.end(), fresh.begin(), fresh.end());
                        s->progress = progress;
                    }
                })
        .then(this,
              [self, generation](const search::Result& r) {
                  if (self == nullptr || generation != self->generation_) {
                      return;
                  }
                  self->poll();
                  self->finish(r.pageCount, r.pagesDone, r.noText(), r.aborted);
              })
        .onFailed(this, [self, generation] {
            if (self != nullptr && generation == self->generation_) {
                self->fail();  // неверное регулярное выражение
            }
        });
    poll_.start();
    taskDelay_.start();
    updateCounter();
}

void FindBar::poll() {
    if (shared_ == nullptr) {
        return;
    }
    std::vector<SearchHit> fresh;
    {
        const std::lock_guard lock(shared_->mutex);
        fresh.swap(shared_->pending);
        pagesDone_ = shared_->progress.pagesDone;
        if (shared_->progress.pageCount > 0) {
            pageCount_ = shared_->progress.pageCount;
        }
    }
    if (task_ != 0 && pageCount_ > 0) {
        BackgroundTasks::shared().setProgress(task_, pagesDone_ * 100 / pageCount_);
    }
    if (fresh.empty()) {
        updateCounter();
        return;
    }
    hits_.insert(hits_.end(), std::make_move_iterator(fresh.begin()), std::make_move_iterator(fresh.end()));
    view_.setSearchHits(hits_);
    // Первое совпадение — заданное (Расширенный поиск) или с текущей страницы и дальше.
    int pick = current_;
    if (pick < 0) {
        for (int i = 0; i < count(); ++i) {
            const SearchHit& h = hits_[static_cast<std::size_t>(i)];
            if (focusPage_ >= 0 ? (h.page == focusPage_ && (focusChar_ < 0 || h.firstChar == focusChar_))
                                : h.page >= startPage_) {
                pick = i;
                break;
            }
        }
    }
    select(pick);
}

void FindBar::finish(int pageCount, int pagesDone, bool noText, bool aborted) {
    poll_.stop();
    taskDelay_.stop();
    searching_ = false;
    shared_.reset();
    if (task_ != 0) {
        BackgroundTasks::shared().finish(task_);
        task_ = 0;
    }
    pageCount_ = pageCount;
    pagesDone_ = pagesDone;
    noText_ = noText && !aborted;
    if (current_ < 0 && !hits_.empty()) {
        select(0);  // после текущей страницы ничего — по кругу к первому
    }
    updateCounter();
    emit searchFinished(count());
}

void FindBar::fail() {
    poll_.stop();
    taskDelay_.stop();
    searching_ = false;
    shared_.reset();
    if (task_ != 0) {
        BackgroundTasks::shared().finish(task_);
        task_ = 0;
    }
    error_ = true;
    updateCounter();
    emit searchFinished(0);
}

void FindBar::cancelSearch() {
    if (control_ != nullptr) {
        control_->cancel();
    }
    control_.reset();
    shared_.reset();
    ++generation_;
    poll_.stop();
    taskDelay_.stop();
    if (task_ != 0) {
        BackgroundTasks::shared().finish(task_);
        task_ = 0;
    }
    searching_ = false;
}

void FindBar::select(int index) {
    current_ = index >= 0 && index < count() ? index : -1;
    view_.setCurrentSearchHit(current_);
    updateCounter();
}

void FindBar::findNext() {
    if (field_->text().isEmpty()) {
        activate();
        return;
    }
    if (!isVisible()) {
        show();
    }
    if (field_->text() != query_ || stale_) {
        startSearch();
        return;
    }
    if (count() > 0) {
        select((current_ + 1) % count());
    }
}

void FindBar::findPrevious() {
    if (field_->text().isEmpty()) {
        activate();
        return;
    }
    if (!isVisible()) {
        show();
    }
    if (field_->text() != query_ || stale_) {
        startSearch();
        return;
    }
    if (count() > 0) {
        select(current_ <= 0 ? count() - 1 : current_ - 1);
    }
}

void FindBar::dismiss() {
    typing_.stop();
    cancelSearch();
    hits_.clear();
    current_ = -1;
    query_.clear();
    view_.clearSearchHits();
    updateCounter();
    hide();
    view_.setFocus();
    emit dismissed();
}

void FindBar::updateCounter() {
    QString text;
    if (searching_) {
        text = count() > 0
                   ? tr("%1 из %2…").arg(current_ + 1).arg(count())
                   : (pageCount_ > 0 ? tr("Поиск… %1 %").arg(pagesDone_ * 100 / pageCount_) : tr("Поиск…"));
    } else if (!query_.isEmpty() && !error_ && !noText_) {
        text = count() > 0 ? tr("%1 из %2").arg(current_ + 1).arg(count()) : tr("Не найдено");
    }
    counter_->setText(text);
    counter_->setVisible(!text.isEmpty());
    if (error_) {
        state_->setText(tr("Неверное выражение"));
        state_->setTone(Tone::Danger);
    } else if (noText_) {
        state_->setText(tr("Нет текста — распознать?"));
        state_->setTone(Tone::Warn);
    }
    state_->setVisible(error_ || noText_);
    ocr_->setVisible(noText_);
    previous_->setEnabled(count() > 0);
    next_->setEnabled(count() > 0);
    const bool canReplace = replaceBlocked_.isEmpty();
    replace_->setEnabled(canReplace);
    if (!canReplace && replaceMode_) {
        setReplaceMode(false);
    }
    replaceOne_->setEnabled(canReplace && !replacing_ && !field_->text().isEmpty());
    replaceAll_->setEnabled(canReplace && !replacing_ && !field_->text().isEmpty());
    adjustSize();
}

bool FindBar::eventFilter(QObject* watched, QEvent* event) {
    if (watched == replaceField_ && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape) {
            dismiss();
            return true;
        }
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
            key->modifiers() == Qt::NoModifier) {
            replaceCurrent();
            return true;
        }
    }
    if (watched == field_ && event->type() == QEvent::KeyPress) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Escape) {
            dismiss();
            return true;
        }
        if (key->key() == Qt::Key_Down && key->modifiers() == Qt::NoModifier) {
            findNext();
            return true;
        }
        if (key->key() == Qt::Key_Up && key->modifiers() == Qt::NoModifier) {
            findPrevious();
            return true;
        }
    }
    return FloatingBar::eventFilter(watched, event);
}

void FindBar::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
        updateCounter();
    }
    FloatingBar::changeEvent(event);
}

void FindBar::retranslate() {
    setAccessibleName(tr("Найти"));
    field_->setPlaceholderText(tr("Найти в документе"));
    field_->setAccessibleName(tr("Найти в документе"));
    previous_->setToolTip(tr("Предыдущее (Shift+F3)"));
    next_->setToolTip(tr("Следующее (F3)"));
    optionsButton_->setToolTip(tr("Параметры поиска"));
    replace_->setLabel(tr("Найти и заменить (Ctrl+Shift+H)"));
    if (!replaceBlocked_.isEmpty()) {
        replace_->setToolTip(replaceBlocked_);
    }
    replaceField_->setPlaceholderText(tr("Заменить на"));
    replaceField_->setAccessibleName(tr("Заменить на"));
    replaceOne_->setLabel(tr("Заменить"));
    replaceAll_->setLabel(tr("Заменить все…"));
    ocr_->setToolTip(tr("«Распознать текст» появится в версии 0.8"));
    close_->setToolTip(tr("Закрыть (Esc)"));
    wholeWord_->setText(tr("Только &целые слова"));
    caseSensitive_->setText(tr("С учётом &регистра"));
    regex_->setText(tr("Регулярное &выражение"));
    advanced_->setText(tr("Открыть &расширенный поиск…"));
}

}  // namespace ot::ui
