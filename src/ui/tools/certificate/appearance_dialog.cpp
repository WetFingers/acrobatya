#include "ui/tools/certificate/appearance_dialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

#include <algorithm>
#include <exception>

#include "ui/theme/theme.h"
#include "ui/tools/certificate/sign_texts.h"
#include "ui/widgets/property_sheet.h"
#include "ui/widgets/tone.h"

namespace ot::ui {

QPixmap appearancePreview(sign::SignatureAppearance look, const sign::AppearanceData& data, QSizeF sizePt,
                          qreal devicePixelRatio, double zoom) {
    if (sizePt.width() < 1 || sizePt.height() < 1 || zoom <= 0) {
        return {};
    }
    look.labels = SignTexts::appearanceLabels();
    devicePixelRatio = std::max<qreal>(1.0, devicePixelRatio);
    const float dpi = static_cast<float>(96.0 * zoom * devicePixelRatio);
    Bitmap bitmap;
    try {
        bitmap = sign::previewAppearance(look, data, static_cast<float>(sizePt.width()),
                                         static_cast<float>(sizePt.height()), dpi);
    } catch (const std::exception&) {
        return {};
    }
    if (bitmap.empty()) {
        return {};
    }
    const QImage::Format format = bitmap.components == 1 ? QImage::Format_Grayscale8 : QImage::Format_RGB888;
    QImage image(bitmap.pixels.data(), bitmap.width, bitmap.height, bitmap.stride, format);
    QPixmap pixmap = QPixmap::fromImage(image.copy());
    pixmap.setDevicePixelRatio(devicePixelRatio);
    return pixmap;
}

AppearanceDialog::AppearanceDialog(NamedAppearance initial, sign::AppearanceData sample, QWidget* parent)
    : QDialog(parent), sample_(std::move(sample)) {
    setObjectName(QStringLiteral("appearanceDialog"));
    const Metrics& m = Theme::sizes();
    const sign::SignatureAppearance& look = initial.look;
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(m.grid * 6, m.grid * 6, m.grid * 6, m.grid * 4);
    column->setSpacing(m.grid * 3);
    sheet_ = new PropertySheet(this);
    column->addWidget(sheet_);

    name_ = new QLineEdit(initial.name);
    name_->setObjectName(QStringLiteral("appearanceName"));
    sections_ << sheet_->addSection(QString());
    rows_ << sheet_->addRow(QString(), name_);

    // Графика: нет / имя / изображение.
    sections_ << sheet_->addSection(QString());
    auto* graphicRow = new QWidget();
    auto* g = new QHBoxLayout(graphicRow);
    g->setContentsMargins(0, 0, 0, 0);
    graphic_ = new QButtonGroup(this);
    none_ = new QRadioButton();
    none_->setObjectName(QStringLiteral("appearanceNoGraphic"));
    byName_ = new QRadioButton();
    byName_->setObjectName(QStringLiteral("appearanceNameGraphic"));
    image_ = new QRadioButton();
    image_->setObjectName(QStringLiteral("appearanceImageGraphic"));
    graphic_->addButton(none_, static_cast<int>(sign::AppearanceGraphic::None));
    graphic_->addButton(byName_, static_cast<int>(sign::AppearanceGraphic::Name));
    graphic_->addButton(image_, static_cast<int>(sign::AppearanceGraphic::Image));
    g->addWidget(none_);
    g->addWidget(byName_);
    g->addWidget(image_);
    g->addStretch(1);
    sheet_->addWidget(graphicRow);
    auto* imageRow = new QWidget();
    auto* ir = new QHBoxLayout(imageRow);
    ir->setContentsMargins(0, 0, 0, 0);
    imageButton_ = new QPushButton();
    imageButton_->setObjectName(QStringLiteral("appearanceImageButton"));
    imageLabel_ = new QLabel();
    imageLabel_->setObjectName(QStringLiteral("appearanceImagePath"));
    imageLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    ir->addWidget(imageButton_);
    ir->addWidget(imageLabel_, 1);
    sheet_->addWidget(imageRow);
    imageOnly_ = new QCheckBox();
    imageOnly_->setObjectName(QStringLiteral("appearanceImageOnly"));
    sheet_->addWidget(imageOnly_);

    // Текст.
    sections_ << sheet_->addSection(QString());
    auto* textRow = new QWidget();
    auto* tl = new QHBoxLayout(textRow);
    tl->setContentsMargins(0, 0, 0, 0);
    const auto box = [&](const char* name, bool on) {
        auto* b = new QCheckBox();
        b->setObjectName(QLatin1String(name));
        b->setChecked(on);
        tl->addWidget(b);
        connect(b, &QCheckBox::toggled, this, &AppearanceDialog::refresh);
        return b;
    };
    textName_ = box("appearanceTextName", look.text.name);
    textReason_ = box("appearanceTextReason", look.text.reason);
    textLocation_ = box("appearanceTextLocation", look.text.location);
    textDate_ = box("appearanceTextDate", look.text.date);
    textDn_ = box("appearanceTextDn", look.text.dn);
    textLabels_ = box("appearanceTextLabels", look.text.labels);
    tl->addStretch(1);
    sheet_->addWidget(textRow);

    // Логотип и язык.
    sections_ << sheet_->addSection(QString());
    auto* logoRow = new QWidget();
    auto* lr = new QHBoxLayout(logoRow);
    lr->setContentsMargins(0, 0, 0, 0);
    logoButton_ = new QPushButton();
    logoButton_->setObjectName(QStringLiteral("appearanceLogoButton"));
    logoClear_ = new QPushButton();
    logoClear_->setObjectName(QStringLiteral("appearanceLogoClear"));
    logoLabel_ = new QLabel();
    logoLabel_->setObjectName(QStringLiteral("appearanceLogoPath"));
    lr->addWidget(logoButton_);
    lr->addWidget(logoClear_);
    lr->addWidget(logoLabel_, 1);
    rows_ << sheet_->addRow(QString(), logoRow);
    language_ = new QComboBox();
    language_->setObjectName(QStringLiteral("appearanceLanguage"));
    language_->addItem(QString(), QStringLiteral("ru"));
    language_->addItem(QString(), QStringLiteral("en"));
    rows_ << sheet_->addRow(QString(), language_);

    sections_ << sheet_->addSection(QString());
    preview_ = new QLabel();
    preview_->setObjectName(QStringLiteral("appearancePreview"));
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setFrameShape(QFrame::StyledPanel);
    preview_->setMinimumSize(m.dialogWidth / 2, m.cardRow * 2);
    sheet_->addWidget(preview_);

    error_ = new QLabel(this);
    error_->setObjectName(QStringLiteral("appearanceError"));
    error_->setWordWrap(true);
    error_->hide();
    column->addWidget(error_);
    auto* buttons = new QDialogButtonBox(this);
    save_ = buttons->addButton(QString(), QDialogButtonBox::AcceptRole);
    save_->setObjectName(QStringLiteral("appearanceSave"));
    save_->setDefault(true);
    save_->setProperty("primary", true);
    repolish(save_);
    cancel_ = buttons->addButton(QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &AppearanceDialog::submit);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    column->addWidget(buttons);

    if (QAbstractButton* b = graphic_->button(static_cast<int>(look.graphic))) {
        b->setChecked(true);
    }
    imageOnly_->setChecked(look.imageOnly);
    imagePath_ = QString::fromStdWString(look.image.wstring());
    logoPath_ = QString::fromStdWString(look.logo.wstring());
    const int lang = language_->findData(QString::fromStdString(look.language));
    language_->setCurrentIndex(lang >= 0 ? lang : 0);
    connect(graphic_, &QButtonGroup::idClicked, this, [this] { refresh(); });
    connect(imageOnly_, &QCheckBox::toggled, this, &AppearanceDialog::refresh);
    connect(language_, &QComboBox::currentIndexChanged, this, &AppearanceDialog::refresh);
    connect(imageButton_, &QPushButton::clicked, this, [this] {
        const QString path = choose(tr("Изображение подписи"));
        if (!path.isEmpty()) {
            setImage(path);
        }
    });
    connect(logoButton_, &QPushButton::clicked, this, [this] {
        const QString path = choose(tr("Логотип"));
        if (!path.isEmpty()) {
            setLogo(path);
        }
    });
    connect(logoClear_, &QPushButton::clicked, this, [this] { setLogo({}); });
    setMinimumWidth(m.dialogWidth * 3 / 4);
    retranslate();
    refresh();
}

QRadioButton* AppearanceDialog::graphicButton(sign::AppearanceGraphic graphic) const {
    return qobject_cast<QRadioButton*>(graphic_->button(static_cast<int>(graphic)));
}

QCheckBox* AppearanceDialog::textBox(const QString& key) const {
    if (key == QLatin1String("name")) {
        return textName_;
    }
    if (key == QLatin1String("reason")) {
        return textReason_;
    }
    if (key == QLatin1String("location")) {
        return textLocation_;
    }
    if (key == QLatin1String("date")) {
        return textDate_;
    }
    if (key == QLatin1String("dn")) {
        return textDn_;
    }
    return key == QLatin1String("labels") ? textLabels_ : nullptr;
}

NamedAppearance AppearanceDialog::appearance() const {
    NamedAppearance out;
    out.name = name_->text().trimmed();
    sign::SignatureAppearance& a = out.look;
    a.graphic = static_cast<sign::AppearanceGraphic>(std::max(0, graphic_->checkedId()));
    a.image = std::filesystem::path(imagePath_.toStdWString());
    a.imageOnly = imageOnly_->isChecked() && a.graphic == sign::AppearanceGraphic::Image;
    a.text.name = textName_->isChecked();
    a.text.reason = textReason_->isChecked();
    a.text.location = textLocation_->isChecked();
    a.text.date = textDate_->isChecked();
    a.text.dn = textDn_->isChecked();
    a.text.labels = textLabels_->isChecked();
    a.logo = std::filesystem::path(logoPath_.toStdWString());
    a.language = language_->currentData().toString().toStdString();
    return out;
}

QString AppearanceDialog::choose(const QString& title) {
    if (chooser_) {
        return chooser_(this, title);
    }
    return QFileDialog::getOpenFileName(this, title, QString(), tr("Изображения (*.png *.jpg *.jpeg)"));
}

void AppearanceDialog::setImage(const QString& path) {
    imagePath_ = path;
    image_->setChecked(true);
    refresh();
}

void AppearanceDialog::setLogo(const QString& path) {
    logoPath_ = path;
    refresh();
}

void AppearanceDialog::refresh() {
    const bool isImage = image_->isChecked();
    imageButton_->setEnabled(isImage);
    imageOnly_->setEnabled(isImage);
    const bool textOn = !(isImage && imageOnly_->isChecked());
    for (QCheckBox* b : {textName_, textReason_, textLocation_, textDate_, textDn_, textLabels_}) {
        b->setEnabled(textOn);
    }
    imageLabel_->setText(imagePath_.isEmpty() ? tr("файл не выбран") : QFileInfo(imagePath_).fileName());
    logoLabel_->setText(logoPath_.isEmpty() ? tr("без логотипа") : QFileInfo(logoPath_).fileName());
    logoClear_->setEnabled(!logoPath_.isEmpty());
    const QSizeF pt(234, 90);  // рамка подписи — как обычно рисуют: шире, чем выше
    const QPixmap pixmap = appearancePreview(appearance().look, sample_, pt, devicePixelRatioF());
    if (pixmap.isNull()) {
        preview_->setPixmap({});
        preview_->setText(tr("Предпросмотр недоступен: изображение не читается."));
    } else {
        preview_->setText({});
        preview_->setPixmap(pixmap);
    }
}

void AppearanceDialog::submit() {
    const NamedAppearance a = appearance();
    QString problem;
    if (a.name.isEmpty()) {
        problem = tr("Укажите название внешнего вида.");
    } else if (a.look.graphic == sign::AppearanceGraphic::Image && imagePath_.isEmpty()) {
        problem = tr("Выберите файл изображения или другую графику.");
    }
    if (!problem.isEmpty()) {
        error_->setText(problem);
        SignTexts::setNote(error_, Tone::Danger);
        error_->show();
        return;
    }
    accept();
}

void AppearanceDialog::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QDialog::changeEvent(event);
}

void AppearanceDialog::retranslate() {
    setWindowTitle(tr("Внешний вид подписи"));
    const QStringList sections = {tr("Название"), tr("Графика слева"), tr("Текст справа"), tr("Оформление"),
                                  tr("Предпросмотр")};
    for (int i = 0; i < sections_.size(); ++i) {
        sections_[i]->setText(sections.value(i));
    }
    const QStringList rows = {tr("&Название:"), tr("&Логотип:"), tr("&Язык текста:")};
    for (int i = 0; i < rows_.size(); ++i) {
        rows_[i]->setText(rows.value(i));
    }
    name_->setPlaceholderText(tr("Например, Рабочая подпись"));
    none_->setText(tr("&Нет"));
    byName_->setText(tr("&Имя"));
    image_->setText(tr("&Изображение"));
    imageButton_->setText(tr("Выбрать &файл…"));
    imageOnly_->setText(tr("Только изображение, &без текста"));
    textName_->setText(tr("Имя"));
    textReason_->setText(tr("Причина"));
    textLocation_->setText(tr("Место"));
    textDate_->setText(tr("Дата"));
    textDn_->setText(tr("DN"));
    textDn_->setToolTip(tr("Различающееся имя сертификата: имя, организация, страна"));
    textLabels_->setText(tr("Ярлыки"));
    logoButton_->setText(tr("Выбрать…"));
    logoClear_->setText(tr("Убрать"));
    language_->setItemText(0, tr("Русский"));
    language_->setItemText(1, tr("Английский"));
    save_->setText(tr("&Сохранить"));
    cancel_->setText(tr("Отмена"));
    refresh();
}

}  // namespace ot::ui
