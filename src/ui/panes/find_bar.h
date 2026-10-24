// Панель «Найти» (план 2.5; эталон 3.2 «Поиск», 6.1 «Глобальная панель»; view-18): плавающая панель в правом
// верхнем углу документа (у каждой вкладки своя) — поле, счётчик «3 из 17», ▲ ▼ (Shift+F3 / F3, Ctrl+G),
// параметры «Только целые слова», «С учётом регистра», «Регулярное выражение», «Заменить» (3.4: поле
// «Заменить на», «Заменить» — текущее совпадение, «Заменить все» — предпросмотр и отчёт ReplaceDialog;
// Ctrl+Shift+H; edit-05), закрыть (Esc). Поиск — задачей потока документа (services/search_ops.h) страницами:
// совпадения приходят по мере поиска (опрос раз в 100 мс общего буфера — без вызовов из чужого потока в
// виджет), подсвечиваются все (PageView::setSearchHits), первым выбирается совпадение на текущей странице или
// после неё; новая строка или параметры, закрытие и Esc отменяют идущий поиск; долгий (> 0,5 с) — ещё и
// строкой фоновой задачи с отменой. Документ без текста (скан без OCR) — «Нет текста — распознать?» с кнопкой
// «Распознать текст» (8.1).
#pragma once

#include <QPointer>
#include <QTimer>

#include <memory>
#include <optional>
#include <vector>

#include "core/text/search.h"
#include "ui/widgets/floating_bar.h"

class QLabel;
class QMenu;

namespace ot {
class DocumentSession;
namespace search {
struct Control;
}
}  // namespace ot

namespace ot::ui {

class Chip;
class PageView;
class ReplaceDialog;
class SearchField;
class ToolButton;

class FindBar : public FloatingBar {
    Q_OBJECT
public:
    // Задержка поиска при наборе (мс): поиск идёт и по Enter сразу.
    static constexpr int kTypingDelayMs = 400;
    static constexpr int kPollMs = 100;
    static constexpr int kBackgroundTaskDelayMs = 500;

    FindBar(DocumentSession& session, PageView& view, QWidget* parent = nullptr);
    ~FindBar() override;

    SearchField* field() const { return field_; }
    QLabel* counterLabel() const { return counter_; }
    // «Нет текста — распознать?», «Неверное выражение» — чип с тоном.
    Chip* stateChip() const { return state_; }
    ToolButton* nextButton() const { return next_; }
    ToolButton* previousButton() const { return previous_; }
    ToolButton* replaceButton() const { return replace_; }
    ToolButton* ocrButton() const { return ocr_; }
    QMenu* optionsMenu() const { return options_; }
    SearchField* replaceField() const { return replaceField_; }
    ToolButton* replaceOneButton() const { return replaceOne_; }
    ToolButton* replaceAllButton() const { return replaceAll_; }
    ReplaceDialog* replaceDialog() const;

    // Ctrl+F: показать, фокус в поле, текст выделен.
    void activate();
    // Найти text с параметрами (Расширенный поиск, поле глобальной панели); focusPage/focusChar — выбрать
    // совпадение, начинающееся там (−1 — первое с текущей страницы).
    void find(const QString& text, std::optional<SearchOptions> options = std::nullopt, int focusPage = -1,
              int focusChar = -1);
    // Искать строку поля с текущими параметрами (повтор той же строки — заново).
    void startSearch();
    // F3 / Shift+F3: следующее / предыдущее по кругу (панель скрыта и строки нет — открыть панель).
    void findNext();
    void findPrevious();
    // Esc, «×»: скрыть, отменить поиск, снять подсветку; фокус — документу.
    void dismiss();

    // «Найти и заменить» (Ctrl+Shift+H): панель с полем «Заменить на»; фокус — в поле поиска (пустое) или в
    // поле замены.
    void activateReplace();
    void setReplaceMode(bool on);
    bool replaceMode() const { return replaceMode_; }
    // Документ нельзя править (только просмотр, только чтение, подпись): «Заменить» серая с причиной.
    void setReplaceBlocked(const QString& reason);
    // «Заменить» — текущее совпадение (потом — следующее); «Заменить все» — предпросмотр в ReplaceDialog.
    void replaceCurrent();
    void replaceAllPreview();
    bool isReplacing() const { return replacing_; }

    SearchOptions options() const;
    void setOptions(const SearchOptions& options);

    bool isSearching() const { return searching_; }
    int count() const { return static_cast<int>(hits_.size()); }
    int current() const { return current_; }
    const std::vector<SearchHit>& hits() const { return hits_; }
    QString query() const { return query_; }
    bool noText() const { return noText_; }
    bool hasError() const { return error_; }

signals:
    void advancedSearchRequested(const QString& text);
    void searchFinished(int count);
    void dismissed();
    // Замена сделана (заменено count) или не удалась (текст по-русски).
    void replaced(int count);
    void replaceFailed(const QString& message);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    struct Shared;

    void select(int index);
    void poll();
    void finish(int pageCount, int pagesDone, bool noText, bool aborted);
    void fail();
    void cancelSearch();
    void updateCounter();
    void retranslate();

    DocumentSession& session_;
    PageView& view_;
    SearchField* field_ = nullptr;
    QLabel* counter_ = nullptr;
    Chip* state_ = nullptr;
    ToolButton* previous_ = nullptr;
    ToolButton* next_ = nullptr;
    ToolButton* optionsButton_ = nullptr;
    ToolButton* replace_ = nullptr;
    SearchField* replaceField_ = nullptr;
    ToolButton* replaceOne_ = nullptr;
    ToolButton* replaceAll_ = nullptr;
    QPointer<ReplaceDialog> dialog_;
    bool replaceMode_ = false;
    bool replacing_ = false;
    QString replaceBlocked_;
    ToolButton* ocr_ = nullptr;
    ToolButton* close_ = nullptr;
    QMenu* options_ = nullptr;
    QAction* wholeWord_ = nullptr;
    QAction* caseSensitive_ = nullptr;
    QAction* regex_ = nullptr;
    QAction* advanced_ = nullptr;

    QTimer typing_;
    QTimer poll_;
    QTimer taskDelay_;
    std::shared_ptr<Shared> shared_;
    std::shared_ptr<search::Control> control_;
    quint64 generation_ = 0;
    quint64 task_ = 0;  // строка фоновой задачи (0 — нет)
    QString query_;
    SearchOptions searchedOptions_;
    std::vector<SearchHit> hits_;
    int current_ = -1;
    int startPage_ = 0;
    int focusPage_ = -1;
    int focusChar_ = -1;
    int pagesDone_ = 0;
    int pageCount_ = 0;
    bool searching_ = false;
    bool noText_ = false;
    bool error_ = false;
    bool stale_ = false;  // документ правили после поиска
};

}  // namespace ot::ui
