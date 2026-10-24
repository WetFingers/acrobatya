// Слой режима «Редактировать» (план 3.4, п. 1; эталон 6.4 — цвета только из темы): рамки блоков видимых
// страниц — пунктир textMuted, у «только чтение» — border и замок textMuted у правого верхнего угла снаружи,
// активный блок — сплошная рамка accent; выделение — selection; каретка — accent 1 px (на бумаге kPaperColor
// видна в любой теме). Локальная копия правленого абзаца: маска цветом бумаги по старой и новой рамкам, глифы
// — шрифтом Qt того же семейства по раскладке редактора (перо, базовая линия, Ts, Tz, наклон), пока растр
// документа старый.
#include <QFontDatabase>
#include <QPainter>
#include <QRegularExpression>

#include <cmath>
#include <cstring>

#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/theme/tokens.h"
#include "ui/tools/edit/edit_mode.h"
#include "ui/view/page_view.h"
#include "ui/view/tile_painter.h"

namespace ot::ui {

namespace {

constexpr qreal kFontPx = 100.0;  // глиф рисуется шрифтом 100 px и масштабом до кегля

QRectF toQRect(const RectF& r) { return QRectF(QPointF(r.x0, r.y0), QPointF(r.x1, r.y1)); }

// Семейство Qt по /BaseFont без префикса подмножества: «Arial-BoldMT» → «Arial», «TimesNewRomanPSMT» →
// «Times New Roman».
QString qtFamily(const edit::CharStyle& style) {
    if (!style.family.empty()) {
        return QString::fromStdString(style.family);
    }
    QString name = QString::fromStdString(style.fontName);
    const qsizetype cut = name.indexOf(QRegularExpression(QStringLiteral("[-,]")));
    if (cut > 0) {
        name.truncate(cut);
    }
    for (const char* tail : {"PSMT", "MT", "PS"}) {
        if (name.endsWith(QLatin1String(tail)) &&
            name.size() > static_cast<qsizetype>(std::strlen(tail)) + 2) {
            name.chop(static_cast<qsizetype>(std::strlen(tail)));
            break;
        }
    }
    static const QStringList families = QFontDatabase::families();
    if (families.contains(name, Qt::CaseInsensitive)) {
        return name;
    }
    QString spaced;
    for (qsizetype i = 0; i < name.size(); ++i) {
        if (i > 0 && name[i].isUpper() && name[i - 1].isLower()) {
            spaced += QLatin1Char(' ');
        }
        spaced += name[i];
    }
    return spaced;
}

QFont qtFont(const edit::CharStyle& style) {
    QFont f(qtFamily(style));
    f.setPixelSize(static_cast<int>(kFontPx));
    f.setBold(style.bold);
    f.setItalic(style.italic);
    f.setUnderline(style.underline);
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

QColor qtColor(std::uint32_t argb) { return QColor::fromRgba(argb); }

QTransform qtMatrix(const Matrix& m) { return QTransform(m.a, m.b, m.c, m.d, m.e, m.f); }

}  // namespace

void EditMode::paintOverlay(QPainter& painter) {
    // Растр после записи готов — копия больше не нужна (первый кадр после ответа считается без проверки:
    // готовность тайлов в нём — ещё старой правки).
    if (renderWait_ && ++renderWaitPaints_ > 1 && view_.tilePainter().visibleComplete()) {
        renderWait_ = false;
        afterglow_.clear();
        renderTimer_.stop();
    }
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (const Written& w : afterglow_) {
        paintLocalCopy(painter, w.editor, w.page, w.original);
    }
    for (const Active& d : detached_) {
        paintLocalCopy(painter, d.editor, d.page, d.original);
    }
    if (showsLocalCopy()) {
        paintLocalCopy(painter, active_->editor, active_->page, active_->original);
    }
    paintFrames(painter);
    paintCaret(painter);
    painter.restore();
}

void EditMode::paintFrames(QPainter& painter) const {
    const Palette& colors = Theme::colors();
    const Metrics& m = Theme::sizes();
    const int icon = m.iconRow;
    for (const Frame& f : frames()) {
        const QRectF r = f.rect.adjusted(-2, -2, 2, 2);
        if (f.active) {
            painter.setPen(QPen(colors.accent.color(), 1));
        } else {
            QPen pen(f.editable ? colors.textMuted.color() : colors.border.color(), 1, Qt::DashLine);
            pen.setCosmetic(true);
            painter.setPen(pen);
        }
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(r);
        if (!f.editable) {
            const QPixmap lock =
                Icons::get(QStringLiteral("lock"), IconTone::Muted, icon, view_.devicePixelRatioF());
            painter.drawPixmap(QPointF(r.right() + 2, r.top()), lock);
        }
    }
}

void EditMode::paintLocalCopy(QPainter& painter, const edit::ParagraphEditor& editor, int page,
                              const RectF& original) const {
    if (page < 0 || editor.layout().empty() || view_.pageViewportRect(page).isEmpty()) {
        return;
    }
    const QTransform pageToView = view_.pageTransform(page);
    const QTransform layoutToView = qtMatrix(editor.geometry().toPage) * pageToView;
    // Маска: старая рамка и строки новой раскладки — цветом бумаги.
    painter.save();
    painter.setClipRect(view_.pageViewportRect(page));
    painter.setPen(Qt::NoPen);
    painter.setBrush(kPaperColor.color());
    painter.drawRect(pageToView.mapRect(toQRect(original)).adjusted(-1, -1, 1, 1));
    for (const QuadF& q : editor.selectionQuads({0, editor.size()})) {
        painter.drawRect(pageToView.mapRect(toQRect(q.bounds())).adjusted(-1, -1, 1, 1));
    }
    const auto& chars = editor.chars();
    const auto& styles = editor.styles();
    const edit::TextLayout& layout = editor.layout();
    std::vector<QFont> fonts;
    fonts.reserve(styles.size());
    for (const edit::CharStyle& s : styles) {
        fonts.push_back(qtFont(s));
    }
    for (const edit::LayoutLine& line : layout.lines) {
        for (int i = line.begin;
             i < line.end && i < static_cast<int>(chars.size()) && i < static_cast<int>(layout.x.size());
             ++i) {
            const edit::EditChar& c = chars[static_cast<std::size_t>(i)];
            if (c.ch == U' ' || c.ch == U'\n' || c.ch == U'\t' || c.group == 0) {
                continue;
            }
            const edit::CharStyle& s = styles[static_cast<std::size_t>(std::max(0, c.style))];
            if (s.render == 3) {
                continue;  // невидимый текст
            }
            const qreal size = s.size / kFontPx;
            QTransform glyph;
            glyph.translate(layout.x[static_cast<std::size_t>(i)], line.baseline - s.rise);
            glyph.shear(-s.skew, 0);
            glyph.scale(size * s.hscale, size);
            painter.setTransform(glyph * layoutToView);
            painter.setFont(fonts[static_cast<std::size_t>(std::max(0, c.style))]);
            painter.setPen(qtColor(s.color));
            painter.drawText(QPointF(0, 0), QString::fromUcs4(&c.ch, 1));
        }
    }
    painter.restore();
}

void EditMode::paintCaret(QPainter& painter) const {
    if (!active_) {
        return;
    }
    const Palette& colors = Theme::colors();
    const QTransform t = view_.pageTransform(active_->page);
    const edit::TextRange sel = selection();
    if (!sel.empty()) {
        QColor fill = colors.selection.color();
        painter.setPen(Qt::NoPen);
        painter.setBrush(fill);
        for (const QuadF& q : active_->editor.selectionQuads(sel)) {
            QPolygonF poly;
            poly << t.map(QPointF(q.ul.x, q.ul.y)) << t.map(QPointF(q.ur.x, q.ur.y))
                 << t.map(QPointF(q.lr.x, q.lr.y)) << t.map(QPointF(q.ll.x, q.ll.y));
            painter.drawPolygon(poly);
        }
    }
    const edit::Caret c = active_->editor.caret(active_->caret);
    const QPointF top = t.map(QPointF(c.top.x, c.top.y));
    const QPointF bottom = t.map(QPointF(c.bottom.x, c.bottom.y));
    if (!active_->preedit.isEmpty()) {
        // Строка IME — подчёркнутой подписью у каретки (ещё не в абзаце).
        QFont f = uiFont(Theme::sizes());
        f.setPixelSize(std::max(8, static_cast<int>(std::round(std::abs(bottom.y() - top.y()) * 0.8))));
        f.setUnderline(true);
        painter.setFont(f);
        painter.setPen(colors.accent.color());
        painter.drawText(QPointF(top.x() + 2, bottom.y() - (bottom.y() - top.y()) * 0.2), active_->preedit);
    }
    if (caretOn_ && view_.hasFocus()) {
        QPen pen(colors.accent.color(), 1);
        pen.setCosmetic(true);
        painter.setPen(pen);
        painter.drawLine(top, bottom);
    }
}

}  // namespace ot::ui
