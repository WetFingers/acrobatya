#include "ui/tools/print/print_preview.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <cmath>
#include <exception>

#include "services/document_session.h"
#include "ui/theme/theme.h"
#include "ui/widgets/tool_button.h"

namespace ot::ui {

namespace {

constexpr int kDebounceMs = 120;  // пауза после правки параметров до нового растра

}  // namespace

// Холст с листом: бумага по центру с тенью и рамкой, растр листа, пунктир области печати.
class PrintPreview::Canvas : public QWidget {
public:
    explicit Canvas(PrintPreview& owner) : QWidget(&owner), owner_(owner) {
        setObjectName(QStringLiteral("printPreviewCanvas"));
        setMinimumSize(Theme::sizes().printPreview, Theme::sizes().printPreview);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    }

protected:
    void paintEvent(QPaintEvent* /*event*/) override {
        const Palette& c = Theme::colors();
        const Metrics& m = Theme::sizes();
        QPainter p(this);
        p.fillRect(rect(), c.canvas.color());
        if (owner_.count_ <= 0) {
            p.setPen(c.textMuted.color());
            p.drawText(rect(), Qt::AlignCenter | Qt::TextWordWrap,
                       PrintPreview::tr("Нет страниц для печати"));
            return;
        }
        const bool landscape = owner_.landscape();
        const print::SizeF sheet = owner_.paper_.oriented(landscape);
        const QRectF area = QRectF(rect()).adjusted(m.grid * 4, m.grid * 4, -m.grid * 4, -m.grid * 4);
        const qreal scale = std::min(area.width() / sheet.width, area.height() / sheet.height);
        const QSizeF size(sheet.width * scale, sheet.height * scale);
        const QRectF paper(area.center() - QPointF(size.width() / 2, size.height() / 2), size);
        // Тень (эталон 6.1: 0 4 16 rgba(0,0,0,.18)) — несколько слоёв.
        for (int i = m.shadowBlur / 4; i > 0; --i) {
            QColor shadow = c.shadow.color();
            shadow.setAlphaF(shadow.alphaF() / (m.shadowBlur / 4.0));
            p.fillRect(paper.translated(0, m.shadowOffset).adjusted(-i, -i, i, i), shadow);
        }
        p.fillRect(paper, Qt::white);
        if (!owner_.image_.isNull()) {
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.drawImage(paper, owner_.image_);
        }
        // Область печати принтера — пунктир (поля, куда принтер не печатает).
        const RectF pr = owner_.paper_.printableFor(landscape);
        if (pr.x0 > 0.5f || pr.y0 > 0.5f || pr.x1 < sheet.width - 0.5f || pr.y1 < sheet.height - 0.5f) {
            QPen dash(c.border.color(), 1, Qt::DashLine);
            p.setPen(dash);
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(paper.x() + pr.x0 * scale, paper.y() + pr.y0 * scale, pr.width() * scale,
                              pr.height() * scale));
        }
        p.setPen(QPen(c.border.color(), m.pageBorder));
        p.setBrush(Qt::NoBrush);
        p.drawRect(paper);
    }

private:
    PrintPreview& owner_;
};

PrintPreview::PrintPreview(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("printPreview"));
    const Metrics& m = Theme::sizes();
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(m.grid * 2);
    canvas_ = new Canvas(*this);
    column->addWidget(canvas_, 1);
    auto* nav = new QHBoxLayout();
    nav->setSpacing(m.grid * 2);
    prev_ = new ToolButton(QStringLiteral("chevron-left"), QString(), this);
    prev_->setObjectName(QStringLiteral("previewPrevious"));
    next_ = new ToolButton(QStringLiteral("chevron-right"), QString(), this);
    next_->setObjectName(QStringLiteral("previewNext"));
    counter_ = new QLabel(this);
    counter_->setObjectName(QStringLiteral("previewCounter"));
    counter_->setAlignment(Qt::AlignCenter);
    nav->addStretch(1);
    nav->addWidget(prev_);
    nav->addWidget(counter_);
    nav->addWidget(next_);
    nav->addStretch(1);
    column->addLayout(nav);
    paperLabel_ = new QLabel(this);
    paperLabel_->setObjectName(QStringLiteral("previewPaper"));
    paperLabel_->setAlignment(Qt::AlignCenter);
    column->addWidget(paperLabel_);
    connect(prev_, &QToolButton::clicked, this, &PrintPreview::previous);
    connect(next_, &QToolButton::clicked, this, &PrintPreview::next);

    timer_ = new QTimer(this);
    timer_->setSingleShot(true);
    timer_->setInterval(kDebounceMs);
    connect(timer_, &QTimer::timeout, this, &PrintPreview::render);
    updateLabels();
}

// Растр уходит в пул потоков с копиями параметров и сессией (shared_ptr), ответ — только живому виду.
PrintPreview::~PrintPreview() { timer_->stop(); }

bool PrintPreview::landscape() const {
    return current_ >= 0 && current_ < static_cast<int>(landscape_.size()) &&
           landscape_[static_cast<std::size_t>(current_)];
}

void PrintPreview::setSource(std::shared_ptr<DocumentSession> session, const PrintSettings& settings,
                             const print::Paper& paper, const PrintPlan& plan) {
    session_ = std::move(session);
    settings_ = settings;
    paper_ = paper;
    count_ = plan.status == PrintStatus::Done ? static_cast<int>(plan.sheets.size()) : 0;
    landscape_.clear();
    sides_.clear();
    for (const print::Sheet& s : plan.sheets) {
        landscape_.push_back(s.landscape);
        sides_.push_back(s.side);
    }
    current_ = std::clamp(current_, 0, std::max(0, count_ - 1));
    schedule();
}

void PrintPreview::setCurrentSheet(int sheet) {
    const int clamped = std::clamp(sheet, 0, std::max(0, count_ - 1));
    if (clamped == current_) {
        return;
    }
    current_ = clamped;
    schedule();
}

void PrintPreview::schedule() {
    ++generation_;
    ready_ = false;
    updateLabels();
    timer_->start();
}

void PrintPreview::render() {
    if (session_ == nullptr || count_ <= 0) {
        image_ = {};
        ready_ = true;
        canvas_->update();
        emit ready();
        return;
    }
    const quint64 generation = generation_;
    auto session = session_;
    const PrintSettings settings = settings_;
    const print::Paper paper = paper_;
    const int sheet = current_;
    // Прежний растр может ещё идти: результат проверяется по поколению.
    QFuture<QImage> future = QtConcurrent::run([session, settings, paper, sheet] {
        try {
            PrintJob job(*session, settings);
            return job.preview(paper, sheet, kDpi);
        } catch (const std::exception&) {
            return QImage();  // документ закрыт или страница не читается — пустой лист
        }
    });
    future.then(this, [this, generation](const QImage& image) {
        if (generation != generation_) {
            return;
        }
        image_ = image;
        ready_ = true;
        updateLabels();
        canvas_->update();
        emit ready();
    });
}

void PrintPreview::updateLabels() {
    prev_->setEnabled(current_ > 0);
    next_->setEnabled(current_ + 1 < count_);
    prev_->setToolTip(tr("Предыдущий лист"));
    prev_->setAccessibleName(tr("Предыдущий лист"));
    next_->setToolTip(tr("Следующий лист"));
    next_->setAccessibleName(tr("Следующий лист"));
    const print::SheetSide side = current_ < static_cast<int>(sides_.size())
                                      ? sides_[static_cast<std::size_t>(current_)]
                                      : print::SheetSide::Single;
    if (count_ <= 0) {
        counter_->setText(tr("Нет листов"));
    } else if (side == print::SheetSide::Single) {
        counter_->setText(tr("Лист %1 из %2").arg(current_ + 1).arg(count_));
    } else {
        counter_->setText((side == print::SheetSide::Front ? tr("Сторона %1 из %2 · лицевая")
                                                           : tr("Сторона %1 из %2 · оборотная"))
                              .arg(current_ + 1)
                              .arg(count_));
    }
    const print::SizeF sheet = paper_.oriented(landscape());
    const auto mm = [](float pt) { return static_cast<int>(std::lround(pt / print::kPointsPerMm)); };
    paperLabel_->setText(tr("Бумага: %1 × %2 мм").arg(mm(sheet.width)).arg(mm(sheet.height)));
    canvas_->setAccessibleName(tr("Предпросмотр печати"));
}

void PrintPreview::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        updateLabels();
    }
    QWidget::changeEvent(event);
}

}  // namespace ot::ui
