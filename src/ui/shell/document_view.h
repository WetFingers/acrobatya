// DocumentView — вкладка документа (эталон 6.1 «Документ», 4.2; view-27): PageView на холсте, сверху — стек
// полос сообщений (MessageStack, ui/shell/message_bar.h: виды и порядок по важности), внизу по центру —
// плавающая панель (QuickBar, setFloatingBar). Документ — сессия DocumentRegistry (вкладка держит shared_ptr;
// указателей MuPDF нет), пул и кеш тайлов — общие у окна. «Несохранено» — в истории есть шаг назад
// (UndoState::canUndo; сохранение историю очищает). Не-PDF (XPS, EPUB, CBZ, картинки, TXT, HTML) — «только
// просмотр»: полоса с кнопкой «Сохранить как PDF» (сигнал saveAsPdfRequested; view-01).
#pragma once

#include <QPointer>
#include <QWidget>

#include <memory>

#include <optional>

#include "core/links/links.h"
#include "services/document_session.h"
#include "services/recent.h"
#include "ui/widgets/tone.h"

class QVBoxLayout;

namespace ot {
class RenderPool;
class TileCache;
}  // namespace ot

namespace ot::ui {

class FindBar;
class MessageBar;
class MessageStack;
class PageView;

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

    // Полосы сообщений над документом: виды и порядок — MessageStack; addMessage — своя полоса (после видов).
    MessageStack& messageStack() const { return *stack_; }
    MessageBar* addMessage(Tone tone, const QString& text);
    QList<MessageBar*> messages() const;

    // Панель «Найти» (Ctrl+F, 2.5) — плавающая в правом верхнем углу вида; у вкладки своя.
    FindBar& findBar() const { return *find_; }

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
    void modifiedChanged(bool modified);
    void saveAsPdfRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void updateModified();
    void placeFloatingBar();
    void placeFindBar();

    std::shared_ptr<DocumentSession> session_;
    RenderPool* pool_ = nullptr;
    PageView* view_ = nullptr;
    MessageStack* stack_ = nullptr;
    FindBar* find_ = nullptr;
    QPointer<QWidget> floating_;
    QString error_;
    int initialPage_ = -1;
    LastView initialView_;
    std::optional<links::LinkAction> initialAction_;
    bool modified_ = false;
};

}  // namespace ot::ui
