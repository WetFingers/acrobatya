// Диалог «Подписать как» (план 4.6, п. 1; эталон 3.8 «Диалог подписания», 2 — сценарий 4; sign-12, sign-14,
// sign-17, sign-20, sign-08): удостоверение — список своих (services/identities.h: .pfx в <данные>\ids,
// сертификаты хранилища Windows), «Добавить файл…» (.pfx/.p12 с паролем), «Выбрать в Windows…»
// (CryptUIDlgSelectCertificateFromStore — только на экране; в offscreen кнопка серая с подписью),
// «Создать…» (самоподписанное); пароль .pfx (пусто — запомненный или кэш до тайм-аута удостоверения) и
// «Запомнить пароль»; внешний вид (видимая подпись: «Стандартный» и свои из Установок, «Создать…» —
// конструктор) с предпросмотром; причина (свои или шесть стандартных), место и контакт — по настройкам
// «показывать»; «Заблокировать документ после подписи»; «Расширенно» — уровень PAdES (B-B; B-T по умолчанию
// с меткой службы времени из «Серверов времени»; B-LT и B-LTA) и служба времени; «Проверить документ» —
// предупреждения (sign-17) списком с «Просмотреть». Режим «Сертифицировать» (sign-16) — тот же диалог с
// «Разрешёнными изменениями» вместо блокировки (CertifyDialog). «Подписать» открывает удостоверение (пароль
// проверяется здесь, диалог остаётся при ошибке) и закрывает диалог — запись делает инструмент.
#pragma once

#include <QDialog>
#include <QFuture>
#include <QSizeF>

#include <functional>
#include <optional>
#include <vector>

#include "core/sign/docmdp.h"
#include "core/sign/identity.h"
#include "core/sign/signer.h"
#include "core/sign/tsa_servers.h"
#include "core/sign/warnings_scan.h"
#include "services/sign_settings.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;

namespace ot::ui {

class PropertySheet;

class SignDialog : public QDialog {
    Q_OBJECT
public:
    enum class Mode { Sign, Certify };
    struct Context {
        Mode mode = Mode::Sign;
        bool visible = true;   // видимая подпись: внешний вид и предпросмотр
        QSizeF rect{234, 90};  // рамка в пунктах (предпросмотр)
        QString documentTitle;
    };
    // «Добавить файл…»: путь .pfx (пусто — отмена) и пароль к нему (nullopt — отмена).
    using FileChooser = std::function<QString(QWidget* parent)>;
    using PasswordPrompt = std::function<std::optional<QString>(QWidget* parent, const QString& file)>;
    // «Проверить документ»: предупреждения документа (задача потока документа).
    using Scanner = std::function<QFuture<sign::WarningScan>()>;

    explicit SignDialog(Context context, QWidget* parent = nullptr);

    Mode mode() const { return context_.mode; }

    // Удостоверения.
    QComboBox* identityBox() const { return identity_; }
    QPushButton* addFileButton() const { return addFile_; }
    QPushButton* windowsButton() const { return windows_; }
    QLabel* windowsNote() const { return windowsNote_; }
    QPushButton* createButton() const { return create_; }
    QLineEdit* passwordField() const { return password_; }
    QCheckBox* rememberBox() const { return remember_; }
    // Внешний вид.
    QComboBox* appearanceBox() const { return appearance_; }
    QPushButton* newAppearanceButton() const { return newAppearance_; }
    QLabel* previewLabel() const { return preview_; }
    // Сведения.
    QComboBox* reasonBox() const { return reason_; }
    QLineEdit* locationField() const { return location_; }
    QLineEdit* contactField() const { return contact_; }
    QCheckBox* lockBox() const { return lock_; }
    QComboBox* permissionBox() const { return permission_; }
    // Расширенно.
    QToolButton* advancedButton() const { return advanced_; }
    QComboBox* levelBox() const { return level_; }
    QComboBox* tsaBox() const { return tsa_; }
    // Проверка документа.
    QPushButton* reviewButton() const { return review_; }
    QLabel* warningsLabel() const { return warningsNote_; }
    QLabel* errorLabel() const { return error_; }
    QPushButton* signButton() const { return sign_; }

    void setFileChooser(FileChooser chooser) { chooser_ = std::move(chooser); }
    void setPasswordPrompt(PasswordPrompt prompt) { passwordPrompt_ = std::move(prompt); }
    void setScanner(Scanner scanner);
    const std::optional<sign::WarningScan>& warnings() const { return scan_; }

    // Перечитать удостоверения (select — id; пусто — по умолчанию).
    void reloadIdentities(const QString& select = {});
    // .pfx в список (с проверкой пароля) и выбрать его; ошибка — в errorLabel, false.
    bool addPfx(const QString& path, const QString& password);
    // Сертификат хранилища Windows (DER) в список и выбрать.
    bool addWindowsCertificate(const QByteArray& der);
    // «Проверить документ»: запустить проверку (результат — warningsLabel, список — WarningsDialog).
    void runReview();
    // Список предупреждений (WarningsDialog поверх, open()); nullptr — проверки ещё не было.
    QDialog* showWarnings();

    // «Подписать»: удостоверение открывается (пароль), при ошибке — строка в диалоге, иначе accept().
    void submit();

    // Результат (после accept).
    const sign::SigningIdentity& identity() const { return identityResult_; }
    QString identityId() const;
    QString signerName() const;
    sign::SignatureAppearance appearance() const;  // ярлыки строк подставляет инструмент
    QString reason() const;
    QString location() const;
    QString contact() const;
    bool lockDocument() const;
    sign::SignLevel level() const;
    std::optional<sign::TsaServer> tsaServer() const;
    sign::MdpPermission permission() const;

signals:
    void goToPage(int page);

protected:
    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    void build();
    void retranslate();
    void reloadAppearances(const QString& select = {});
    void updateIdentity();
    void refreshPreview();
    void createIdentity();
    void addFile();
    void pickWindows();
    void createAppearance();
    void showWarningsSummary();
    void fail(const QString& text);
    std::optional<sign::IdentityRecord> currentRecord() const;

    Context context_;
    std::vector<sign::IdentityRecord> records_;
    std::vector<NamedAppearance> appearances_;
    sign::TsaServers servers_;
    FileChooser chooser_;
    PasswordPrompt passwordPrompt_;
    Scanner scanner_;
    std::optional<sign::WarningScan> scan_;
    bool scanning_ = false;
    bool reviewed_ = false;
    sign::SigningIdentity identityResult_;

    QLabel* intro_ = nullptr;
    PropertySheet* sheet_ = nullptr;
    QComboBox* identity_ = nullptr;
    QPushButton* addFile_ = nullptr;
    QPushButton* windows_ = nullptr;
    QLabel* windowsNote_ = nullptr;
    QPushButton* create_ = nullptr;
    QLineEdit* password_ = nullptr;
    QCheckBox* remember_ = nullptr;
    QLabel* identityNote_ = nullptr;
    QComboBox* appearance_ = nullptr;
    QPushButton* newAppearance_ = nullptr;
    QLabel* preview_ = nullptr;
    QComboBox* reason_ = nullptr;
    QLineEdit* location_ = nullptr;
    QLineEdit* contact_ = nullptr;
    QCheckBox* lock_ = nullptr;
    QComboBox* permission_ = nullptr;
    QToolButton* advanced_ = nullptr;
    QWidget* advancedPanel_ = nullptr;
    PropertySheet* advancedSheet_ = nullptr;
    QComboBox* level_ = nullptr;
    QComboBox* tsa_ = nullptr;
    QLabel* levelNote_ = nullptr;
    QPushButton* review_ = nullptr;
    QLabel* warningsNote_ = nullptr;
    QLabel* error_ = nullptr;
    QPushButton* sign_ = nullptr;
    QPushButton* cancel_ = nullptr;
    QList<QLabel*> sections_;
    QList<QLabel*> rows_;
    QList<QLabel*> advancedRows_;
};

}  // namespace ot::ui
