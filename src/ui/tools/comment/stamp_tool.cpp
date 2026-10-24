// Штамп (план 5.2, п. 4; эталон 3.4 «Инструмент»: Штамп (K) — из палитры, из буфера (comment-41); comment-11,
// comment-12, comment-13): щелчок — выбранный штамп в его размере центром в точке щелчка (в пределах страницы);
// протягивание — в рамке протягивания по ширине с теми же пропорциями; динамический — со значениями Identity
// и датой/временем постановки (services/stamps_catalog.h stampValues); картинка буфера обмена — один раз
// штампом-картинкой (размер — 96 dpi, не шире kMaxImageStamp), затем снова штамп каталога. Пока инструмент
// выбран — рамка штампа за курсором.
#include <QBuffer>
#include <QFuture>
#include <QPainter>

#include <algorithm>
#include <cmath>

#include "services/annot_ops.h"
#include "services/document_session.h"
#include "services/settings.h"
#include "services/settings_keys.h"
#include "services/stamps_catalog.h"
#include "ui/theme/theme.h"
#include "ui/tools/comment/comment_mode.h"
#include "ui/view/page_view.h"

namespace ot::ui {

namespace {

constexpr double kMaxImageStamp = 300;  // картинка буфера — не шире, пт

}  // namespace

void CommentMode::setStamp(const QString& id) {
    stamp_ = id;
    clipboardStamp_ = {};
    sizedStamp_.clear();
    if (!id.isEmpty()) {
        Settings().set(settings::kCommentStamp, id);
    }
}

QString CommentMode::stamp() const {
    return stamp_.isEmpty() ? Settings().get(settings::kCommentStamp) : stamp_;
}

void CommentMode::setClipboardStamp(const QImage& image) {
    clipboardStamp_ = image;
    sizedStamp_.clear();
}

QSizeF CommentMode::stampSize() {
    if (!clipboardStamp_.isNull()) {
        QSizeF s(clipboardStamp_.width() * 72.0 / 96.0, clipboardStamp_.height() * 72.0 / 96.0);
        if (s.width() > kMaxImageStamp) {
            s *= kMaxImageStamp / s.width();
        }
        return s;
    }
    const QString id = stamp();
    if (sizedStamp_ != id) {
        sizedStamp_ = id;
        stampSize_ = {};
        const stamps::StampCatalog catalog;
        if (const auto entry = catalog.find(id)) {
            const annot::StampSize s = catalog.size(*entry);
            stampSize_ = QSizeF(s.width, s.height);
        }
    }
    return stampSize_;
}

void CommentMode::stampRelease() {
    DocumentSession* s = session();
    if (s == nullptr) {
        return;
    }
    const QSizeF size = stampSize();
    if (size.isEmpty()) {
        reportError(tr("штамп не найден — выберите его в палитре"));
        return;
    }
    const QSizeF page = view_.pageSizePoints(page_);
    QRectF rect;
    if (clicked()) {
        rect = QRectF(start_ - QPointF(size.width() / 2, size.height() / 2), size);
    } else {
        const double w = std::max(8.0, std::fabs(end_.x() - start_.x()));
        const double h = w * size.height() / size.width();
        rect = QRectF(end_.x() < start_.x() ? start_.x() - w : start_.x(),
                      end_.y() < start_.y() ? start_.y() - h : start_.y(), w, h);
    }
    rect.moveLeft(std::clamp(rect.left(), 0.0, std::max(0.0, page.width() - rect.width())));
    rect.moveTop(std::clamp(rect.top(), 0.0, std::max(0.0, page.height() - rect.height())));
    annot::AnnotProps props = newProps(CommentAction::Stamp);
    props.rect = {static_cast<float>(rect.left()), static_cast<float>(rect.top()), static_cast<float>(rect.right()),
                  static_cast<float>(rect.bottom())};
    if (!clipboardStamp_.isNull()) {
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        clipboardStamp_.save(&buffer, "PNG");
        clipboardStamp_ = {};
        sizedStamp_.clear();
        done(annot::createImageStamp(*s, page_, std::string(png.constData(), static_cast<std::size_t>(png.size())),
                                     std::move(props), "Image"),
             false);
        return;
    }
    const stamps::StampCatalog catalog;
    const auto entry = catalog.find(stamp());
    if (!entry) {
        reportError(tr("штамп не найден — выберите его в палитре"));
        return;
    }
    done(annot::createStamp(*s, page_, catalog.source(*entry), stamps::stampValues(Settings()), std::move(props)),
         false);
}

// Рамка штампа: за курсором, при протягивании — по рамке протягивания.
void CommentMode::paintStampPreview(QPainter& painter) {
    const QSizeF size = stampSize();
    if (size.isEmpty()) {
        return;
    }
    QRectF pageRect;
    int page = page_;
    if (pressing_ && !clicked()) {
        const double w = std::max(8.0, std::fabs(end_.x() - start_.x()));
        const double h = w * size.height() / size.width();
        pageRect = QRectF(end_.x() < start_.x() ? start_.x() - w : start_.x(),
                          end_.y() < start_.y() ? start_.y() - h : start_.y(), w, h);
    } else {
        page = view_.pageAtViewport(hover_);
        if (page < 0) {
            return;
        }
        const QPointF c = view_.viewportToPage(page, hover_);
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
