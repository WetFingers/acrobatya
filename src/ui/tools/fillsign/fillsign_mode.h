// Режим «Заполнить и подписать» вида (план 5.3, п. 2–3; эталон 3.5, 6.2 п. 4; forms-07, sign-01, comment-09):
// инструмент вида в слоте ViewToolId::FillSign (ставит FillSignTool), действие — FillSignAction:
//   Текст — щелчок: умное размещение (core/annot/fillsign.h placeText по геометрии бланка страницы): на
//     подчёркивании — базовая линия на линии, в клетке — от её края, кегль под высоту клетки; набор — на
//     странице (InlineTextEditor), поле FreeText без рамки создаётся при фиксации; гребёнка — по символу в
//     клетку (CombEditor, одним шагом); щелчок по своему полю — его правка (text_placer.cpp);
//   ✓ ✕ ● — ▭ — отметка-глиф во флажок под точкой или центром в точке (marks.cpp);
//   Подпись, Инициалы — сохранённая или только что созданная картинка штампом /Stamp: щелчок — в рост
//     клетки над линией или высотой по умолчанию, протягивание — в рамке (signature_placer.cpp);
// поставленное перемещается, масштабируется и удаляется «Выбором объектов» прямо в режиме: нажатие на
// аннотацию или ручку выбранной уходит AnnotSelectTool, Del — удалить выбранное. Пока курсор над бланком —
// пунктир найденной клетки. Правка запрещена (защищённый просмотр, только чтение, право /P) — setLocked:
// курсор «нельзя», действия нет. Каждое действие — шаг истории (services/annot_ops.h, fillsign_ops.h).
#pragma once

#include <QColor>
#include <QFuture>
#include <QImage>
#include <QObject>
#include <QPointF>
#include <QRectF>
#include <QString>

#include <map>
#include <optional>

#include "core/annot/annots.h"
#include "core/annot/fillsign.h"
#include "services/saved_signatures.h"
#include "ui/view/tools/view_tool.h"

namespace ot {
class DocumentSession;
}

namespace ot::ui {

enum class FillSignAction { Text, Check, Cross, Dot, Line, Box, Signature, Initials };

class FillSignMode : public QObject, public ViewTool {
    Q_OBJECT
public:
    static constexpr double kDragThresholdPx = 4;  // меньше — щелчок
    static constexpr float kSignatureHeight = 36;  // подпись щелчком вне линии, пт
    static constexpr float kInitialsHeight = 24;   // инициалы
    static constexpr float kMaxSignatureWidth = 220;

    FillSignMode(PageView& view, ToolController& controller);
    ~FillSignMode() override;

    ViewToolId id() const noexcept override { return ViewToolId::FillSign; }
    QString title() const override;
    void activate() override;
    void deactivate() override;
    bool mousePress(QMouseEvent* event) override;
    bool mouseMove(QMouseEvent* event) override;
    bool mouseRelease(QMouseEvent* event) override;
    bool mouseDoubleClick(QMouseEvent* event) override;
    bool keyPress(QKeyEvent* event) override;
    bool wantsShortcut(const QKeyEvent* event) const override;
    void paintOverlay(QPainter& painter) override;
    QCursor cursor(QPointF point, Qt::KeyboardModifiers modifiers) const override;
    bool cancel() override;
    bool escape(bool cancelled) override;
    void documentChanged() override;
    void pageChanged(int page) override;

    FillSignAction action() const noexcept { return action_; }
    void setAction(FillSignAction action);
    static QString actionTitle(FillSignAction action);
    static bool isMark(FillSignAction action) noexcept;
    static fillsign::Mark markOf(FillSignAction action) noexcept;

    // Правка запрещена: причина по-русски; пусто — можно.
    void setLocked(const QString& reason);
    const QString& lockReason() const noexcept { return lock_; }
    // Цвет текста, отметок и (при постановке) подписи; кегль текста без привязки и отметок вне флажка.
    void setInkColor(const QColor& color);
    QColor inkColor() const { return ink_; }
    void setFontSize(float size) noexcept { fontSize_ = size; }
    float fontSize() const noexcept { return fontSize_; }
    // Картинка подписи или инициалов для постановки (из меню «Подпись ▾»); пусто — сохранённая.
    void setSignatureImage(fillsign::SignatureKind kind, const QImage& image);
    QImage signatureImage(fillsign::SignatureKind kind) const;

    // Геометрия бланка страницы (кеш; нет — запрос задачей, ответ — geometryReady).
    const fillsign::PageGeometry* geometry(int page);
    // Найденная клетка под курсором (пунктир) — страница и размещение.
    int hoverPage() const noexcept { return hoverPage_; }
    const std::optional<fillsign::Placement>& hoverPlacement() const noexcept { return hover_; }

    // Действие в точке page/point (пункты страницы) — как щелчок мышью (сценарии и тесты).
    void placeAt(int page, QPointF point);
    // Подпись или инициалы в рамке rect (пункты страницы).
    void placeSignature(int page, const QRectF& rect, fillsign::SignatureKind kind);

signals:
    void created(const ot::annot::AnnotRef& ref);
    void failed(const QString& message);
    void actionChanged(ot::ui::FillSignAction action);
    void geometryReady(int page);

private:
    DocumentSession* session() const;
    void reportError(const QString& what);
    bool forwardPress(QMouseEvent* event, int page, QPointF point);
    // Щелчок мимо известных аннотаций: попадание задачей документа (список страницы мог не прийти) — своё
    // поле «Текстом» — правка, другая аннотация — выбор, пусто — действие.
    void clickAt(int page, QPointF point);
    void updateHover(QPointF viewportPoint);
    void withGeometry(int page, std::function<void(const fillsign::PageGeometry&)> then);
    void select(const annot::AnnotRef& ref);
    void finishCreate(const QFuture<annot::AnnotRef>& future, bool selectIt);
    annot::AnnotColor annotColor() const;

    // text_placer.cpp
    void placeText(int page, QPointF point, const fillsign::PageGeometry& geometry);
    void startComb(int page, const fillsign::Placement& placement);
    void editText(const annot::AnnotRef& ref);
    // marks.cpp
    void placeMark(int page, QPointF point, const fillsign::PageGeometry& geometry);
    // signature_placer.cpp
    void placeSignatureAt(int page, QPointF point, const fillsign::PageGeometry& geometry);
    QSizeF signatureSize(fillsign::SignatureKind kind, float height) const;
    void paintSignaturePreview(QPainter& painter);

    FillSignAction action_ = FillSignAction::Text;
    QString lock_;
    QColor ink_ = Qt::black;
    float fontSize_ = fillsign::kDefaultFontSize;
    std::map<int, fillsign::PageGeometry> geometry_;
    std::map<int, QFuture<fillsign::PageGeometry>> pending_;
    quint64 generation_ = 0;  // смена документа — ответы геометрии устарели
    quint64 request_ = 0;     // щелчок: Esc и новый щелчок делают ответ попадания устаревшим
    std::map<fillsign::SignatureKind, QImage> images_;
    bool forwarding_ = false;  // нажатие ушло «Выбору объектов»
    bool pressing_ = false;    // протягивание рамки подписи
    int page_ = -1;
    QPointF start_;  // пункты страницы page_
    QPointF end_;
    QPointF pressViewport_;
    std::optional<annot::AnnotInfo> pressHit_;  // аннотация под нажатием (правка текста щелчком)
    QPointF hoverViewport_;
    int hoverPage_ = -1;
    std::optional<fillsign::Placement> hover_;
};

}  // namespace ot::ui
