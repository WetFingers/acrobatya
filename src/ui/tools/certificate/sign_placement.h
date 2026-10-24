// Рамка подписи на странице (план 4.6, п. 1; эталон 3.8 «Инструмент», 2 — сценарий 4; sign-12, sign-16):
// инструмент вида в слоте ViewToolId::Sign — курсор-перекрестие, рамка протяжкой мыши (в пределах страницы);
// щелчок без протяжки по пустому видимому полю /Sig (они подсвечены пунктиром) — подписать это поле, мимо —
// подсказка. Однократный: после рамки — прежний инструмент; Esc и смена инструмента — отмена (cancelled).
// Ставит его CertificateTool (ToolController::install) при первом подписании на вкладке.
#pragma once

#include <QObject>
#include <QString>

#include <vector>

#include "core/sign/sig_fields.h"
#include "core/util/geometry.h"
#include "ui/view/tools/view_tool.h"

namespace ot::ui {

class SignPlacementTool : public QObject, public RubberBandTool {
    Q_OBJECT
public:
    // Меньше этого по стороне — щелчок, не рамка (пункты).
    static constexpr float kMinSide = 12.0f;

    SignPlacementTool(PageView& view, ToolController& controller);

    ViewToolId id() const noexcept override { return ViewToolId::Sign; }
    QString title() const override;
    bool oneShot() const noexcept override { return true; }
    void activate() override;
    void deactivate() override;
    QCursor cursor(QPointF point, Qt::KeyboardModifiers modifiers) const override;
    void paintOverlay(QPainter& painter) override;
    void documentChanged() override { fields_.clear(); }

    // Поля подписи документа (пустые подсвечиваются и ловят щелчок).
    void setFields(std::vector<sign::SignatureField> fields);
    const std::vector<sign::SignatureField>& fields() const { return fields_; }

    // Как будто рамку нарисовали (тесты, --script): rect — пункты страницы MuPDF; field — пустое поле.
    void place(int page, const RectF& rect, const QString& field = {});

signals:
    void placed(int page, ot::RectF rect, const QString& field);
    void missed();  // щелчок мимо пустого поля
    void cancelled();

protected:
    void finish(const QRectF& rect, QPointF point, Qt::KeyboardModifiers modifiers) override;

private:
    const sign::SignatureField* fieldAt(QPointF viewportPoint) const;

    std::vector<sign::SignatureField> fields_;
    bool done_ = false;
};

}  // namespace ot::ui
