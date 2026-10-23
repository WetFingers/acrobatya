#include "ui/shell/home/brief_pane.h"

#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QGridLayout>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

#include "services/thumbnails.h"
#include "ui/theme/theme.h"
#include "ui/widgets/panel_header.h"

namespace ot::ui {

namespace {

const char* const kFields[] = {"location", "size", "pages", "modified", "opened"};

}  // namespace

BriefPane::BriefPane(ThumbnailCache& thumbs, QWidget* parent) : QWidget(parent), thumbs_(thumbs) {
    setObjectName(QStringLiteral("briefPane"));
    setAttribute(Qt::WA_StyledBackground);
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    header_ = new PanelHeader(QString(), this);
    column->addWidget(header_);

    auto* body = new QWidget(this);
    auto* inner = new QVBoxLayout(body);
    thumb_ = new QLabel(body);
    thumb_->setObjectName(QStringLiteral("briefThumb"));
    thumb_->setAlignment(Qt::AlignCenter);
    inner->addWidget(thumb_);
    name_ = new QLabel(body);
    name_->setObjectName(QStringLiteral("briefName"));
    name_->setWordWrap(true);
    inner->addWidget(name_);

    auto* grid = new QGridLayout();
    for (int i = 0; i < 5; ++i) {
        auto* caption = new QLabel(body);
        caption->setObjectName(QStringLiteral("briefCaption"));
        auto* value = new QLabel(body);
        value->setObjectName(QLatin1String(kFields[i]));
        value->setWordWrap(true);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse);
        grid->addWidget(caption, i, 0, Qt::AlignTop);
        grid->addWidget(value, i, 1);
        captions_ << caption;
        values_ << value;
    }
    grid->setColumnStretch(1, 1);
    inner->addLayout(grid);

    open_ = new QPushButton(body);
    open_->setObjectName(QStringLiteral("briefOpen"));
    open_->setProperty("primary", true);
    connect(open_, &QPushButton::clicked, this, [this] { emit openRequested(path_); });
    print_ = new QPushButton(body);
    print_->setObjectName(QStringLiteral("briefPrint"));
    connect(print_, &QPushButton::clicked, this, [this] { emit printRequested(path_); });
    combine_ = new QPushButton(body);
    combine_->setObjectName(QStringLiteral("briefCombine"));
    connect(combine_, &QPushButton::clicked, this, [this] { emit combineRequested(path_); });
    inner->addWidget(open_);
    inner->addWidget(print_);
    inner->addWidget(combine_);
    inner->addStretch(1);
    column->addWidget(body, 1);

    connect(&thumbs_, &ThumbnailCache::ready, this, [this](const QString& p) {
        if (p == path_) {
            refreshThumb();
        }
    });
    connect(&Theme::instance(), &Theme::themeChanged, this, &BriefPane::applyMetrics);
    retranslate();
    applyMetrics();
}

void BriefPane::setFile(const QString& path, const std::optional<RecentEntry>& entry) {
    path_ = path;
    const QFileInfo info(path);
    const QLocale locale;
    name_->setText(info.fileName());
    // Длинный путь переносится по разделителям (пробел нулевой ширины после «\»).
    QString location = QDir::toNativeSeparators(info.absolutePath());
    values_[0]->setText(location.replace(QLatin1Char(0x5C), QString(QLatin1Char(0x5C)) + QChar(0x200B)));
    values_[0]->setToolTip(QDir::toNativeSeparators(info.absolutePath()));
    values_[1]->setText(info.exists()
                            ? locale.formattedDataSize(info.size(), 1, QLocale::DataSizeTraditionalFormat)
                            : tr("файл не найден"));
    values_[2]->setText(entry && entry->pageCount > 0 ? QString::number(entry->pageCount)
                                                      : QStringLiteral("—"));
    values_[3]->setText(info.exists() ? locale.toString(info.lastModified(), QLocale::ShortFormat)
                                      : QStringLiteral("—"));
    values_[4]->setText(entry && entry->openedAt.toMSecsSinceEpoch() > 0
                            ? locale.toString(entry->openedAt, QLocale::ShortFormat)
                            : QStringLiteral("—"));
    open_->setEnabled(info.exists());
    refreshThumb();
}

void BriefPane::clear() {
    path_.clear();
    name_->clear();
    thumb_->clear();
    for (QLabel* v : values_) {
        v->clear();
    }
}

QString BriefPane::valueText(const QString& field) const {
    for (int i = 0; i < 5; ++i) {
        if (field == QLatin1String(kFields[i])) {
            return values_[i]->text();
        }
    }
    return {};
}

void BriefPane::refreshThumb() {
    const QImage image = path_.isEmpty() ? QImage() : thumbs_.get(path_);
    const Metrics& m = Theme::sizes();
    // Миниатюра целиком: не выше 5/4 ширины (длинные страницы уменьшаются).
    thumb_->setPixmap(image.isNull()
                          ? QPixmap()
                          : QPixmap::fromImage(image.scaled(m.thumbWidth, m.thumbWidth * 5 / 4,
                                                            Qt::KeepAspectRatio, Qt::SmoothTransformation)));
}

void BriefPane::retranslate() {
    header_->setTitle(tr("Краткие сведения"));
    const QString captions[] = {tr("Расположение"), tr("Размер"), tr("Страниц"), tr("Изменён"), tr("Открыт")};
    for (int i = 0; i < 5; ++i) {
        captions_[i]->setText(captions[i]);
    }
    open_->setText(tr("&Открыть"));
    print_->setText(tr("&Печать…"));
    combine_->setText(tr("О&бъединить…"));
    setAccessibleName(tr("Краткие сведения"));
}

void BriefPane::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QWidget::changeEvent(event);
}

void BriefPane::applyMetrics() {
    const Metrics& m = Theme::sizes();
    setFixedWidth(m.rightPanel);
    thumb_->setFixedHeight(m.thumbWidth * 5 / 4);
    name_->setFont(panelTitleFont(m));
    if (auto* inner = qobject_cast<QVBoxLayout*>(thumb_->parentWidget()->layout())) {
        inner->setContentsMargins(m.grid * 4, m.grid * 2, m.grid * 4, m.grid * 4);
        inner->setSpacing(m.grid * 2);
    }
}

}  // namespace ot::ui
