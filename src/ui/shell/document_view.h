// DocumentView — вкладка документа (эталон 6.1 «Документ», 4.2; view-27): PageView на холсте, сверху — стек
// полос сообщений (MessageBar; все виды полос и приоритеты — 2.4), внизу по центру — плавающая панель
// (QuickBar, setFloatingBar). Документ — сессия DocumentRegistry (вкладка держит shared_ptr; указателей MuPDF
// нет), пул и кеш тайлов — общие у окна. «Несохранено» — в истории есть шаг назад (UndoState::canUndo;
// сохранение историю очищает). Не-PDF (XPS, EPUB, CBZ, картинки, TXT, HTML) — «только просмотр»: полоса с
// кнопкой «Сохранить как PDF» (сигнал saveAsPdfRequested; view-01).
#pragma once

#include <QPointer>
#include <QWidget>

#include <memory>

#include "services/document_session.h"
#include "services/recent.h"
#include "ui/widgets/tone.h"

class QVBoxLayout;

namespace ot {
class RenderPool;
class TileCache;
}  // namespace ot

namespace ot::ui {

class MessageBar;
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

    // Полоса сообщений над документом (сверху вниз — по порядку добавления).
    MessageBar* addMessage(Tone tone, const QString& text);
    QList<MessageBar*> messages() const;

    // Плавающая панель (QuickBar) — поверх вида, по центру у нижнего края.
    void setFloatingBar(QWidget* bar);
    QWidget* floatingBar() const { return floating_; }

    // Страница после первого показа (--page, восстановление сеанса).
    void setInitialPage(int page);
    // Последний вид файла (недавние, services/recent.h): раскладка, масштаб и страница после первого показа.
    void setInitialView(const LastView& view);
    LastView currentView() const;

    // «Сохранить» в свой файл с ожиданием (подтверждение закрытия; полный поток сохранения — 2.5). false —
    // отказ или ошибка (errorString()).
    bool save();
    QString errorString() const { return error_; }

signals:
    void modifiedChanged(bool modified);
    void saveAsPdfRequested();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void updateModified();
    void placeFloatingBar();

    std::shared_ptr<DocumentSession> session_;
    RenderPool* pool_ = nullptr;
    PageView* view_ = nullptr;
    QWidget* messages_ = nullptr;
    QVBoxLayout* messageLayout_ = nullptr;
    QPointer<QWidget> floating_;
    QString error_;
    int initialPage_ = -1;
    LastView initialView_;
    bool modified_ = false;
};

}  // namespace ot::ui
