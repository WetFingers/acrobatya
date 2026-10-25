// Окно записки на странице (план 5.1; эталон 3.4 «Попапы и ответы»; comment-01): своё окно поверх PageView —
// MuPDF окна /Popup не рисует. PopupWidget — заголовок цветом комментария (автор, дата /M, «Закрыть»), под
// ним текст /Contents для набора (шрифт и кегль — Установки → Комментирование); набор пишется в документ
// через 0,4 с паузы шагом истории «Edit note text» (шаги подряд за 1 с сливаются — один шаг на фразу). Окно
// стоит у левого верхнего угла рамки /Popup (нет — справа от значка), размер — токены notePopupWidth/Height,
// не зависит от масштаба; при прокрутке и масштабе — следом. Документ только для чтения — текст не правится.
//
// PopupLayer — окна записок одного вида (ребёнок PageView, слой z 50): open/close/closeAll, линии-связки от
// значка к окну (Установки: «Линии-связки»), подсказка «автор — текст» при наведении на комментарий
// (Установки: «Показывать окно при наведении»). Правка страницы перечитывает свойства открытых окон её
// страницы; исчезнувшие (undo, удаление) закрываются. 5.4: под текстом — ответы деревом и статус, внизу —
// поле «Ответить…» (окно вырастает по высоте, не больше двух высот токена).
#pragma once

#include <QFrame>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include <vector>

#include "core/annot/annots.h"
#include "ui/view/overlay.h"

class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QToolButton;

namespace ot {
class DocumentSession;
}

namespace ot::ui {

class PageView;

class PopupWidget : public QFrame {
    Q_OBJECT
public:
    static constexpr int kCommitDelayMs = 400;

    PopupWidget(PageView& view, const annot::AnnotRef& ref);
    ~PopupWidget() override;

    const annot::AnnotRef& ref() const noexcept { return ref_; }
    void setProps(const annot::AnnotProps& props);
    const annot::AnnotProps& props() const noexcept { return props_; }
    QPlainTextEdit* editor() const noexcept { return editor_; }
    QLabel* authorLabel() const noexcept { return author_; }
    QToolButton* closeButton() const noexcept { return close_; }
    // 5.4: ветка ответов и статус (HTML под текстом; пусто — скрыта) и поле «Ответить…» (Enter — ответ автора
    // Identity шагом «Add reply»).
    QLabel* repliesLabel() const noexcept { return replies_; }
    QLineEdit* replyField() const noexcept { return reply_; }
    // Перечитать ветку (дерево документа задачей).
    void loadThread();
    // Записать набранный текст сейчас (без паузы).
    void flush();
    bool pending() const { return timer_.isActive(); }
    // Место на экране: по рамке /Popup или справа от значка.
    void reposition();
    // Рамка значка комментария на экране (для линии-связки); нет — пустая.
    QRectF iconOnViewport() const;

signals:
    void closeRequested(const ot::annot::AnnotRef& ref);

private:
    void applyStyle();

    PageView& view_;
    annot::AnnotRef ref_;
    annot::AnnotProps props_;
    QWidget* header_ = nullptr;
    QLabel* author_ = nullptr;
    QLabel* date_ = nullptr;
    QToolButton* close_ = nullptr;
    QPlainTextEdit* editor_ = nullptr;
    QLabel* replies_ = nullptr;
    QLineEdit* reply_ = nullptr;
    QTimer timer_;
    quint64 threadRequest_ = 0;
    void sendReply();
    void fitHeight();
    QString committed_;
    bool loading_ = false;
};

class PopupLayer : public QObject, public Overlay {
    Q_OBJECT
public:
    // Слой вида (создаётся при первом обращении, живёт, пока жив вид).
    static PopupLayer& of(PageView& view);
    explicit PopupLayer(PageView& view);

    // Открыть окно записки ref (уже открыто — поднять); focus — курсор в текст.
    void open(const annot::AnnotRef& ref, bool focus);
    void close(const annot::AnnotRef& ref);
    void closeAll();
    PopupWidget* popup(const annot::AnnotRef& ref) const;
    std::vector<PopupWidget*> popups() const;
    int count() const { return static_cast<int>(popups().size()); }

    void paintViewport(QPainter& painter, const PageView& view) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

signals:
    void opened(const ot::annot::AnnotRef& ref);

private:
    void repositionAll();
    void pageChanged(int page);
    void showTip(QPoint global, QPointF viewport);

    PageView& view_;
    std::vector<QPointer<PopupWidget>> open_;
    quint64 tip_ = 0;
};

}  // namespace ot::ui
