// «Установки → Подписи» (план 4.6, п. 3; эталон 3.18 «Подписи», 3.8 «Доверие»; sign-05 (UI), sign-10,
// sign-11, sign-13, sign-14, sign-17, sign-19, sign-30, sign-31 (UI), sign-32, sign-34): вкладки как разделы
// Acrobat — «Создание и внешний вид» (формат PAdES/PKCS#7, показывать причину, место и контакт, свои причины,
// проверять документ перед подписью, внешние виды — создать, изменить, удалить, по умолчанию), «Проверка»
// (проверять при открытии, время проверки, отзыв, просроченные метки, предлагать доверять подписанту,
// сведения LTV после подписи и /VRI), «Удостоверения» (IdentitiesWidget), «Доверенные сертификаты»
// (TrustedWidget), «Серверы времени» (TsaWidget). Удостоверения и якоря доверия меняются сразу, остальное —
// по «ОК».
#pragma once

#include <QStringList>

#include <vector>

#include "services/sign_settings.h"
#include "ui/shell/preferences/preferences_page.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;

namespace ot::ui {

class AppearanceDialog;
class IdentitiesWidget;
class PropertySheet;
class TrustedWidget;
class TsaWidget;

class SignaturesPage : public PreferencesPage {
    Q_OBJECT
public:
    explicit SignaturesPage(QWidget* parent = nullptr);

    QString categoryId() const override { return QStringLiteral("signatures"); }
    QString title() const override;
    void load(const Settings& settings) override;
    void apply(Settings& settings) override;

    // Вкладки: creation, verification, identities, trusted, tsa.
    static QStringList tabIds();
    bool showTab(const QString& id);
    QTabWidget* tabs() const { return tabs_; }
    IdentitiesWidget* identities() const { return identities_; }
    TrustedWidget* trusted() const { return trusted_; }
    TsaWidget* tsa() const { return tsa_; }

    // Создание.
    QComboBox* formatBox() const { return format_; }
    QCheckBox* showReasonBox() const { return showReason_; }
    QCheckBox* showLocationBox() const { return showLocation_; }
    QPlainTextEdit* reasonsEdit() const { return reasons_; }
    QComboBox* reviewBox() const { return review_; }
    QListWidget* appearancesList() const { return appearances_; }
    QPushButton* appearanceNewButton() const { return lookNew_; }
    QPushButton* appearanceEditButton() const { return lookEdit_; }
    QPushButton* appearanceRemoveButton() const { return lookRemove_; }
    QPushButton* appearanceDefaultButton() const { return lookDefault_; }
    // Проверка.
    QCheckBox* verifyOnOpenBox() const { return verifyOnOpen_; }
    QComboBox* verifyTimeBox() const { return verifyTime_; }
    QComboBox* revocationBox() const { return revocation_; }
    QCheckBox* expiredBox() const { return expired_; }
    QCheckBox* promptTrustBox() const { return promptTrust_; }
    QComboBox* ltvBox() const { return ltv_; }
    QCheckBox* vriBox() const { return vri_; }

    // Внешние виды (до «ОК»): имена в порядке списка (первый — «Стандартный»), вид по умолчанию.
    const std::vector<NamedAppearance>& appearances() const { return looks_; }
    QString defaultAppearance() const { return defaultLook_; }
    // «Создать…»/«Изменить…»: диалог открыт (open); nullptr — нечего менять.
    AppearanceDialog* newAppearance();
    AppearanceDialog* editAppearance();
    bool removeAppearance();
    bool setDefaultAppearance();

protected:
    void changeEvent(QEvent* event) override;

private:
    QWidget* buildCreation();
    QWidget* buildVerification();
    void retranslate();
    void fillAppearances();
    QString selectedAppearance() const;  // пусто — «Стандартный»
    AppearanceDialog* openAppearance(const NamedAppearance& initial, const QString& replaces);

    QTabWidget* tabs_ = nullptr;
    IdentitiesWidget* identities_ = nullptr;
    TrustedWidget* trusted_ = nullptr;
    TsaWidget* tsa_ = nullptr;
    // Создание.
    PropertySheet* creation_ = nullptr;
    QComboBox* format_ = nullptr;
    QCheckBox* showReason_ = nullptr;
    QCheckBox* showLocation_ = nullptr;
    QPlainTextEdit* reasons_ = nullptr;
    QComboBox* review_ = nullptr;
    QListWidget* appearances_ = nullptr;
    QPushButton* lookNew_ = nullptr;
    QPushButton* lookEdit_ = nullptr;
    QPushButton* lookRemove_ = nullptr;
    QPushButton* lookDefault_ = nullptr;
    std::vector<NamedAppearance> looks_;
    QString defaultLook_;
    // Проверка.
    PropertySheet* verification_ = nullptr;
    QCheckBox* verifyOnOpen_ = nullptr;
    QComboBox* verifyTime_ = nullptr;
    QComboBox* revocation_ = nullptr;
    QCheckBox* expired_ = nullptr;
    QCheckBox* promptTrust_ = nullptr;
    QComboBox* ltv_ = nullptr;
    QCheckBox* vri_ = nullptr;
    QList<QLabel*> sections_;
    QList<QLabel*> rows_;
};

}  // namespace ot::ui
