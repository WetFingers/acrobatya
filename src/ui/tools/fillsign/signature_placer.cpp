// Подпись и инициалы Fill & Sign (план 5.3, п. 3; эталон 3.5: «подпись/инициалы — /Stamp с изображением
// (pdf_set_annot_stamp_image)»; sign-01): картинка — сохранённая (services/saved_signatures.h) или только что
// созданная в «Подпись ▾ → Добавить…»; щелчок над линией или в клетке бланка — высотой в 1,3 клетки, низом
// чуть за линию (росчерк), с точки щелчка; вне полей — высотой kSignatureHeight (инициалы — kInitialsHeight)
// центром в точке; протягивание — вписать в рамку с пропорциями картинки. Шаг «Add signature» / «Add
// initials»; поставленная выбрана, инструмент возвращается к «Тексту». Пока выбрано — рамка за курсором.
#include <QPainter>

#include <algorithm>

#include "services/comment_settings.h"
#include "services/document_session.h"
#include "services/fillsign_ops.h"
#include "services/settings.h"
#include "services/signature_image.h"
#include "ui/theme/theme.h"
#include "ui/tools/fillsign/fillsign_mode.h"
#include "ui/view/page_view.h"

namespace ot::ui {

namespace {

fillsign::SignatureKind signatureKindOf(FillSignAction action) {
    return action == FillSignAction::Initials ? fillsign::SignatureKind::Initials
                                              : fillsign::SignatureKind::Signature;
}

}  // namespace

QSizeF FillSignMode::signatureSize(fillsign::SignatureKind kind, float height) const {
    const QImage image = signatureImage(kind);
    if (image.isNull() || image.height() <= 0) {
        return {};
    }
    double h = height;
    double w = h * image.width() / image.height();
    if (w > kMaxSignatureWidth) {
        h *= kMaxSignatureWidth / w;
        w = kMaxSignatureWidth;
    }
    return {w, h};
}

void FillSignMode::placeSignatureAt(int page, QPointF point, const fillsign::PageGeometry& geometry) {
    const fillsign::SignatureKind kind = signatureKindOf(action_);
    const PointF at{static_cast<float>(point.x()), static_cast<float>(point.y())};
    const fillsign::Placement line = fillsign::placeText(geometry, at, fontSize_);
    const bool initials = kind == fillsign::SignatureKind::Initials;
    QRectF rect;
    if (line.kind == fillsign::FieldKind::Line || line.kind == fillsign::FieldKind::Box) {
        const float height = std::clamp(line.cellHeight * 1.3f, 14.0f, 48.0f) * (initials ? 0.8f : 1.0f);
        QSizeF size = signatureSize(kind, height);
        const double left = line.kind == fillsign::FieldKind::Box ? line.field.x0 + 2 : at.x;
        const double room = line.field.x1 - left;
        if (size.width() > room && room > 20) {
            size *= room / size.width();
        }
        const double bottom = line.field.y1 + size.height() * 0.12;
        rect = QRectF(left, bottom - size.height(), size.width(), size.height());
    } else {
        const QSizeF size = signatureSize(kind, initials ? kInitialsHeight : kSignatureHeight);
        rect = QRectF(point - QPointF(size.width() / 2, size.height() / 2), size);
    }
    placeSignature(page, rect, kind);
}

void FillSignMode::placeSignature(int page, const QRectF& rect, fillsign::SignatureKind kind) {
    DocumentSession* s = session();
    if (s == nullptr || !lock_.isEmpty()) {
        return;
    }
    const QImage image = signatureImage(kind);
    if (image.isNull() || rect.width() < 1 || rect.height() < 1) {
        reportError(kind == fillsign::SignatureKind::Initials
                        ? tr("инициалы не заданы — добавьте их в меню «Инициалы»")
                        : tr("подпись не задана — добавьте её в меню «Подпись»"));
        return;
    }
    // Рамка — с пропорциями картинки (протягивание вписывает), в пределах страницы.
    const QSizeF size = QSizeF(image.size()).scaled(rect.size(), Qt::KeepAspectRatio);
    QRectF target(rect.topLeft(), size);
    const QSizeF pageSize = view_.pageSizePoints(page);
    target.moveLeft(std::clamp(target.left(), 0.0, std::max(0.0, pageSize.width() - target.width())));
    target.moveTop(std::clamp(target.top(), 0.0, std::max(0.0, pageSize.height() - target.height())));
    annot::AnnotProps props;
    props.type = annot::AnnotType::Stamp;
    props.rect = {static_cast<float>(target.left()), static_cast<float>(target.top()),
                  static_cast<float>(target.right()), static_cast<float>(target.bottom())};
    props.author = comments::author(Settings());
    props.subject = kind == fillsign::SignatureKind::Initials ? "Initials" : "Signature";
    const QByteArray png = fillsign::toPng(image);
    finishCreate(fillsign::createSignature(*s, page,
                                           std::string(png.constData(), static_cast<std::size_t>(png.size())),
                                           std::move(props), kind == fillsign::SignatureKind::Initials),
                 true);
    setAction(FillSignAction::Text);
}

// Рамка подписи: за курсором, при протягивании — по рамке протягивания.
void FillSignMode::paintSignaturePreview(QPainter& painter) {
    const fillsign::SignatureKind kind = signatureKindOf(action_);
    QRectF pageRect;
    int page = page_;
    if (pressing_) {
        pageRect = QRectF(start_, end_).normalized();
    } else {
        page = view_.pageAtViewport(hoverViewport_);
        if (page < 0) {
            return;
        }
        const QSizeF size = signatureSize(
            kind, kind == fillsign::SignatureKind::Initials ? kInitialsHeight : kSignatureHeight);
        if (size.isEmpty()) {
            return;
        }
        const QPointF c = view_.viewportToPage(page, hoverViewport_);
        pageRect = QRectF(c - QPointF(size.width() / 2, size.height() / 2), size);
    }
    QPen pen(Theme::colors().accent.color(), Theme::sizes().pageBorder, Qt::DashLine);
    pen.setCosmetic(true);
    painter.save();
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(view_.pageTransform(page).mapRect(pageRect));
    painter.restore();
}

}  // namespace ot::ui
