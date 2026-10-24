#include "ui/shell/preferences/signatures_page.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

#include <algorithm>

#include "services/settings.h"
#include "ui/shell/preferences/identities_widget.h"
#include "ui/shell/preferences/trusted_widget.h"
#include "ui/shell/preferences/tsa_widget.h"
#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/tools/certificate/appearance_dialog.h"
#include "ui/tools/certificate/sign_texts.h"
#include "ui/widgets/property_sheet.h"

namespace ot::ui {

namespace signatures_page {

constexpr int kAppearanceNameRole = Qt::UserRole + 1;

template <std::size_t N>
void addChoices(QComboBox* box, const char* const (&values)[N]) {
    for (const char* v : values) {
        box->addItem(QString(), QString::fromLatin1(v));
    }
}

// Длинные пункты не раздвигают страницу Установок (список выпадает шире поля).
void compact(QComboBox* box) {
    box->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    box->setMinimumContentsLength(16);
}

void selectChoice(QComboBox* box, const QString& value) {
    const int index = box->findData(value);
    box->setCurrentIndex(index >= 0 ? index : 0);
}

QWidget* scrollPage(QWidget* content) {
    auto* page = new QWidget();
    auto* column = new QVBoxLayout(page);
    const Metrics& m = Theme::sizes();
    column->setContentsMargins(m.grid * 3, m.grid * 3, m.grid * 3, m.grid * 3);
    column->addWidget(content);
    column->addStretch(1);
    return page;
}

}  // namespace signatures_page

SignaturesPage::SignaturesPage(QWidget* parent) : PreferencesPage(parent) {
    setObjectName(QStringLiteral("signaturesPage"));
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    tabs_ = new QTabWidget(this);
    tabs_->setObjectName(QStringLiteral("signaturesTabs"));
    tabs_->addTab(signatures_page::scrollPage(buildCreation()), QString());
    tabs_->addTab(signatures_page::scrollPage(buildVerification()), QString());
    identities_ = new IdentitiesWidget();
    tabs_->addTab(signatures_page::scrollPage(identities_), QString());
    trusted_ = new TrustedWidget();
    tabs_->addTab(signatures_page::scrollPage(trusted_), QString());
    tsa_ = new TsaWidget();
    tabs_->addTab(signatures_page::scrollPage(tsa_), QString());
    column->addWidget(tabs_);
    retranslate();
}

QStringList SignaturesPage::tabIds() {
    return {QStringLiteral("creation"), QStringLiteral("verification"), QStringLiteral("identities"),
            QStringLiteral("trusted"), QStringLiteral("tsa")};
}

bool SignaturesPage::showTab(const QString& id) {
    const int index = static_cast<int>(tabIds().indexOf(id));
    if (index < 0) {
        return false;
    }
    tabs_->setCurrentIndex(index);
    return true;
}

QString SignaturesPage::title() const { return tr("Подписи"); }

QWidget* SignaturesPage::buildCreation() {
    const Metrics& m = Theme::sizes();
    creation_ = new PropertySheet();
    format_ = new QComboBox();
    format_->setObjectName(QStringLiteral("signFormat"));
    format_->addItem(QString(), false);
    format_->addItem(QString(), true);
    showReason_ = new QCheckBox();
    showReason_->setObjectName(QStringLiteral("signShowReason"));
    showLocation_ = new QCheckBox();
    showLocation_->setObjectName(QStringLiteral("signShowLocation"));
    reasons_ = new QPlainTextEdit();
    reasons_->setObjectName(QStringLiteral("signReasons"));
    reasons_->setMaximumHeight(m.row * 4);
    review_ = new QComboBox();
    review_->setObjectName(QStringLiteral("signReview"));
    static const char* const kReview[] = {"never", "certify", "always"};
    signatures_page::addChoices(review_, kReview);
    for (QComboBox* box : {format_, review_}) {
        signatures_page::compact(box);
    }
    appearances_ = new QListWidget();
    appearances_->setObjectName(QStringLiteral("signAppearances"));
    appearances_->setMaximumHeight(m.row * 5);
    auto* buttons = new QWidget();
    auto* row = new QHBoxLayout(buttons);
    row->setContentsMargins(0, 0, 0, 0);
    lookNew_ = new QPushButton(Icons::icon(QStringLiteral("plus")), QString());
    lookNew_->setObjectName(QStringLiteral("signAppearanceNew"));
    lookEdit_ = new QPushButton();
    lookEdit_->setObjectName(QStringLiteral("signAppearanceEdit"));
    lookRemove_ = new QPushButton(Icons::icon(QStringLiteral("trash-2")), QString());
    lookRemove_->setObjectName(QStringLiteral("signAppearanceRemove"));
    lookDefault_ = new QPushButton(Icons::icon(QStringLiteral("star")), QString());
    lookDefault_->setObjectName(QStringLiteral("signAppearanceDefault"));
    for (QPushButton* b : {lookNew_, lookEdit_, lookRemove_, lookDefault_}) {
        row->addWidget(b);
    }
    row->addStretch(1);

    sections_ << creation_->addSection(QString());
    rows_ << creation_->addRow(QString(), format_);
    creation_->addWidget(showReason_);
    creation_->addWidget(showLocation_);
    rows_ << creation_->addRow(QString(), reasons_);
    rows_ << creation_->addRow(QString(), review_);
    sections_ << creation_->addSection(QString());
    creation_->addWidget(appearances_);
    creation_->addWidget(buttons);

    connect(lookNew_, &QPushButton::clicked, this, &SignaturesPage::newAppearance);
    connect(lookEdit_, &QPushButton::clicked, this, &SignaturesPage::editAppearance);
    connect(appearances_, &QListWidget::itemDoubleClicked, this, &SignaturesPage::editAppearance);
    connect(lookRemove_, &QPushButton::clicked, this, &SignaturesPage::removeAppearance);
    connect(lookDefault_, &QPushButton::clicked, this, &SignaturesPage::setDefaultAppearance);
    connect(appearances_, &QListWidget::currentRowChanged, this, [this] {
        const bool custom = !selectedAppearance().isEmpty();
        lookEdit_->setEnabled(custom);
        lookRemove_->setEnabled(custom);
        lookDefault_->setEnabled(selectedAppearance() != defaultLook_);
    });
    return creation_;
}

QWidget* SignaturesPage::buildVerification() {
    verification_ = new PropertySheet();
    verifyOnOpen_ = new QCheckBox();
    verifyOnOpen_->setObjectName(QStringLiteral("verifyOnOpen"));
    verifyTime_ = new QComboBox();
    verifyTime_->setObjectName(QStringLiteral("verifyTime"));
    static const char* const kTimes[] = {"signing", "secure", "current"};
    signatures_page::addChoices(verifyTime_, kTimes);
    revocation_ = new QComboBox();
    revocation_->setObjectName(QStringLiteral("verifyRevocation"));
    static const char* const kRevocation[] = {"required", "bestEffort", "never"};
    signatures_page::addChoices(revocation_, kRevocation);
    expired_ = new QCheckBox();
    expired_->setObjectName(QStringLiteral("verifyExpired"));
    promptTrust_ = new QCheckBox();
    promptTrust_->setObjectName(QStringLiteral("verifyPromptTrust"));
    ltv_ = new QComboBox();
    ltv_->setObjectName(QStringLiteral("verifyLtv"));
    static const char* const kLtv[] = {"never", "ask", "always"};
    signatures_page::addChoices(ltv_, kLtv);
    vri_ = new QCheckBox();
    vri_->setObjectName(QStringLiteral("verifyVri"));
    for (QComboBox* box : {verifyTime_, revocation_, ltv_}) {
        signatures_page::compact(box);
    }

    sections_ << verification_->addSection(QString());
    verification_->addWidget(verifyOnOpen_);
    verification_->addWidget(promptTrust_);
    sections_ << verification_->addSection(QString());
    rows_ << verification_->addRow(QString(), verifyTime_);
    verification_->addWidget(expired_);
    rows_ << verification_->addRow(QString(), revocation_);
    sections_ << verification_->addSection(QString());
    rows_ << verification_->addRow(QString(), ltv_);
    verification_->addWidget(vri_);
    return verification_;
}

void SignaturesPage::load(const Settings& settings) {
    format_->setCurrentIndex(settings.get(settings::kSignCompatibility) ? 1 : 0);
    showReason_->setChecked(settings.get(settings::kSignShowReason));
    showLocation_->setChecked(settings.get(settings::kSignShowLocation));
    reasons_->setPlainText(settings.get(settings::kSignReasons).join(QLatin1Char('\n')));
    signatures_page::selectChoice(review_, settings.get(settings::kSignReviewWarnings));
    looks_ = loadAppearances(settings);
    defaultLook_ = settings.get(settings::kSignAppearance);
    fillAppearances();
    verifyOnOpen_->setChecked(settings.get(settings::kVerifyOnOpen));
    signatures_page::selectChoice(verifyTime_, settings.get(settings::kVerifyTime));
    signatures_page::selectChoice(revocation_, settings.get(settings::kVerifyRevocation));
    expired_->setChecked(settings.get(settings::kVerifyExpiredTimestamps));
    promptTrust_->setChecked(settings.get(settings::kVerifyPromptTrust));
    signatures_page::selectChoice(ltv_, settings.get(settings::kAutoAddLtv));
    vri_->setChecked(settings.get(settings::kLtvVri));
    identities_->reload();
    trusted_->load(settings);
    tsa_->load(settings);
}

void SignaturesPage::apply(Settings& settings) {
    settings.set(settings::kSignCompatibility, format_->currentData().toBool());
    settings.set(settings::kSignShowReason, showReason_->isChecked());
    settings.set(settings::kSignShowLocation, showLocation_->isChecked());
    QStringList reasons;
    for (const QString& line : reasons_->toPlainText().split(QLatin1Char('\n'))) {
        if (!line.trimmed().isEmpty()) {
            reasons << line.trimmed();
        }
    }
    settings.set(settings::kSignReasons, reasons);
    settings.set(settings::kSignReviewWarnings, review_->currentData().toString());
    saveAppearances(settings, looks_);
    settings.set(settings::kSignAppearance, defaultLook_);
    settings.set(settings::kVerifyOnOpen, verifyOnOpen_->isChecked());
    settings.set(settings::kVerifyTime, verifyTime_->currentData().toString());
    settings.set(settings::kVerifyRevocation, revocation_->currentData().toString());
    settings.set(settings::kVerifyExpiredTimestamps, expired_->isChecked());
    settings.set(settings::kVerifyPromptTrust, promptTrust_->isChecked());
    settings.set(settings::kAutoAddLtv, ltv_->currentData().toString());
    settings.set(settings::kLtvVri, vri_->isChecked());
    trusted_->apply(settings);
    tsa_->apply(settings);
}

// ---------------------------------------------------------------- внешние виды

void SignaturesPage::fillAppearances() {
    const QString keep = selectedAppearance();
    const QSignalBlocker block(appearances_);
    appearances_->clear();
    const int icon = Theme::sizes().iconRow;
    const auto add = [&](const QString& text, const QString& name) {
        auto* item = new QListWidgetItem(text, appearances_);
        item->setData(signatures_page::kAppearanceNameRole, name);
        if (name == defaultLook_) {
            item->setIcon(QIcon(Icons::get(QStringLiteral("star"), IconTone::Accent, icon)));
            QFont f = item->font();
            f.setBold(true);
            item->setFont(f);
        }
    };
    add(tr("Стандартный"), QString());
    for (const NamedAppearance& a : looks_) {
        add(a.name, a.name);
    }
    int row = 0;
    for (int i = 0; i < appearances_->count(); ++i) {
        if (appearances_->item(i)->data(signatures_page::kAppearanceNameRole).toString() == keep) {
            row = i;
        }
    }
    appearances_->setCurrentRow(row);
    const bool custom = !selectedAppearance().isEmpty();
    lookEdit_->setEnabled(custom);
    lookRemove_->setEnabled(custom);
    lookDefault_->setEnabled(selectedAppearance() != defaultLook_);
}

QString SignaturesPage::selectedAppearance() const {
    const QListWidgetItem* item = appearances_->currentItem();
    return item != nullptr ? item->data(signatures_page::kAppearanceNameRole).toString() : QString();
}

AppearanceDialog* SignaturesPage::openAppearance(const NamedAppearance& initial, const QString& replaces) {
    sign::AppearanceData sample;
    sample.name = tr("Иван Иванов").toStdString();
    sample.reason = tr("Я утверждаю этот документ").toStdString();
    sample.location = tr("Москва").toStdString();
    sample.time = QDateTime::currentSecsSinceEpoch();
    auto* d = new AppearanceDialog(initial, sample, this);
    d->setAttribute(Qt::WA_DeleteOnClose);
    connect(d, &QDialog::accepted, this, [this, d, replaces] {
        const NamedAppearance made = d->appearance();
        bool replaced = false;
        for (NamedAppearance& a : looks_) {
            if (a.name == made.name || (!replaces.isEmpty() && a.name == replaces)) {
                if (defaultLook_ == a.name) {
                    defaultLook_ = made.name;
                }
                a = made;
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            looks_.push_back(made);
        }
        fillAppearances();
        for (int i = 0; i < appearances_->count(); ++i) {
            if (appearances_->item(i)->data(signatures_page::kAppearanceNameRole).toString() == made.name) {
                appearances_->setCurrentRow(i);
            }
        }
    });
    d->open();
    return d;
}

AppearanceDialog* SignaturesPage::newAppearance() { return openAppearance({}, {}); }

AppearanceDialog* SignaturesPage::editAppearance() {
    const QString name = selectedAppearance();
    for (const NamedAppearance& a : looks_) {
        if (a.name == name) {
            return openAppearance(a, name);
        }
    }
    return nullptr;
}

bool SignaturesPage::removeAppearance() {
    const QString name = selectedAppearance();
    const auto it =
        std::find_if(looks_.begin(), looks_.end(), [&](const NamedAppearance& a) { return a.name == name; });
    if (name.isEmpty() || it == looks_.end()) {
        return false;
    }
    looks_.erase(it);
    if (defaultLook_ == name) {
        defaultLook_.clear();
    }
    fillAppearances();
    return true;
}

bool SignaturesPage::setDefaultAppearance() {
    defaultLook_ = selectedAppearance();
    fillAppearances();
    return true;
}

void SignaturesPage::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    PreferencesPage::changeEvent(event);
}

void SignaturesPage::retranslate() {
    const QStringList tabs = {tr("Создание"), tr("Проверка"), tr("Удостоверения"), tr("Доверенные"),
                              tr("Серверы времени")};
    const QStringList tips = {tr("Создание и внешний вид подписи"), tr("Проверка подписей"),
                              tr("Удостоверения для подписи"), tr("Доверенные сертификаты"),
                              tr("Серверы меток времени")};
    for (int i = 0; i < tabs_->count(); ++i) {
        tabs_->setTabText(i, tabs.value(i));
        tabs_->setTabToolTip(i, tips.value(i));
    }
    const QStringList sections = {tr("При подписании"), tr("Внешние виды подписи"), tr("Поведение"),
                                  tr("Время и отзыв"), tr("Долгосрочная проверка (LTV)")};
    for (int i = 0; i < sections_.size(); ++i) {
        sections_[i]->setText(sections.value(i));
    }
    const QStringList rows = {tr("&Формат подписи:"),     tr("&Свои причины (по строке):"),
                              tr("Проверять &документ:"), tr("Проверять сертификаты на &время:"),
                              tr("Проверка &отзыва:"),    tr("Сведения для проверки &после подписи:")};
    for (int i = 0; i < rows_.size(); ++i) {
        rows_[i]->setText(rows.value(i));
    }
    format_->setItemText(0, tr("PAdES (ETSI.CAdES.detached) — по умолчанию"));
    format_->setItemText(1, tr("PKCS#7 (adbe.pkcs7.detached) — для старых программ"));
    showReason_->setText(tr("Показывать &причину при подписании"));
    showLocation_->setText(tr("Показывать &место и контакт при подписании"));
    reasons_->setPlaceholderText(SignTexts::standardReasons().join(QLatin1Char('\n')));
    reasons_->setToolTip(tr("Пусто — шесть стандартных причин"));
    const QStringList review = {tr("никогда"), tr("при сертификации"), tr("при каждой подписи")};
    for (int i = 0; i < review_->count(); ++i) {
        review_->setItemText(i, review.value(i));
    }
    lookNew_->setText(tr("Со&здать…"));
    lookEdit_->setText(tr("&Изменить…"));
    lookRemove_->setText(tr("&Удалить"));
    lookDefault_->setText(tr("По &умолчанию"));
    verifyOnOpen_->setText(tr("Проверять подписи при &открытии документа"));
    promptTrust_->setText(tr("Предлагать &доверять подписанту, если подпись цела, а подписант не доверен"));
    const QStringList times = {tr("время создания подписи (метка, иначе часы подписавшего)"),
                               tr("безопасное время (метка времени, иначе текущее)"), tr("текущее время")};
    for (int i = 0; i < verifyTime_->count(); ++i) {
        verifyTime_->setItemText(i, times.value(i));
    }
    expired_->setText(tr("Принимать &просроченные метки времени"));
    const QStringList revocation = {tr("обязательно: без сведений — «неизвестно»"),
                                    tr("по возможности (по умолчанию)"), tr("не проверять")};
    for (int i = 0; i < revocation_->count(); ++i) {
        revocation_->setItemText(i, revocation.value(i));
    }
    const QStringList ltv = {tr("не добавлять"), tr("спрашивать"), tr("добавлять всегда")};
    for (int i = 0; i < ltv_->count(); ++i) {
        ltv_->setItemText(i, ltv.value(i));
    }
    vri_->setText(tr("Записывать /&VRI — для старых программ проверки"));
    fillAppearances();
}

}  // namespace ot::ui
