// Набор и правка текста поля прямо на странице (план 5.2, п. 1; эталон 3.4 «Инструмент»: Текст (J), Выноска;
// comment-04, comment-05, comment-09): QTextEdit поверх вида в рамке поля — шрифт, цвет и выравнивание поля,
// кегль — по масштабу вида, перенос по ширине поля, высота растёт по тексту; при прокрутке и масштабе —
// следом. Фиксация — Esc, потеря фокуса (щелчок мимо; панель инструмента и её меню фокус не забирают — формат
// меняется на ходу), смена документа: новое поле с непустым текстом создаётся одним шагом «Add text box»
// (выноска — с /CL), у своего поля — текст, рамка и формат одним шагом «Edit text box» (пустой текст — поле
// удаляется, как у Acrobat). Над своим полем фон редактора — заливка поля или бумага: вид поля на странице
// под ним не двоится.
#pragma once

#include <QColor>
#include <QPointer>
#include <QString>
#include <QTextEdit>

#include <optional>

#include "core/annot/annots.h"

namespace ot::ui {

class PageView;

class InlineTextEditor : public QTextEdit {
    Q_OBJECT
public:
    // Новое поле на странице page: props — FreeText со стилем, рамкой поля rect (пункты страницы) и выноской.
    static InlineTextEditor* startNew(PageView& view, int page, annot::AnnotProps props);
    // Правка своего поля ref со свойствами props.
    static InlineTextEditor* startEdit(PageView& view, const annot::AnnotRef& ref, annot::AnnotProps props);
    // Открытый редактор вида; нет — nullptr.
    static InlineTextEditor* active(const PageView& view);

    int page() const noexcept { return page_; }
    const annot::AnnotProps& props() const noexcept { return props_; }
    const std::optional<annot::AnnotRef>& ref() const noexcept { return ref_; }
    // Формат (панель «Формат текста»): сразу в редакторе, в документ — при фиксации.
    void setFormat(const QString& family, double size, const QColor& color, int align);
    // Записать набранное (один раз) и закрыть редактор.
    void commit();

signals:
    // Поле создано или изменено (ref), удалено (пустой текст — ref с page −1) — после записи.
    void committed(const ot::annot::AnnotRef& ref);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    InlineTextEditor(PageView& view, int page, annot::AnnotProps props, std::optional<annot::AnnotRef> ref);
    void reposition();
    void applyFormat();
    void grow();
    double scale() const;
    bool focusStaysNear() const;

    PageView& view_;
    int page_ = -1;
    annot::AnnotProps props_;     // рамка поля растёт с текстом; формат — с панели
    annot::AnnotProps original_;  // своё поле до правки
    std::optional<annot::AnnotRef> ref_;
    bool done_ = false;
};

}  // namespace ot::ui
