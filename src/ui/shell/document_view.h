// DocumentView — вкладка документа (эталон 6.1 «Документ», 4.2; view-27): PageView на холсте, сверху — стек
// полос сообщений (MessageStack, ui/shell/message_bar.h: виды и порядок по важности), внизу по центру —
// плавающая панель (QuickBar, setFloatingBar). Документ — сессия DocumentRegistry (вкладка держит shared_ptr;
// указателей MuPDF нет), пул и кеш тайлов — общие у окна. «Несохранено» — в истории есть шаг назад
// (UndoState::canUndo; сохранение историю очищает). Не-PDF (XPS, EPUB, CBZ, картинки, TXT, HTML) — «только
// просмотр»: полоса с кнопкой «Сохранить как PDF» (сигнал saveAsPdfRequested; view-01).
#pragma once

#include <QPointer>
#include <QWidget>

#include <map>
#include <memory>
#include <optional>
#include <vector>

#include "core/links/links.h"
#include "services/document_session.h"
#include "services/protected_view.h"
#include "services/recent.h"
#include "ui/view/layout.h"
#include "ui/view/view_style.h"
#include "ui/widgets/tone.h"

class QVBoxLayout;

namespace ot {
class RenderPool;
class TileCache;
}  // namespace ot

namespace ot::ui {

class FindBar;
class FormFillTool;
class QuickActionsBar;
class MessageBar;
class MessageStack;
class PageView;
class SignatureStatus;

class DocumentView : public QWidget {
    Q_OBJECT
public:
    DocumentView(std::shared_ptr<DocumentSession> session, RenderPool* pool, TileCache* cache,
                 QWidget* parent = nullptr);
    ~DocumentView() override;

    DocumentSession& session() const { return *session_; }
    const std::shared_ptr<DocumentSession>& sessionPtr() const { return session_; }
    quint64 id() const { return session_->id(); }
    PageView& view() const { return *view_; }
    // Пул рендера окна (миниатюры панели навигации рисуются в нём же, с низким приоритетом).
    RenderPool* pool() const { return pool_; }
    QString path() const;
    QString title() const;  // имя файла
    bool isModified() const { return modified_; }
    // Не PDF: правка и «Сохранить» недоступны, «Сохранить как PDF» — новый файл.
    bool isViewOnly() const;
    // Только чтение (2.5): PDF/A в режиме просмотра до «Включить редактирование» — правки и «Выбор объектов»
    // не меняют документ. canEdit — не «только просмотр» и не «только чтение».
    void setReadOnly(bool readOnly);
    bool isReadOnly() const { return readOnly_; }
    // Защищённый просмотр (2.5b; services/protected_view.h): только чтение, «Сохранить» и «Сохранить как»
    // запрещены (печать — можно), ссылки наружу и вложения не открываются. Origin::None — снять («Включить
    // все функции»).
    void setProtectedView(protected_view::Origin origin);
    protected_view::Origin protectedOrigin() const { return protected_; }
    bool isProtectedView() const { return protected_ != protected_view::Origin::None; }
    // Стиль рендера окна (2.5b; view_style.h): замена цветов, сглаживание, overprint — для PDF/X по его
    // OutputIntents.
    void setViewStyle(const ViewStyle& style);
    // Показ/скрытие комментариев (5.1; эталон 3.4 «Показать/скрыть все», Ctrl+Shift+8; основа фильтров 5.4):
    // визуальный фильтр вкладки по подтипам (бит — AnnotType), не /F Hidden — файл не меняется. Скрытые не
    // рисуются (ключ тайлов annotsHidden), не выбираются, их окна записок закрыты.
    void setAnnotsHidden(std::uint32_t mask);
    std::uint32_t annotsHidden() const { return annotsHidden_; }
    void setCommentsHidden(bool hidden);
    bool commentsHidden() const;
    // Подтипы-комментарии (annot::isComment) маской.
    static std::uint32_t commentTypesMask();
    // Фильтр панели «Комментарии» (5.4; эталон 3.4 «фильтры действуют на панель, оверлей, печать/сводку»):
    // скрытые фильтром номера объектов по страницам — не рисуются (поле annotFilter ключа тайлов), не
    // выбираются; файл не меняется. Пусто — фильтра нет.
    void setFilteredAnnots(std::map<int, std::vector<int>> byPage);
    const std::map<int, std::vector<int>>& filteredAnnots() const { return filtered_; }
    bool isPdfX() const { return pdfx_; }
    // Права /P документа (OpenSummary::permissions; после сохранения — заново).
    const Permissions& permissions() const { return permissions_; }
    // canEdit — не «только просмотр», не «только чтение», не защищённый просмотр; canSave — свой файл можно
    // записать; canAnnotate — править комментарии (ещё и право /P «комментарии»).
    bool canEdit() const { return !isViewOnly() && !readOnly_ && !isProtectedView(); }
    bool canSave() const { return !isViewOnly() && !isProtectedView(); }
    bool canAnnotate() const { return canEdit() && permissions_.annotate; }

    // Выделенные миниатюры (панель «Миниатюры», view-10; 2.6): страницы с нуля по возрастанию — «Выделенные
    // страницы» диалога «Печать».
    void setSelectedPages(QList<int> pages) { selectedPages_ = std::move(pages); }
    const QList<int>& selectedPages() const { return selectedPages_; }

    // Полосы сообщений над документом: виды и порядок — MessageStack; addMessage — своя полоса (после видов).
    MessageStack& messageStack() const { return *stack_; }
    MessageBar* addMessage(Tone tone, const QString& text);
    QList<MessageBar*> messages() const;

    // Панель «Найти» (Ctrl+F, 2.5) — плавающая в правом верхнем углу вида; у вкладки своя.
    FindBar& findBar() const { return *find_; }
    // 5.1: быстрые действия над выделенным текстом (comment-10).
    QuickActionsBar& quickActions() const { return *quickActions_; }
    // Подписи вкладки (4.6, ui/shell/signature_status.h): проверка, полоса, значки на полях.
    SignatureStatus& signatures() const { return *signatures_; }
    // Заполнение формы (6.1, ui/tools/forms/form_fill_tool.h): поля, редакторы, подсветка, полосы формы и
    // JavaScript.
    FormFillTool& forms() const { return *forms_; }

    // Плавающая панель (QuickBar) — поверх вида, по центру у нижнего края.
    void setFloatingBar(QWidget* bar);
    QWidget* floatingBar() const { return floating_; }

    // Страница после первого показа (--page, восстановление сеанса).
    void setInitialPage(int page);
    // Последний вид файла (недавние, services/recent.h): раскладка, масштаб и страница после первого показа.
    void setInitialView(const LastView& view);
    // /OpenAction файла (начальный вид, view-47): переход после первого показа, как по ссылке.
    void setInitialAction(const links::LinkAction& action);
    LastView currentView() const;

    // «Сохранить» в свой файл с ожиданием (подтверждение закрытия; полный поток сохранения — 2.5). false —
    // отказ или ошибка (errorString()).
    bool save();
    QString errorString() const { return error_; }

signals:
    void annotsHiddenChanged(std::uint32_t mask);
    void filteredAnnotsChanged();
    void modifiedChanged(bool modified);
    void readOnlyChanged(bool readOnly);
    void protectedViewChanged(bool on);
    void saveAsPdfRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void updateModified();
    void placeFloatingBar();
    // pageDisplay/defaultLayout → режим (auto и неизвестное — nullopt: как в документе).
    static std::optional<PageMode> defaultLayoutMode(const QString& value);
    // pageDisplay/defaultZoom: подгонка или проценты; auto — ничего.
    void applyDefaultZoom(const QString& value);
    void placeFindBar();
    void updateReplaceBlocked();

    std::shared_ptr<DocumentSession> session_;
    RenderPool* pool_ = nullptr;
    PageView* view_ = nullptr;
    MessageStack* stack_ = nullptr;
    FindBar* find_ = nullptr;
    QuickActionsBar* quickActions_ = nullptr;
    std::uint32_t annotsHidden_ = 0;            // 5.1: действия над выделенным текстом (вьюпорт вида)
    std::map<int, std::vector<int>> filtered_;  // 5.4: скрытые фильтром панели «Комментарии»
    QPointer<QWidget> floating_;
    QString error_;
    int initialPage_ = -1;
    LastView initialView_;
    std::optional<links::LinkAction> initialAction_;
    QString defaultZoom_;  // pageDisplay/defaultZoom при открытии (auto — как решит вид)
    bool modified_ = false;
    bool readOnly_ = false;
    protected_view::Origin protected_ = protected_view::Origin::None;
    Permissions permissions_;
    bool pdfx_ = false;
    ViewStyle viewStyle_;
    QList<int> selectedPages_;
    std::unique_ptr<SignatureStatus> signatures_;  // до дочерних виджетов: снимает свой слой с PageView
    std::unique_ptr<FormFillTool> forms_;          // так же: снимает слой и обработчик полей с PageView
};

}  // namespace ot::ui
