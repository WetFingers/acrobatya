// Режим «Добавить комментарии» вида (план 5.1, 5.2; эталон 3.4 «Инструмент», 6.2 п. 3; comment-01…07):
// инструмент вида в слоте ViewToolId::Comment (ставит CommentTool), действие — CommentAction:
//   Записка (S, Ctrl+6) — щелчок ставит значок /Text и открывает окно записки (PopupWidget) для набора;
//     щелчок по своей записке — открывает её окно (note_tool.cpp);
//   Выделить (U) / Подчеркнуть / Зачеркнуть / Волнистая — протягивание по тексту, как «Выделение» (квады
//     core/text/selection.h → /QuadPoints), без текста «Выделить» — область рамкой (markup_tool.cpp);
//   Вставить текст — каретка /Caret у ближайшей границы символа и окно для вставляемого текста;
//     Заменить текст — выделенное зачёркивается (/StrikeOut /IT /StrikeOutTextEdit), каретка в конце,
//     обе — группа /IRT /RT /Group одним шагом истории (caret_tool.cpp);
//   Рисовать (D) — штрих мышью: оверлей — кривые Безье по точкам, в /InkList — точки после Дугласа–Пекера
//     (ε = 0,5 пт) (ink_tool.cpp); Ластик — круг r = 6 px режет штрихи Ink под курсором (eraser_tool.cpp);
//   Текстовое поле (J, 5.2) — рамка протягиванием (щелчок — поле шириной kTextBoxWidth), набор прямо на
//     странице (InlineTextEditor); щелчок по своему полю — его правка (freetext_tool.cpp);
//   Текстовая выноска — нажатие — конец стрелки, отпускание — поле (щелчок — поле выше и правее), колено —
//     слева от поля (callout_tool.cpp);
//   Фигуры (5.2) — линия, стрелка, прямоугольник (R), овал — протягиванием; многоугольник, облако, ломаная
//     — щелчками по вершинам, конец — двойной щелчок, Enter или щелчок по первой вершине, Backspace —
//     убрать вершину; Shift — линия под 45°, квадрат, круг (shape_tools.cpp).
// Каждое действие — операция журнала (services/annot_ops.h); стиль — умолчания ядра + свой стиль инструмента
// (services/comment_settings.h). Записка, вставка, замена, текстовое поле, выноска и фигуры — однократные
// (после действия — прежний инструмент, если не «закреплён», comment-42); разметка, карандаш и ластик
// остаются. Правка комментариев запрещена (защищённый просмотр, только чтение, право /P) — setLocked: курсор
// «нельзя», действия нет.
#pragma once

#include <QElapsedTimer>
#include <QFuture>
#include <QObject>
#include <QPointF>
#include <QString>

#include <map>
#include <optional>
#include <vector>

#include "core/annot/annots.h"
#include "core/text/selection.h"
#include "ui/view/tools/view_tool.h"

namespace ot {
class DocumentSession;
class TextPage;
}  // namespace ot

namespace ot::ui {

enum class CommentAction {
    Note,
    Highlight,
    Underline,
    StrikeOut,
    Squiggly,
    InsertText,
    ReplaceText,
    Ink,
    Eraser,
    TextBox,
    Callout,
    Line,
    Arrow,
    Rectangle,
    Oval,
    Polygon,
    Cloud,
    PolyLine
};

class CommentMode : public QObject, public ViewTool {
    Q_OBJECT
public:
    static constexpr float kNoteIconSize = 20;  // значок записки, пт (как у Acrobat)
    static constexpr int kTextWaitStepMs = 50;  // ожидание текстового слоя после протягивания
    static constexpr int kTextWaitTries = 60;
    static constexpr double kEraserRadiusPx = 6;   // круг ластика на экране
    static constexpr float kTextBoxWidth = 180;    // поле по щелчку, пт
    static constexpr float kCalloutOffset = 60;    // поле выноски по щелчку — выше и правее конца стрелки, пт
    static constexpr float kCalloutKnee = 24;      // колено выноски — левее поля, пт
    static constexpr double kDragThresholdPx = 4;  // меньше — щелчок

    CommentMode(PageView& view, ToolController& controller);
    ~CommentMode() override;

    ViewToolId id() const noexcept override { return ViewToolId::Comment; }
    QString title() const override;
    bool oneShot() const noexcept override;
    void activate() override;
    void deactivate() override;
    bool mousePress(QMouseEvent* event) override;
    bool mouseMove(QMouseEvent* event) override;
    bool mouseRelease(QMouseEvent* event) override;
    bool mouseDoubleClick(QMouseEvent* event) override;
    bool keyPress(QKeyEvent* event) override;
    void paintOverlay(QPainter& painter) override;
    QCursor cursor(QPointF point, Qt::KeyboardModifiers modifiers) const override;
    bool cancel() override;
    void documentChanged() override;
    void pageChanged(int page) override;

    CommentAction action() const noexcept { return action_; }
    void setAction(CommentAction action);
    // Название действия по-русски и ключ стиля (commenting/toolStyles) — "note", "highlight"…
    static QString actionTitle(CommentAction action);
    static QString actionKey(CommentAction action);
    static annot::AnnotType typeOf(CommentAction action);
    // Разметка текста: Highlight, Underline, StrikeOut, Squiggly.
    static bool isMarkup(CommentAction action) noexcept;
    // Фигуры: протягиванием (линия, стрелка, прямоугольник, овал) и по вершинам (многоугольник, облако,
    // ломаная).
    static bool isDragShape(CommentAction action) noexcept;
    static bool isPolyShape(CommentAction action) noexcept;
    // Вершины многоугольника, облака или ломаной, поставленные до конца (пункты страницы polyPage()).
    const std::vector<QPointF>& polyVertices() const noexcept { return poly_; }
    int polyPage() const noexcept { return polyPage_; }

    // Правка комментариев запрещена: причина по-русски; пусто — можно.
    void setLocked(const QString& reason);
    const QString& lockReason() const noexcept { return lock_; }

    // Свойства нового комментария действия (умолчания + стиль + автор из Установок).
    annot::AnnotProps newProps(CommentAction action) const;
    // Разметка выделенного текста (быстрые действия, comment-10): по аннотации на страницу выделения; note —
    // открыть окно записки у первой. Ответ — refs созданных.
    void markupSelection(CommentAction action, const std::map<int, TextSelection>& selection, bool note);

    // Правка своего текстового поля на странице (InlineTextEditor; щелчок «Текстовым полем», двойной щелчок
    // «Выбором объектов»).
    void editTextBox(const annot::AnnotRef& ref);

    // Идёт протягивание (разметка, замена).
    bool dragging() const noexcept { return pressing_; }
    // Предпросмотр разметки: квады страницы dragPage() (пункты).
    const std::vector<QuadF>& previewQuads() const noexcept { return preview_.quads; }
    int dragPage() const noexcept { return page_; }

signals:
    // Комментарий создан (первый из группы).
    void created(const ot::annot::AnnotRef& ref);
    void failed(const QString& message);
    void actionChanged(ot::ui::CommentAction action);

private:
    struct Locator {
        int page = -1;
        QPointF point;  // пункты страницы
    };
    Locator locate(QPointF viewportPoint) const;
    const TextPage* textPage(int page);
    DocumentSession* session() const;
    void done(const QFuture<annot::AnnotRef>& future, bool openNote);
    void finishDrag(quint64 request);
    void reportError(const QString& what);

    // note_tool.cpp
    void notePress(const Locator& at);
    void noteHit(const std::optional<annot::AnnotInfo>& hit, Locator at, quint64 request);
    // markup_tool.cpp
    void markupMove();
    void markupRelease();
    TextSelection selectionNow();
    // caret_tool.cpp
    void insertCaret(const Locator& at);
    void replaceRelease();
    RectF caretRectAt(const TextPage& text, int boundary) const;
    // ink_tool.cpp
    void inkRelease();
    void paintInk(QPainter& painter);
    // eraser_tool.cpp
    void eraserRelease();
    void paintEraser(QPainter& painter);
    // freetext_tool.cpp
    void startTextEditor(int page, annot::AnnotProps props);
    void textBoxHit(const std::optional<annot::AnnotInfo>& hit, Locator at, quint64 request);
    void textBoxRelease();
    // callout_tool.cpp
    void calloutRelease();
    // shape_tools.cpp
    QPointF shapeEnd() const;
    void shapeRelease();
    void polyPress(const Locator& at, bool shift);
    void polyFinish();
    void paintShape(QPainter& painter);
    void paintTextPreview(QPainter& painter);
    bool clicked() const;

    CommentAction action_ = CommentAction::Note;
    QString lock_;
    bool pressing_ = false;
    int page_ = -1;
    QPointF start_;  // пункты страницы page_
    QPointF end_;
    QPointF pressViewport_;
    TextSelection preview_;
    bool area_ = false;  // «Выделить» без текста — область
    quint64 request_ = 0;
    int attempts_ = 0;
    std::vector<QPointF> stroke_;  // карандаш и ластик: путь мыши, пункты страницы page_
    QPointF hover_;                // курсор (вьюпорт) — круг ластика
    bool shift_ = false;           // Shift при протягивании фигуры
    std::vector<QPointF> poly_;    // вершины фигуры по щелчкам (пункты страницы polyPage_)
    int polyPage_ = -1;
};

}  // namespace ot::ui
