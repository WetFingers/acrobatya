#include "ui/tools/fillsign/signature_capture_dialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSlider>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <iterator>

#include "core/annot/defaults.h"
#include "services/comment_settings.h"
#include "services/settings.h"
#include "services/signature_image.h"
#include "ui/theme/theme.h"
#include "ui/theme/tokens.h"
#include "ui/widgets/segmented_bar.h"

namespace ot::ui {

namespace {

constexpr int kMaxImageSide = 1200;  // «Картинка» — не больше, px (подпись, а не скан страницы)

// Картинка на бумаге в рамке box (предпросмотр): вписана с сохранением пропорций, не крупнее себя.
QPixmap onPaper(const QImage& image, QSize box, qreal dpr) {
    QPixmap pm(QSize(qRound(box.width() * dpr), qRound(box.height() * dpr)));
    pm.setDevicePixelRatio(dpr);
    pm.fill(kPaperColor.color());
    if (!image.isNull()) {
        QPainter p(&pm);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        const QSizeF fit = QSizeF(image.size()).scaled(QSizeF(box) * 0.9, Qt::KeepAspectRatio);
        const QSizeF size(std::min(fit.width(), static_cast<double>(image.width())),
                          std::min(fit.height(), static_cast<double>(image.height())));
        const QRectF target(QPointF((box.width() - size.width()) / 2, (box.height() - size.height()) / 2),
                            size);
        p.drawImage(target, image);
    }
    return pm;
}

QPixmap inkIcon(const QColor& color, int side, qreal dpr) {
    QPixmap pm(QSize(qRound(side * dpr), qRound(side * dpr)));
    pm.setDevicePixelRatio(dpr);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(Theme::colors().border.color(), 1));
    p.setBrush(color);
    p.drawEllipse(QRectF(1, 1, side - 2, side - 2));
    return pm;
}

}  // namespace

// ---------------------------------------------------------------- SignaturePad

SignaturePad::SignaturePad(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("signaturePad"));
    setAccessibleName(tr("Поле подписи"));
    setCursor(Qt::CrossCursor);
    setMinimumHeight(Theme::sizes().signaturePadHeight);
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void SignaturePad::clear() {
    strokes_.clear();
    drawing_ = false;
    update();
    emit changed();
}

void SignaturePad::addStroke(std::vector<QPointF> stroke) {
    if (stroke.empty()) {
        return;
    }
    strokes_.push_back(std::move(stroke));
    update();
    emit changed();
}

void SignaturePad::setInkColor(const QColor& color) {
    ink_ = color;
    update();
}

QImage SignaturePad::image(double scale) const {
    return fillsign::strokesToImage(strokes_, QSizeF(size()), ink_, kPenWidth, scale);
}

void SignaturePad::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.fillRect(rect(), kPaperColor.color());
    const auto& colors = Theme::colors();
    // Линия-подсказка на трёх четвертях высоты, как на бланке.
    QPen guide(colors.border.color(), 1, Qt::DashLine);
    p.setPen(guide);
    const int y = height() * 3 / 4;
    const int m = Theme::sizes().grid * 6;
    p.drawLine(m, y, width() - m, y);
    p.setPen(QPen(colors.border.color(), 1));
    p.setBrush(Qt::NoBrush);
    p.drawRect(rect().adjusted(0, 0, -1, -1));
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(ink_, kPenWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    for (const std::vector<QPointF>& stroke : strokes_) {
        if (stroke.size() == 1) {
            p.drawPoint(stroke.front());
            continue;
        }
        QPainterPath path(stroke.front());
        for (std::size_t i = 1; i + 1 < stroke.size(); ++i) {
            path.quadTo(stroke[i], (stroke[i] + stroke[i + 1]) / 2);
        }
        path.lineTo(stroke.back());
        p.drawPath(path);
    }
}

void SignaturePad::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) {
        return;
    }
    drawing_ = true;
    strokes_.push_back({event->position()});
    update();
}

void SignaturePad::mouseMoveEvent(QMouseEvent* event) {
    if (!drawing_ || strokes_.empty()) {
        return;
    }
    const QPointF p(std::clamp(event->position().x(), 0.0, static_cast<double>(width())),
                    std::clamp(event->position().y(), 0.0, static_cast<double>(height())));
    if (strokes_.back().back() != p) {
        strokes_.back().push_back(p);
        update();
    }
}

void SignaturePad::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton || !drawing_) {
        return;
    }
    mouseMoveEvent(event);
    drawing_ = false;
    emit changed();
}

// ---------------------------------------------------------------- SignatureCaptureDialog

SignatureCaptureDialog::SignatureCaptureDialog(fillsign::SignatureKind kind, QWidget* parent)
    : QDialog(parent), kind_(kind), ink_(kSignatureInkColors[0].color()) {
    const bool initials = kind == fillsign::SignatureKind::Initials;
    setObjectName(QStringLiteral("signatureCaptureDialog"));
    setWindowTitle(initials ? tr("Инициалы") : tr("Подпись"));
    const Metrics& m = Theme::sizes();
    resize(m.signatureDialogWidth, m.signatureDialogHeight);
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(m.grid * 4, m.grid * 4, m.grid * 4, m.grid * 4);
    column->setSpacing(m.grid * 3);

    modeBar_ = new SegmentedBar(this);
    modeBar_->setObjectName(QStringLiteral("signatureMode"));
    modeBar_->addSegment(tr("Набрать"), QStringLiteral("type"));
    modeBar_->addSegment(tr("Нарисовать"), QStringLiteral("pen-line"));
    modeBar_->addSegment(tr("Картинка"), QStringLiteral("image"));
    modeBar_->setCurrentIndex(0);
    column->addWidget(modeBar_, 0, Qt::AlignLeft);

    stack_ = new QStackedWidget(this);
    stack_->addWidget(buildType());
    stack_->addWidget(buildDraw());
    stack_->addWidget(buildImage());
    column->addWidget(stack_, 1);

    auto* bottom = new QHBoxLayout();
    bottom->setSpacing(m.grid * 2);
    auto* inkLabel = new QLabel(tr("Цвет:"), this);
    bottom->addWidget(inkLabel);
    auto* group = new QButtonGroup(this);
    group->setExclusive(true);
    static const char* const kInkTitles[] = {QT_TR_NOOP("Чёрный"), QT_TR_NOOP("Синий"),
                                             QT_TR_NOOP("Красный")};
    for (std::size_t i = 0; i < std::size(kSignatureInkColors); ++i) {
        auto* b = new QToolButton(this);
        b->setObjectName(QStringLiteral("signatureInk"));
        b->setCheckable(true);
        b->setAutoRaise(true);
        b->setIcon(inkIcon(kSignatureInkColors[i].color(), m.iconPanel, devicePixelRatioF()));
        b->setIconSize(QSize(m.iconPanel, m.iconPanel));
        b->setMinimumSize(m.hitTarget, m.hitTarget);
        b->setToolTip(tr(kInkTitles[i]));
        b->setAccessibleName(tr(kInkTitles[i]));
        group->addButton(b, static_cast<int>(i));
        bottom->addWidget(b);
        inks_.push_back(b);
    }
    inks_.front()->setChecked(true);
    connect(group, &QButtonGroup::idClicked, this,
            [this](int id) { setInkColor(kSignatureInkColors[static_cast<std::size_t>(id)].color()); });
    bottom->addStretch(1);
    save_ = new QCheckBox(initials ? tr("&Сохранить инициалы") : tr("&Сохранить подпись"), this);
    save_->setObjectName(QStringLiteral("signatureSave"));
    save_->setChecked(true);
    save_->setToolTip(tr("Для следующих документов: хранится только на этом компьютере"));
    bottom->addWidget(save_);
    column->addLayout(bottom);

    auto* buttons = new QDialogButtonBox(this);
    apply_ = buttons->addButton(tr("&Применить"), QDialogButtonBox::AcceptRole);
    apply_->setObjectName(QStringLiteral("signatureApply"));
    apply_->setDefault(true);
    buttons->addButton(QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &SignatureCaptureDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    column->addWidget(buttons);

    connect(modeBar_, &SegmentedBar::currentChanged, this,
            [this](int index) { setMode(static_cast<Mode>(index)); });
    refresh();
}

SignatureCaptureDialog* SignatureCaptureDialog::capture(fillsign::SignatureKind kind, QWidget* parent) {
    auto* dialog = new SignatureCaptureDialog(kind, parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->open();
    return dialog;
}

QWidget* SignatureCaptureDialog::buildType() {
    const Metrics& m = Theme::sizes();
    auto* page = new QWidget(this);
    auto* column = new QVBoxLayout(page);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(m.grid * 2);
    auto* form = new QFormLayout();
    form->setSpacing(m.grid * 2);
    name_ = new QLineEdit(page);
    name_->setObjectName(QStringLiteral("signatureName"));
    const annot::Identity identity = comments::identity(Settings());
    const QString name = QString::fromStdString(identity.name.empty() ? annot::loginName() : identity.name);
    name_->setText(kind_ == fillsign::SignatureKind::Initials ? fillsign::initialsOf(name) : name);
    form->addRow(kind_ == fillsign::SignatureKind::Initials ? tr("&Инициалы:") : tr("&Имя:"), name_);
    font_ = new QComboBox(page);
    font_->setObjectName(QStringLiteral("signatureFont"));
    for (const char* family : fillsign::kTypedFonts) {
        font_->addItem(QString::fromLatin1(family), QString::fromLatin1(family));
    }
    form->addRow(tr("&Шрифт:"), font_);
    column->addLayout(form);
    typePreview_ = new QLabel(page);
    typePreview_->setObjectName(QStringLiteral("signatureTypePreview"));
    typePreview_->setMinimumHeight(m.signaturePadHeight * 2 / 3);
    typePreview_->setAlignment(Qt::AlignCenter);
    column->addWidget(typePreview_, 1);
    connect(name_, &QLineEdit::textChanged, this, [this] { refresh(); });
    connect(font_, &QComboBox::currentIndexChanged, this, [this] { refresh(); });
    return page;
}

QWidget* SignatureCaptureDialog::buildDraw() {
    const Metrics& m = Theme::sizes();
    auto* page = new QWidget(this);
    auto* column = new QVBoxLayout(page);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(m.grid * 2);
    auto* hint = new QLabel(tr("Распишитесь мышью или пером в поле ниже."), page);
    hint->setWordWrap(true);
    column->addWidget(hint);
    pad_ = new SignaturePad(page);
    pad_->setInkColor(ink_);
    column->addWidget(pad_, 1);
    clear_ = new QPushButton(tr("&Очистить"), page);
    clear_->setObjectName(QStringLiteral("signatureClear"));
    column->addWidget(clear_, 0, Qt::AlignLeft);
    connect(clear_, &QPushButton::clicked, pad_, &SignaturePad::clear);
    connect(pad_, &SignaturePad::changed, this, [this] { refresh(); });
    return page;
}

QWidget* SignatureCaptureDialog::buildImage() {
    const Metrics& m = Theme::sizes();
    auto* page = new QWidget(this);
    auto* column = new QVBoxLayout(page);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(m.grid * 2);
    browse_ = new QPushButton(tr("&Выбрать картинку…"), page);
    browse_->setObjectName(QStringLiteral("signatureBrowse"));
    column->addWidget(browse_, 0, Qt::AlignLeft);
    preview_ = new QLabel(page);
    preview_->setObjectName(QStringLiteral("signatureImagePreview"));
    preview_->setMinimumHeight(m.signaturePadHeight * 2 / 3);
    preview_->setAlignment(Qt::AlignCenter);
    column->addWidget(preview_, 1);
    auto* form = new QFormLayout();
    form->setSpacing(m.grid * 2);
    threshold_ = new QSlider(Qt::Horizontal, page);
    threshold_->setObjectName(QStringLiteral("signatureThreshold"));
    threshold_->setRange(80, 250);
    threshold_->setValue(fillsign::kDefaultThreshold);
    threshold_->setAccessibleName(tr("Порог фона"));
    threshold_->setToolTip(tr("Светлее порога — фон, он станет прозрачным"));
    form->addRow(tr("&Убрать фон светлее:"), threshold_);
    column->addLayout(form);
    keepColor_ = new QCheckBox(tr("Сохранить &исходный цвет"), page);
    keepColor_->setObjectName(QStringLiteral("signatureKeepColor"));
    column->addWidget(keepColor_);
    connect(browse_, &QPushButton::clicked, this, &SignatureCaptureDialog::browse);
    connect(threshold_, &QSlider::valueChanged, this, [this] { refresh(); });
    connect(keepColor_, &QCheckBox::toggled, this, [this] { refresh(); });
    return page;
}

void SignatureCaptureDialog::setMode(Mode mode) {
    mode_ = mode;
    if (modeBar_->currentIndex() != static_cast<int>(mode)) {
        modeBar_->setCurrentIndex(static_cast<int>(mode));
    }
    stack_->setCurrentIndex(static_cast<int>(mode));
    refresh();
}

QToolButton* SignatureCaptureDialog::inkButton(int index) const {
    return index >= 0 && index < static_cast<int>(inks_.size()) ? inks_[static_cast<std::size_t>(index)]
                                                                : nullptr;
}

QColor SignatureCaptureDialog::inkColor() const { return ink_; }

void SignatureCaptureDialog::setInkColor(const QColor& color) {
    ink_ = color.isValid() ? color : kSignatureInkColors[0].color();
    for (std::size_t i = 0; i < inks_.size() && i < std::size(kSignatureInkColors); ++i) {
        inks_[i]->setChecked(kSignatureInkColors[i].color() == ink_);
    }
    pad_->setInkColor(ink_);
    refresh();
}

void SignatureCaptureDialog::browse() {
    const QString path = chooser_ ? chooser_(this)
                                  : QFileDialog::getOpenFileName(this, tr("Картинка подписи"), QString(),
                                                                 tr("Изображения (*.png *.jpg *.jpeg)"));
    if (!path.isEmpty()) {
        loadImageFile(path);
    }
}

bool SignatureCaptureDialog::loadImageFile(const QString& path) {
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (image.isNull()) {
        preview_->setText(tr("Не удалось прочитать картинку. Подойдут PNG и JPEG."));
        source_ = {};
        refresh();
        return false;
    }
    if (std::max(image.width(), image.height()) > kMaxImageSide) {
        image = image.scaled(kMaxImageSide, kMaxImageSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    source_ = std::move(image);
    if (mode_ != Mode::Image) {
        setMode(Mode::Image);
    } else {
        refresh();
    }
    return true;
}

QImage SignatureCaptureDialog::result() const {
    switch (mode_) {
        case Mode::Type:
            return fillsign::typedImage(name_->text(), font_->currentData().toString(), ink_);
        case Mode::Draw:
            return pad_->image();
        case Mode::Image:
            if (source_.isNull()) {
                return {};
            }
            return fillsign::cropToInk(
                fillsign::removeBackground(source_, threshold_->value(), keepColor_->isChecked(), ink_));
    }
    return {};
}

void SignatureCaptureDialog::refresh() {
    const QImage image = result();
    const qreal dpr = devicePixelRatioF();
    if (mode_ == Mode::Type) {
        typePreview_->setPixmap(onPaper(image, typePreview_->size().expandedTo(QSize(200, 80)), dpr));
    } else if (mode_ == Mode::Image && !source_.isNull()) {
        preview_->setPixmap(onPaper(image, preview_->size().expandedTo(QSize(200, 80)), dpr));
    }
    keepColor_->setEnabled(!source_.isNull());
    threshold_->setEnabled(!source_.isNull());
    apply_->setEnabled(fillsign::hasInk(image));
}

void SignatureCaptureDialog::accept() {
    const QImage image = result();
    if (!fillsign::hasInk(image)) {
        return;
    }
    if (save_->isChecked()) {
        fillsign::SavedSignatures().save(kind_, image);
    }
    emit captured(kind_, image);
    QDialog::accept();
}

}  // namespace ot::ui
