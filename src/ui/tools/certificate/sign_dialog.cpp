#include "ui/tools/certificate/sign_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

#include <exception>
#include <memory>

#include "core/sign/x509_info.h"
#include "services/cert_picker.h"
#include "services/identities.h"
#include "services/settings.h"
#include "services/tsa_settings.h"
#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/tools/certificate/appearance_dialog.h"
#include "ui/tools/certificate/new_identity_dialog.h"
#include "ui/tools/certificate/sign_texts.h"
#include "ui/tools/certificate/warnings_dialog.h"
#include "ui/widgets/property_sheet.h"
#include "ui/widgets/tone.h"

namespace ot::ui {

namespace sign_dialog {

// CN из строки DN RFC 2253 («CN=АкроБатя тест, OU=RSA, …»); нет — строка как есть.
QString commonNameOf(const QString& dn) {
    const int at = dn.indexOf(QLatin1String("CN="));
    if (at < 0) {
        return dn;
    }
    const int end = dn.indexOf(QLatin1Char(','), at);
    return dn.mid(at + 3, end < 0 ? -1 : end - at - 3).trimmed();
}

bool expired(const sign::IdentityRecord& r) {
    const QDateTime notAfter = QDateTime::fromString(QString::fromStdString(r.notAfter), Qt::ISODate);
    return notAfter.isValid() && notAfter < QDateTime::currentDateTimeUtc();
}

void setRowVisible(QLabel* label, QWidget* editor, bool visible) {
    if (label != nullptr) {
        label->setVisible(visible);
    }
    if (editor != nullptr) {
        editor->setVisible(visible);
    }
}

}  // namespace sign_dialog

SignDialog::SignDialog(Context context, QWidget* parent)
    : QDialog(parent), context_(std::move(context)), servers_(loadTsaServers(Settings())) {
    setObjectName(QStringLiteral("signDialog"));
    build();
    reloadIdentities();
    reloadAppearances();
    retranslate();
    updateIdentity();
}

void SignDialog::build() {
    const Metrics& m = Theme::sizes();
    const Settings settings;
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(m.grid * 6, m.grid * 6, m.grid * 6, m.grid * 4);
    column->setSpacing(m.grid * 3);
    intro_ = new QLabel(this);
    intro_->setObjectName(QStringLiteral("signIntro"));
    intro_->setWordWrap(true);
    column->addWidget(intro_);

    sheet_ = new PropertySheet();
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("signScroll"));
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(sheet_);
    column->addWidget(scroll, 1);

    // Удостоверение.
    sections_ << sheet_->addSection(QString());
    identity_ = new QComboBox();
    identity_->setObjectName(QStringLiteral("signIdentity"));
    identity_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    identity_->setMinimumContentsLength(28);
    rows_ << sheet_->addRow(QString(), identity_);
    auto* idButtons = new QWidget();
    auto* ib = new QHBoxLayout(idButtons);
    ib->setContentsMargins(0, 0, 0, 0);
    ib->setSpacing(m.grid * 2);
    addFile_ = new QPushButton(Icons::icon(QStringLiteral("file-key")), QString());
    addFile_->setObjectName(QStringLiteral("signAddFile"));
    windows_ = new QPushButton(Icons::icon(QStringLiteral("key-round")), QString());
    windows_->setObjectName(QStringLiteral("signWindows"));
    windows_->setEnabled(CertPicker::available());
    create_ = new QPushButton(Icons::icon(QStringLiteral("plus")), QString());
    create_->setObjectName(QStringLiteral("signCreateIdentity"));
    ib->addWidget(addFile_);
    ib->addWidget(windows_);
    ib->addWidget(create_);
    ib->addStretch(1);
    sheet_->addWidget(idButtons);
    windowsNote_ = new QLabel();
    windowsNote_->setObjectName(QStringLiteral("signWindowsNote"));
    windowsNote_->setWordWrap(true);
    windowsNote_->setVisible(!CertPicker::available());
    sheet_->addWidget(windowsNote_);
    password_ = new QLineEdit();
    password_->setObjectName(QStringLiteral("signPassword"));
    password_->setEchoMode(QLineEdit::Password);
    rows_ << sheet_->addRow(QString(), password_);
    remember_ = new QCheckBox();
    remember_->setObjectName(QStringLiteral("signRemember"));
    sheet_->addWidget(remember_);
    identityNote_ = new QLabel();
    identityNote_->setObjectName(QStringLiteral("signIdentityNote"));
    identityNote_->setWordWrap(true);
    identityNote_->hide();
    sheet_->addWidget(identityNote_);

    // Внешний вид (видимая подпись).
    sections_ << sheet_->addSection(QString());
    auto* lookRow = new QWidget();
    auto* lr = new QHBoxLayout(lookRow);
    lr->setContentsMargins(0, 0, 0, 0);
    appearance_ = new QComboBox();
    appearance_->setObjectName(QStringLiteral("signAppearance"));
    appearance_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    appearance_->setMinimumContentsLength(16);
    newAppearance_ = new QPushButton();
    newAppearance_->setObjectName(QStringLiteral("signNewAppearance"));
    lr->addWidget(appearance_, 1);
    lr->addWidget(newAppearance_);
    rows_ << sheet_->addRow(QString(), lookRow);
    preview_ = new QLabel();
    preview_->setObjectName(QStringLiteral("signPreview"));
    preview_->setAlignment(Qt::AlignCenter);
    preview_->setFrameShape(QFrame::StyledPanel);
    preview_->setMinimumHeight(m.cardRow * 2);
    sheet_->addWidget(preview_);
    if (!context_.visible) {
        sections_.last()->hide();
        sign_dialog::setRowVisible(rows_.last(), lookRow, false);
        preview_->hide();
    }

    // Сведения.
    sections_ << sheet_->addSection(QString());
    reason_ = new QComboBox();
    reason_->setObjectName(QStringLiteral("signReason"));
    reason_->setEditable(true);
    reason_->setInsertPolicy(QComboBox::NoInsert);
    // Длинные стандартные причины не раздвигают диалог: список шире поля.
    reason_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    reason_->setMinimumContentsLength(24);
    reason_->addItem(QString());
    QStringList reasons = settings.get(settings::kSignReasons);
    if (reasons.isEmpty()) {
        reasons = SignTexts::standardReasons();
    }
    reason_->addItems(reasons);
    rows_ << sheet_->addRow(QString(), reason_);
    location_ = new QLineEdit();
    location_->setObjectName(QStringLiteral("signLocation"));
    rows_ << sheet_->addRow(QString(), location_);
    contact_ = new QLineEdit();
    contact_->setObjectName(QStringLiteral("signContact"));
    rows_ << sheet_->addRow(QString(), contact_);
    sign_dialog::setRowVisible(rows_[rows_.size() - 3], reason_, settings.get(settings::kSignShowReason));
    const bool showLocation = settings.get(settings::kSignShowLocation);
    sign_dialog::setRowVisible(rows_[rows_.size() - 2], location_, showLocation);
    sign_dialog::setRowVisible(rows_.last(), contact_, showLocation);
    lock_ = new QCheckBox();
    lock_->setObjectName(QStringLiteral("signLock"));
    sheet_->addWidget(lock_);
    permission_ = new QComboBox();
    permission_->setObjectName(QStringLiteral("signPermission"));
    for (const sign::MdpPermission p : {sign::MdpPermission::NoChanges, sign::MdpPermission::FormFilling,
                                        sign::MdpPermission::Annotations}) {
        permission_->addItem(SignTexts::permission(p), static_cast<int>(p));
    }
    permission_->setCurrentIndex(1);
    rows_ << sheet_->addRow(QString(), permission_);
    const bool certify = context_.mode == Mode::Certify;
    lock_->setVisible(!certify);
    sign_dialog::setRowVisible(rows_.last(), permission_, certify);

    // Расширенно: уровень PAdES и служба времени.
    advanced_ = new QToolButton();
    advanced_->setObjectName(QStringLiteral("signAdvanced"));
    advanced_->setCheckable(true);
    advanced_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    advanced_->setArrowType(Qt::RightArrow);
    sheet_->addWidget(advanced_);
    advancedPanel_ = new QWidget();
    advancedPanel_->setObjectName(QStringLiteral("signAdvancedPanel"));
    auto* ap = new QVBoxLayout(advancedPanel_);
    ap->setContentsMargins(0, 0, 0, 0);
    advancedSheet_ = new PropertySheet(advancedPanel_);
    ap->addWidget(advancedSheet_);
    level_ = new QComboBox();
    level_->setObjectName(QStringLiteral("signLevel"));
    for (const sign::SignLevel l :
         {sign::SignLevel::B, sign::SignLevel::T, sign::SignLevel::LT, sign::SignLevel::LTA}) {
        level_->addItem(QString(), static_cast<int>(l));
    }
    tsa_ = new QComboBox();
    tsa_->setObjectName(QStringLiteral("signTsa"));
    for (const sign::TsaServer& s : servers_.servers()) {
        tsa_->addItem(QString::fromStdString(s.name), QString::fromStdString(s.name));
        tsa_->setItemData(tsa_->count() - 1, QString::fromStdString(s.url), Qt::ToolTipRole);
    }
    const int def = tsa_->findData(QString::fromStdString(servers_.defaultName()));
    tsa_->setCurrentIndex(def >= 0 ? def : 0);
    advancedRows_ << advancedSheet_->addRow(QString(), level_);
    advancedRows_ << advancedSheet_->addRow(QString(), tsa_);
    levelNote_ = new QLabel();
    levelNote_->setObjectName(QStringLiteral("signLevelNote"));
    levelNote_->setWordWrap(true);
    advancedSheet_->addWidget(levelNote_);
    advancedPanel_->hide();
    sheet_->addWidget(advancedPanel_);
    // Уровень по умолчанию — B-T с меткой службы по умолчанию (решение 4.2); нет служб или метка выключена —
    // B-B.
    const bool timestamp = settings.get(settings::kTimestampSignatures) && tsa_->count() > 0;
    level_->setCurrentIndex(timestamp ? 1 : 0);
    if (tsa_->count() == 0) {
        for (int i = 1; i < level_->count(); ++i) {
            level_->setItemData(i, false, Qt::UserRole - 1);  // недоступны без службы времени
        }
        tsa_->setEnabled(false);
    }

    // «Проверить документ» (sign-17): по настройке — всегда, при сертификации, никогда.
    auto* reviewRow = new QWidget();
    auto* rr = new QHBoxLayout(reviewRow);
    rr->setContentsMargins(0, 0, 0, 0);
    review_ = new QPushButton(Icons::icon(QStringLiteral("scan-search")), QString());
    review_->setObjectName(QStringLiteral("signReview"));
    warningsNote_ = new QLabel();
    warningsNote_->setObjectName(QStringLiteral("signWarnings"));
    warningsNote_->setWordWrap(true);
    rr->addWidget(review_);
    rr->addWidget(warningsNote_, 1);
    sheet_->addWidget(reviewRow);
    const QString review = settings.get(settings::kSignReviewWarnings);
    reviewRow->setVisible(review == QLatin1String("always") ||
                          (review == QLatin1String("certify") && certify));

    error_ = new QLabel(this);
    error_->setObjectName(QStringLiteral("signError"));
    error_->setWordWrap(true);
    error_->hide();
    column->addWidget(error_);
    auto* buttons = new QDialogButtonBox(this);
    sign_ = buttons->addButton(QString(), QDialogButtonBox::AcceptRole);
    sign_->setObjectName(QStringLiteral("signSubmit"));
    sign_->setDefault(true);
    sign_->setProperty("primary", true);
    repolish(sign_);
    cancel_ = buttons->addButton(QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &SignDialog::submit);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    column->addWidget(buttons);

    connect(identity_, &QComboBox::currentIndexChanged, this, &SignDialog::updateIdentity);
    connect(appearance_, &QComboBox::currentIndexChanged, this, &SignDialog::refreshPreview);
    connect(reason_, &QComboBox::currentTextChanged, this, &SignDialog::refreshPreview);
    connect(location_, &QLineEdit::textChanged, this, &SignDialog::refreshPreview);
    connect(addFile_, &QPushButton::clicked, this, &SignDialog::addFile);
    connect(windows_, &QPushButton::clicked, this, &SignDialog::pickWindows);
    connect(create_, &QPushButton::clicked, this, &SignDialog::createIdentity);
    connect(newAppearance_, &QPushButton::clicked, this, &SignDialog::createAppearance);
    connect(advanced_, &QToolButton::toggled, this, [this](bool on) {
        advancedPanel_->setVisible(on);
        advanced_->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
    });
    connect(level_, &QComboBox::currentIndexChanged, this,
            [this] { tsa_->setEnabled(level() != sign::SignLevel::B && tsa_->count() > 0); });
    connect(review_, &QPushButton::clicked, this, [this] {
        if (scan_) {
            showWarnings();
        } else {
            reviewed_ = true;
            runReview();
        }
    });
    resize(m.dialogWidth * 3 / 4, m.dialogHeight + m.cardRow * 2);
}

void SignDialog::setScanner(Scanner scanner) {
    scanner_ = std::move(scanner);
    review_->setEnabled(static_cast<bool>(scanner_));
}

void SignDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    if (!scan_ && !scanning_ && review_->parentWidget()->isVisible()) {
        runReview();  // проверка сразу — итог строкой рядом с кнопкой
    }
}

void SignDialog::runReview() {
    if (!scanner_ || scanning_) {
        return;
    }
    scanning_ = true;
    warningsNote_->setText(tr("Проверяю документ…"));
    SignTexts::clearNote(warningsNote_);
    scanner_().then(this, [this](QFuture<sign::WarningScan> done) {
        scanning_ = false;
        try {
            scan_ = done.result();
        } catch (const std::exception&) {
            warningsNote_->setText(tr("Проверить документ не удалось."));
            return;
        }
        showWarningsSummary();
        if (reviewed_) {
            reviewed_ = false;
            showWarnings();
        }
    });
}

void SignDialog::showWarningsSummary() {
    if (!scan_) {
        return;
    }
    if (scan_->clean()) {
        warningsNote_->setText(tr("Предупреждений нет."));
        SignTexts::setNote(warningsNote_, Tone::Ok);
    } else {
        warningsNote_->setText(
            tr("Найдено предупреждений: %1 — нажмите «Просмотреть».").arg(scan_->warnings.size()));
        SignTexts::setNote(warningsNote_,
                           scan_->worst() == sign::WarningLevel::Info ? Tone::Info : Tone::Warn);
    }
    review_->setText(tr("П&росмотреть предупреждения"));
}

QDialog* SignDialog::showWarnings() {
    if (!scan_) {
        return nullptr;
    }
    auto* d = new WarningsDialog(*scan_, this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    connect(d, &WarningsDialog::reviewRequested, this, [this](int page) { emit goToPage(page); });
    d->open();
    return d;
}

// ---------------------------------------------------------------- удостоверения

void SignDialog::reloadIdentities(const QString& select) {
    QString want = select;
    try {
        records_ = Identities::shared().with([](sign::IdentityStore& s) { return s.list(); });
        if (want.isEmpty()) {
            if (const auto def =
                    Identities::shared().with([](sign::IdentityStore& s) { return s.defaultIdentity(); })) {
                want = QString::fromStdString(def->id);
            }
        }
    } catch (const std::exception& e) {
        records_.clear();
        fail(tr("Список удостоверений не прочитан: %1").arg(QString::fromUtf8(e.what())));
    }
    const QSignalBlocker block(identity_);
    identity_->clear();
    for (const sign::IdentityRecord& r : records_) {
        const QString name = QString::fromStdString(r.name);
        const QString issuer = sign_dialog::commonNameOf(QString::fromStdString(r.issuer));
        const QDateTime until = QDateTime::fromString(QString::fromStdString(r.notAfter), Qt::ISODate);
        QString text = issuer.isEmpty() || issuer == name ? name : tr("%1 — выдан %2").arg(name, issuer);
        if (until.isValid()) {
            text += tr(", до %1").arg(QLocale().toString(until.toLocalTime().date(), QLocale::ShortFormat));
        }
        if (r.kind == sign::IdentityKind::WindowsStore) {
            text += tr(" · хранилище Windows");
        }
        identity_->addItem(
            Icons::icon(r.kind == sign::IdentityKind::WindowsStore ? QStringLiteral("key-round")
                                                                   : QStringLiteral("file-key")),
            text, QString::fromStdString(r.id));
    }
    const int index = identity_->findData(want);
    identity_->setCurrentIndex(index >= 0 ? index : (identity_->count() > 0 ? 0 : -1));
    updateIdentity();
}

std::optional<sign::IdentityRecord> SignDialog::currentRecord() const {
    const QString id = identity_->currentData().toString();
    for (const sign::IdentityRecord& r : records_) {
        if (QString::fromStdString(r.id) == id) {
            return r;
        }
    }
    return std::nullopt;
}

void SignDialog::updateIdentity() {
    const std::optional<sign::IdentityRecord> r = currentRecord();
    const bool file = r && r->kind == sign::IdentityKind::Pkcs12File;
    sign_dialog::setRowVisible(rows_.value(1), password_, file || !r);
    remember_->setVisible(file);
    password_->setEnabled(file);
    if (file) {
        password_->setPlaceholderText(r->rememberPassword ? tr("Пароль запомнен — можно не вводить")
                                                          : tr("Пароль файла удостоверения"));
        remember_->setChecked(r->rememberPassword);
    }
    if (r && sign_dialog::expired(*r)) {
        identityNote_->setText(tr("Срок действия сертификата истёк — подписать им нельзя."));
        SignTexts::setNote(identityNote_, Tone::Danger);
        identityNote_->show();
    } else if (!r) {
        identityNote_->setText(
            tr("Удостоверений пока нет: добавьте файл .pfx, выберите сертификат Windows или создайте "
               "самоподписанное."));
        SignTexts::setNote(identityNote_, Tone::Info);
        identityNote_->show();
    } else {
        identityNote_->hide();
    }
    sign_->setEnabled(r.has_value());
    refreshPreview();
}

bool SignDialog::addPfx(const QString& path, const QString& password) {
    try {
        const sign::IdentityRecord r = Identities::shared().with([&](sign::IdentityStore& s) {
            return s.addPkcs12(std::filesystem::path(path.toStdWString()), password.toStdString(), false);
        });
        error_->hide();
        reloadIdentities(QString::fromStdString(r.id));
        password_->setText(password);  // только что проверен — не спрашивать второй раз
        return true;
    } catch (const sign::IdentityError& e) {
        if (e.status() == sign::IdentityStatus::Exists) {
            fail(SignTexts::identityError(e.status()));
            return false;
        }
        fail(tr("«%1»: %2").arg(QFileInfo(path).fileName(), SignTexts::identityError(e.status())));
    } catch (const std::exception& e) {
        fail(tr("«%1» не добавлен: %2").arg(QFileInfo(path).fileName(), QString::fromUtf8(e.what())));
    }
    return false;
}

bool SignDialog::addWindowsCertificate(const QByteArray& der) {
    try {
        const auto bytes = std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(der.constData()), static_cast<std::size_t>(der.size()));
        sign::Ossl<X509> cert = sign::loadCertificate(bytes);
        const sign::IdentityRecord r =
            Identities::shared().with([&](sign::IdentityStore& s) { return s.addWindows(cert.get()); });
        error_->hide();
        reloadIdentities(QString::fromStdString(r.id));
        return true;
    } catch (const sign::IdentityError& e) {
        if (e.status() == sign::IdentityStatus::Exists) {
            // Уже в списке — просто выбрать.
            sign::Ossl<X509> cert = sign::loadCertificate(
                std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(der.constData()),
                                              static_cast<std::size_t>(der.size())));
            reloadIdentities(QString::fromStdString(sign::certificateId(cert.get())));
            return true;
        }
        fail(SignTexts::identityError(e.status()));
    } catch (const std::exception& e) {
        fail(tr("Сертификат не добавлен: %1").arg(QString::fromUtf8(e.what())));
    }
    return false;
}

void SignDialog::addFile() {
    const QString path = chooser_ ? chooser_(this)
                                  : QFileDialog::getOpenFileName(this, tr("Файл удостоверения"), QString(),
                                                                 tr("Удостоверения (*.pfx *.p12)"));
    if (path.isEmpty()) {
        return;
    }
    std::optional<QString> password;
    if (passwordPrompt_) {
        password = passwordPrompt_(this, path);
    } else {
        bool ok = false;
        const QString text = QInputDialog::getText(this, tr("Пароль удостоверения"),
                                                   tr("Пароль файла «%1»:").arg(QFileInfo(path).fileName()),
                                                   QLineEdit::Password, QString(), &ok);
        if (ok) {
            password = text;
        }
    }
    if (password) {
        addPfx(path, *password);
    }
}

void SignDialog::pickWindows() {
    const std::optional<PickedCertificate> picked =
        CertPicker::pick(static_cast<quintptr>(window()->winId()), tr("Сертификат для подписи"),
                         tr("Выберите сертификат с закрытым ключом для цифровой подписи"));
    if (picked) {
        addWindowsCertificate(picked->der);
    }
}

void SignDialog::createIdentity() {
    auto* d = new NewIdentityDialog(this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    connect(d, &QDialog::accepted, this, [this, d] {
        if (d->record()) {
            reloadIdentities(QString::fromStdString(d->record()->id));
            password_->setText(d->passwordField()->text());
        }
    });
    d->open();
}

QString SignDialog::identityId() const { return identity_->currentData().toString(); }

QString SignDialog::signerName() const {
    const std::optional<sign::IdentityRecord> r = currentRecord();
    return r ? QString::fromStdString(r->name) : QString();
}

// ---------------------------------------------------------------- внешний вид

void SignDialog::reloadAppearances(const QString& select) {
    const Settings settings;
    appearances_ = loadAppearances(settings);
    const QString want = select.isEmpty() ? settings.get(settings::kSignAppearance) : select;
    const QSignalBlocker block(appearance_);
    appearance_->clear();
    appearance_->addItem(QString(), QString());  // «Стандартный»
    for (const NamedAppearance& a : appearances_) {
        appearance_->addItem(a.name, a.name);
    }
    const int index = appearance_->findData(want);
    appearance_->setCurrentIndex(index >= 0 ? index : 0);
    appearance_->setItemText(0, tr("Стандартный"));
    refreshPreview();
}

void SignDialog::createAppearance() {
    sign::AppearanceData sample;
    sample.name = signerName().toStdString();
    sample.reason = reason().toStdString();
    sample.location = location().toStdString();
    sample.time = QDateTime::currentSecsSinceEpoch();
    NamedAppearance initial;
    initial.look = appearance();
    auto* d = new AppearanceDialog(initial, sample, this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    connect(d, &QDialog::accepted, this, [this, d] {
        const NamedAppearance made = d->appearance();
        Settings settings;
        std::vector<NamedAppearance> list = loadAppearances(settings);
        bool replaced = false;
        for (NamedAppearance& a : list) {
            if (a.name == made.name) {
                a = made;
                replaced = true;
            }
        }
        if (!replaced) {
            list.push_back(made);
        }
        saveAppearances(settings, list);
        reloadAppearances(made.name);
    });
    d->open();
}

sign::SignatureAppearance SignDialog::appearance() const {
    const QString name = appearance_->currentData().toString();
    for (const NamedAppearance& a : appearances_) {
        if (a.name == name) {
            return a.look;
        }
    }
    return {};
}

void SignDialog::refreshPreview() {
    if (!context_.visible || preview_ == nullptr) {
        return;
    }
    const std::optional<sign::IdentityRecord> r = currentRecord();
    sign::AppearanceData sample;
    sample.name = r ? r->name : tr("Имя подписанта").toStdString();
    sample.dn = r ? r->subject : std::string();
    sample.reason = reason().toStdString();
    sample.location = location().toStdString();
    sample.time = QDateTime::currentSecsSinceEpoch();
    const QSizeF size = context_.rect;
    // Предпросмотр не шире строки диалога: рамка пропорционально.
    const double maxWidth = std::max(120.0, static_cast<double>(Theme::sizes().dialogWidth) / 2.0);
    const double scale = size.width() > maxWidth ? maxWidth / size.width() : 1.0;
    const QPixmap pixmap = appearancePreview(appearance(), sample, size, devicePixelRatioF(), scale);
    if (pixmap.isNull()) {
        preview_->setPixmap({});
        preview_->setText(tr("Предпросмотр недоступен: изображение внешнего вида не читается."));
        return;
    }
    preview_->setText({});
    preview_->setPixmap(pixmap);
}

// ---------------------------------------------------------------- результат

QString SignDialog::reason() const {
    return reason_->isVisibleTo(sheet_) ? reason_->currentText().trimmed() : QString();
}

QString SignDialog::location() const {
    return location_->isVisibleTo(sheet_) ? location_->text().trimmed() : QString();
}

QString SignDialog::contact() const {
    return contact_->isVisibleTo(sheet_) ? contact_->text().trimmed() : QString();
}

bool SignDialog::lockDocument() const { return context_.mode == Mode::Sign && lock_->isChecked(); }

sign::SignLevel SignDialog::level() const {
    return static_cast<sign::SignLevel>(level_->currentData().toInt());
}

std::optional<sign::TsaServer> SignDialog::tsaServer() const {
    if (level() == sign::SignLevel::B) {
        return std::nullopt;
    }
    if (const sign::TsaServer* s = servers_.find(tsa_->currentData().toString().toStdString())) {
        return *s;
    }
    return std::nullopt;
}

sign::MdpPermission SignDialog::permission() const {
    return context_.mode == Mode::Certify
               ? static_cast<sign::MdpPermission>(permission_->currentData().toInt())
               : sign::MdpPermission::None;
}

void SignDialog::fail(const QString& text) {
    error_->setText(text);
    SignTexts::setNote(error_, Tone::Danger);
    error_->show();
}

void SignDialog::submit() {
    const std::optional<sign::IdentityRecord> r = currentRecord();
    if (!r) {
        fail(tr("Выберите удостоверение или создайте новое."));
        return;
    }
    if (sign_dialog::expired(*r)) {
        fail(tr("Срок действия сертификата истёк — выберите другое удостоверение."));
        return;
    }
    if (r->kind == sign::IdentityKind::WindowsStore) {
        sign::WindowsCert cert;
        cert.thumbprint = r->thumbprint;
        cert.window = static_cast<std::uintptr_t>(window()->winId());
        identityResult_ = cert;
        error_->hide();
        accept();
        return;
    }
    const QString text = password_->text();
    std::optional<std::string> password;
    if (!text.isEmpty()) {
        password = text.toStdString();
    }
    try {
        sign::SigningKey key =
            Identities::shared().with([&](sign::IdentityStore& s) { return s.open(r->id, password); });
        identityResult_ = std::make_shared<const sign::SigningKey>(std::move(key));
        if (password && remember_->isChecked() && !r->rememberPassword) {
            Identities::shared().with(
                [&](sign::IdentityStore& s) { s.setRememberedPassword(r->id, password); });
        } else if (!remember_->isChecked() && r->rememberPassword) {
            Identities::shared().with(
                [&](sign::IdentityStore& s) { s.setRememberedPassword(r->id, std::nullopt); });
        }
    } catch (const sign::IdentityError& e) {
        fail(SignTexts::identityError(e.status()));
        if (e.status() == sign::IdentityStatus::NeedPassword ||
            e.status() == sign::IdentityStatus::BadPassword) {
            password_->clear();
            password_->setFocus();
        }
        return;
    } catch (const std::exception& e) {
        fail(tr("Удостоверение не открылось: %1").arg(QString::fromUtf8(e.what())));
        return;
    }
    error_->hide();
    accept();
}

void SignDialog::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QDialog::changeEvent(event);
}

void SignDialog::retranslate() {
    const bool certify = context_.mode == Mode::Certify;
    setWindowTitle(certify ? tr("Сертифицировать документ") : tr("Подписать как"));
    if (certify) {
        intro_->setText(tr("Сертифицирующая подпись подтверждает содержимое «%1» и задаёт, какие изменения "
                           "разрешены после неё. Её ставят первой — до других подписей.")
                            .arg(context_.documentTitle));
    } else {
        intro_->setText(tr("Цифровая подпись документа «%1»: файл сохранится с подписью, и любое изменение "
                           "после неё будет видно при проверке.")
                            .arg(context_.documentTitle));
    }
    const QStringList sections = {tr("Удостоверение"), tr("Внешний вид"), tr("Сведения о подписи")};
    for (int i = 0; i < sections_.size(); ++i) {
        sections_[i]->setText(sections.value(i));
    }
    const QStringList rows = {
        tr("&Удостоверение:"), tr("&Пароль:"),  tr("&Внешний вид:"),          tr("П&ричина:"),
        tr("&Место:"),         tr("&Контакт:"), tr("Разрешённые &изменения:")};
    for (int i = 0; i < rows_.size(); ++i) {
        rows_[i]->setText(rows.value(i));
    }
    addFile_->setText(tr("Добавить &файл…"));
    addFile_->setToolTip(tr("Удостоверение из файла .pfx или .p12 с паролем"));
    windows_->setText(tr("Выбрать в &Windows…"));
    windows_->setToolTip(
        tr("Сертификат из хранилища Windows «Личное» — в том числе на смарт-карте или токене"));
    windowsNote_->setText(tr("Выбор сертификата Windows доступен только в окне программы на экране."));
    SignTexts::setNote(windowsNote_, Tone::Info);
    create_->setText(tr("Со&здать…"));
    create_->setToolTip(tr("Создать самоподписанное удостоверение"));
    remember_->setText(tr("Запомнить па&роль (защищён Windows)"));
    newAppearance_->setText(tr("Со&здать…"));
    newAppearance_->setToolTip(tr("Настроить свой внешний вид подписи"));
    appearance_->setItemText(0, tr("Стандартный"));
    reason_->setItemText(0, QString());
    reason_->lineEdit()->setPlaceholderText(tr("Без причины"));
    location_->setPlaceholderText(tr("Например, Москва"));
    contact_->setPlaceholderText(tr("Телефон или эл. почта"));
    lock_->setText(tr("Заблокировать &документ после подписи"));
    lock_->setToolTip(tr("Поля формы и сама подпись станут только для чтения"));
    advanced_->setText(tr("Р&асширенно"));
    const QStringList levels = {tr("Без метки времени (B-B)"), tr("С меткой времени (B-T)"),
                                tr("Для долгосрочной проверки (B-LT)"), tr("С архивной меткой (B-LTA)")};
    for (int i = 0; i < level_->count(); ++i) {
        level_->setItemText(i, levels.value(i));
    }
    const QStringList advancedRows = {tr("&Уровень подписи:"), tr("Служба &времени:")};
    for (int i = 0; i < advancedRows_.size(); ++i) {
        advancedRows_[i]->setText(advancedRows.value(i));
    }
    levelNote_->setText(
        tr("B-LT и B-LTA сразу добавляют сведения для проверки (цепочку и отзыв сертификатов), "
           "B-LTA — ещё и метку времени документа. Нужна сеть."));
    SignTexts::setNote(levelNote_, Tone::Info);
    if (!scan_) {
        review_->setText(tr("Проверить &документ"));
        warningsNote_->setText(tr("Что может изменить вид документа после подписи"));
    } else {
        showWarningsSummary();
    }
    sign_->setText(certify ? tr("&Сертифицировать") : tr("&Подписать"));
    cancel_->setText(tr("Отмена"));
}

}  // namespace ot::ui
