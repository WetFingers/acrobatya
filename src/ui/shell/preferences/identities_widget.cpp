#include "ui/shell/preferences/identities_widget.h"

#include <QComboBox>
#include <QDateTime>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <exception>

#include "core/sign/x509_info.h"
#include "services/cert_picker.h"
#include "services/identities.h"
#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/tools/certificate/new_identity_dialog.h"
#include "ui/tools/certificate/sign_texts.h"
#include "ui/widgets/tone.h"

namespace ot::ui {

namespace identities_widget {

constexpr int kIdentityIdRole = Qt::UserRole + 1;
// Тайм-аут пароля (секунды): всегда спрашивать, минуты, до выхода (IdentityStore::kUntilExit).
constexpr int kTimeouts[] = {0, 60, 600, 1800, 7200, sign::IdentityStore::kUntilExit};

QString issuerCn(const std::string& dn) {
    const QString s = QString::fromStdString(dn);
    const int at = s.indexOf(QLatin1String("CN="));
    if (at < 0) {
        return s;
    }
    const int end = s.indexOf(QLatin1Char(','), at);
    return s.mid(at + 3, end < 0 ? -1 : end - at - 3).trimmed();
}

}  // namespace identities_widget

IdentitiesWidget::IdentitiesWidget(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("identitiesWidget"));
    const Metrics& m = Theme::sizes();
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(m.grid * 2);
    list_ = new QTreeWidget(this);
    list_->setObjectName(QStringLiteral("identitiesList"));
    list_->setColumnCount(4);
    list_->setRootIsDecorated(false);
    list_->setUniformRowHeights(true);
    list_->header()->setStretchLastSection(false);
    list_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    list_->setMinimumHeight(m.row * 6);
    column->addWidget(list_, 1);
    auto* grid = new QGridLayout();
    grid->setSpacing(m.grid * 2);
    const auto button = [this](const char* name, const char* icon) {
        auto* b = new QPushButton(this);
        b->setObjectName(QLatin1String(name));
        if (icon != nullptr) {
            b->setIcon(Icons::icon(QLatin1String(icon)));
        }
        return b;
    };
    addFile_ = button("identityAddFile", "file-key");
    windows_ = button("identityWindows", "key-round");
    windows_->setEnabled(CertPicker::available());
    create_ = button("identityCreate", "plus");
    default_ = button("identityDefault", "star");
    export_ = button("identityExport", "download");
    password_ = button("identityPassword", "key-round");
    forget_ = button("identityForget", nullptr);
    remove_ = button("identityRemove", "trash-2");
    grid->addWidget(addFile_, 0, 0);
    grid->addWidget(windows_, 0, 1);
    grid->addWidget(create_, 0, 2);
    grid->addWidget(default_, 1, 0);
    grid->addWidget(export_, 1, 1);
    grid->addWidget(remove_, 1, 2);
    column->addLayout(grid);
    auto* row = new QHBoxLayout();
    timeoutLabel_ = new QLabel(this);
    timeout_ = new QComboBox(this);
    timeout_->setObjectName(QStringLiteral("identityTimeout"));
    for (const int t : identities_widget::kTimeouts) {
        timeout_->addItem(QString(), t);
    }
    timeoutLabel_->setBuddy(timeout_);
    row->addWidget(timeoutLabel_);
    row->addWidget(timeout_, 1);
    column->addLayout(row);
    auto* passwordRow = new QHBoxLayout();
    passwordRow->addWidget(password_);
    passwordRow->addWidget(forget_);
    passwordRow->addStretch(1);
    column->addLayout(passwordRow);
    note_ = new QLabel(this);
    note_->setObjectName(QStringLiteral("identityNote"));
    note_->setWordWrap(true);
    note_->hide();
    column->addWidget(note_);

    connect(list_, &QTreeWidget::currentItemChanged, this, &IdentitiesWidget::syncButtons);
    connect(addFile_, &QPushButton::clicked, this, [this] {
        const QString path = chooseFile(true);
        if (path.isEmpty()) {
            return;
        }
        if (const std::optional<QString> pass =
                askPassword(tr("Пароль файла «%1»:").arg(QFileInfo(path).fileName()))) {
            addPfx(path, *pass);
        }
    });
    connect(windows_, &QPushButton::clicked, this, [this] {
        const std::optional<PickedCertificate> picked =
            CertPicker::pick(static_cast<quintptr>(window()->winId()), tr("Сертификат для подписи"),
                             tr("Выберите сертификат с закрытым ключом для цифровой подписи"));
        if (!picked) {
            return;
        }
        try {
            const auto der =
                std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(picked->der.constData()),
                                              static_cast<std::size_t>(picked->der.size()));
            sign::Ossl<X509> cert = sign::loadCertificate(der);
            const sign::IdentityRecord r =
                Identities::shared().with([&](sign::IdentityStore& s) { return s.addWindows(cert.get()); });
            reload(QString::fromStdString(r.id));
        } catch (const sign::IdentityError& e) {
            fail(SignTexts::identityError(e.status()));
        } catch (const std::exception& e) {
            fail(QString::fromUtf8(e.what()));
        }
    });
    connect(create_, &QPushButton::clicked, this, [this] {
        auto* d = new NewIdentityDialog(this);
        d->setAttribute(Qt::WA_DeleteOnClose);
        connect(d, &QDialog::accepted, this, [this, d] {
            if (d->record()) {
                reload(QString::fromStdString(d->record()->id));
                done(tr("Удостоверение «%1» создано.").arg(QString::fromStdString(d->record()->name)));
            }
        });
        d->open();
    });
    connect(default_, &QPushButton::clicked, this, &IdentitiesWidget::setDefaultSelected);
    connect(export_, &QPushButton::clicked, this, [this] {
        const QString path = chooseFile(false);
        if (!path.isEmpty()) {
            exportSelected(path);
        }
    });
    connect(password_, &QPushButton::clicked, this, [this] {
        const std::optional<QString> old = askPassword(tr("Текущий пароль:"));
        if (!old) {
            return;
        }
        const std::optional<QString> next = askPassword(tr("Новый пароль:"));
        if (next) {
            changePassword(*old, *next);
        }
    });
    connect(timeout_, &QComboBox::activated, this,
            [this](int index) { setTimeoutSelected(timeout_->itemData(index).toInt()); });
    connect(forget_, &QPushButton::clicked, this, &IdentitiesWidget::forgetPasswordSelected);
    connect(remove_, &QPushButton::clicked, this, [this] {
        const std::optional<sign::IdentityRecord> r = selected();
        if (!r) {
            return;
        }
        const QString question =
            tr("Удалить удостоверение «%1» из списка? Файл ключа .pfx в папке программы тоже "
               "будет удалён — подписать им больше не получится.")
                .arg(QString::fromStdString(r->name));
        const bool yes =
            confirm_ ? confirm_(question)
                     : QMessageBox::question(this, tr("Удалить удостоверение"), question) == QMessageBox::Yes;
        if (yes) {
            removeSelected(true);
        }
    });
    retranslate();
    reload();
}

void IdentitiesWidget::reload(const QString& select) {
    const QString want = select.isEmpty() ? selectedId() : select;
    try {
        records_ = Identities::shared().with([](sign::IdentityStore& s) { return s.list(); });
    } catch (const std::exception& e) {
        records_.clear();
        fail(QString::fromUtf8(e.what()));
    }
    const QSignalBlocker block(list_);
    list_->clear();
    const int icon = Theme::sizes().iconRow;
    for (const sign::IdentityRecord& r : records_) {
        auto* item = new QTreeWidgetItem(list_);
        item->setText(0, QString::fromStdString(r.name));
        item->setText(1, identities_widget::issuerCn(r.issuer));
        const QDateTime until = QDateTime::fromString(QString::fromStdString(r.notAfter), Qt::ISODate);
        item->setText(2, until.isValid()
                             ? QLocale().toString(until.toLocalTime().date(), QLocale::ShortFormat)
                             : QString());
        item->setText(3,
                      r.kind == sign::IdentityKind::WindowsStore ? tr("Хранилище Windows") : tr("Файл .pfx"));
        item->setData(0, identities_widget::kIdentityIdRole, QString::fromStdString(r.id));
        item->setToolTip(0, QString::fromStdString(r.subject));
        if (r.isDefault) {
            item->setIcon(0, QIcon(Icons::get(QStringLiteral("star"), IconTone::Accent, icon)));
            QFont f = item->font(0);
            f.setBold(true);
            item->setFont(0, f);
        } else {
            item->setIcon(
                0, QIcon(Icons::get(r.kind == sign::IdentityKind::WindowsStore ? QStringLiteral("key-round")
                                                                               : QStringLiteral("file-key"),
                                    IconTone::Muted, icon)));
        }
    }
    for (int i = 0; i < 4; ++i) {
        if (i != 0) {
            list_->resizeColumnToContents(i);
        }
    }
    if (!this->select(want) && list_->topLevelItemCount() > 0) {
        list_->setCurrentItem(list_->topLevelItem(0));
    }
    syncButtons();
}

QString IdentitiesWidget::selectedId() const {
    const QTreeWidgetItem* item = list_->currentItem();
    return item != nullptr ? item->data(0, identities_widget::kIdentityIdRole).toString() : QString();
}

bool IdentitiesWidget::select(const QString& id) {
    for (int i = 0; i < list_->topLevelItemCount(); ++i) {
        if (list_->topLevelItem(i)->data(0, identities_widget::kIdentityIdRole).toString() == id) {
            list_->setCurrentItem(list_->topLevelItem(i));
            return true;
        }
    }
    return false;
}

std::optional<sign::IdentityRecord> IdentitiesWidget::selected() const {
    const QString id = selectedId();
    for (const sign::IdentityRecord& r : records_) {
        if (QString::fromStdString(r.id) == id) {
            return r;
        }
    }
    return std::nullopt;
}

void IdentitiesWidget::syncButtons() {
    const std::optional<sign::IdentityRecord> r = selected();
    const bool any = r.has_value();
    const bool file = any && r->kind == sign::IdentityKind::Pkcs12File;
    default_->setEnabled(any && !r->isDefault);
    export_->setEnabled(any);
    remove_->setEnabled(any);
    password_->setEnabled(file);
    forget_->setEnabled(file && r->rememberPassword);
    timeout_->setEnabled(file);
    if (file) {
        const int index = timeout_->findData(r->passwordTimeout);
        const QSignalBlocker block(timeout_);
        timeout_->setCurrentIndex(index >= 0 ? index : timeout_->count() - 1);
    }
}

void IdentitiesWidget::fail(const QString& text) {
    note_->setText(text);
    SignTexts::setNote(note_, Tone::Danger);
    note_->show();
}

void IdentitiesWidget::done(const QString& text) {
    note_->setText(text);
    SignTexts::setNote(note_, Tone::Ok);
    note_->show();
}

QString IdentitiesWidget::chooseFile(bool open) {
    if (chooser_) {
        return chooser_(this, open);
    }
    if (open) {
        return QFileDialog::getOpenFileName(this, tr("Файл удостоверения"), QString(),
                                            tr("Удостоверения (*.pfx *.p12)"));
    }
    const std::optional<sign::IdentityRecord> r = selected();
    return QFileDialog::getSaveFileName(
        this, tr("Экспорт сертификата"),
        r ? QString::fromStdString(r->name) + QStringLiteral(".cer") : QString(),
        tr("Сертификат (*.cer);;PKCS#7 (*.p7c);;Обмен сертификатами Acrobat (*.fdf)"));
}

std::optional<QString> IdentitiesWidget::askPassword(const QString& prompt) {
    if (prompt_) {
        return prompt_(this, prompt);
    }
    bool ok = false;
    const QString text =
        QInputDialog::getText(this, tr("Пароль удостоверения"), prompt, QLineEdit::Password, QString(), &ok);
    return ok ? std::optional<QString>(text) : std::nullopt;
}

bool IdentitiesWidget::addPfx(const QString& path, const QString& password) {
    try {
        const sign::IdentityRecord r = Identities::shared().with([&](sign::IdentityStore& s) {
            return s.addPkcs12(std::filesystem::path(path.toStdWString()), password.toStdString(), false);
        });
        reload(QString::fromStdString(r.id));
        done(tr("Удостоверение «%1» добавлено.").arg(QString::fromStdString(r.name)));
        return true;
    } catch (const sign::IdentityError& e) {
        fail(SignTexts::identityError(e.status()));
    } catch (const std::exception& e) {
        fail(QString::fromUtf8(e.what()));
    }
    return false;
}

bool IdentitiesWidget::setDefaultSelected() {
    const std::string id = selectedId().toStdString();
    const bool ok =
        !id.empty() && Identities::shared().with([&](sign::IdentityStore& s) { return s.setDefault(id); });
    if (ok) {
        reload();
    }
    return ok;
}

bool IdentitiesWidget::exportSelected(const QString& path) {
    const std::string id = selectedId().toStdString();
    if (id.empty()) {
        return false;
    }
    const QString suffix = QFileInfo(path).suffix().toLower();
    const sign::CertFormat format = suffix == QLatin1String("p7c") || suffix == QLatin1String("p7b")
                                        ? sign::CertFormat::P7c
                                    : suffix == QLatin1String("fdf") ? sign::CertFormat::Fdf
                                                                     : sign::CertFormat::Cer;
    try {
        Identities::shared().with([&](sign::IdentityStore& s) {
            s.exportCertificate(id, std::filesystem::path(path.toStdWString()), format);
        });
        done(tr("Сертификат сохранён: %1. Передайте его тем, кто будет проверять ваши подписи.")
                 .arg(QFileInfo(path).fileName()));
        return true;
    } catch (const std::exception& e) {
        fail(tr("Сертификат не сохранён: %1").arg(QString::fromUtf8(e.what())));
    }
    return false;
}

bool IdentitiesWidget::changePassword(const QString& oldPassword, const QString& newPassword) {
    const std::string id = selectedId().toStdString();
    if (id.empty()) {
        return false;
    }
    if (newPassword.isEmpty()) {
        fail(tr("Новый пароль не может быть пустым."));
        return false;
    }
    try {
        Identities::shared().with([&](sign::IdentityStore& s) {
            s.changePassword(id, oldPassword.toStdString(), newPassword.toStdString());
        });
        reload();
        done(tr("Пароль удостоверения изменён."));
        return true;
    } catch (const sign::IdentityError& e) {
        fail(SignTexts::identityError(e.status()));
    } catch (const std::exception& e) {
        fail(QString::fromUtf8(e.what()));
    }
    return false;
}

bool IdentitiesWidget::setTimeoutSelected(int seconds) {
    const std::string id = selectedId().toStdString();
    if (id.empty()) {
        return false;
    }
    try {
        Identities::shared().with([&](sign::IdentityStore& s) { s.setPasswordTimeout(id, seconds); });
        reload();
        return true;
    } catch (const std::exception& e) {
        fail(QString::fromUtf8(e.what()));
    }
    return false;
}

bool IdentitiesWidget::forgetPasswordSelected() {
    const std::string id = selectedId().toStdString();
    if (id.empty()) {
        return false;
    }
    try {
        Identities::shared().with([&](sign::IdentityStore& s) {
            s.setRememberedPassword(id, std::nullopt);
            s.forgetCachedPasswords();
        });
        reload();
        done(tr("Запомненный пароль удалён: при подписи он будет запрошен."));
        return true;
    } catch (const std::exception& e) {
        fail(QString::fromUtf8(e.what()));
    }
    return false;
}

bool IdentitiesWidget::removeSelected(bool deleteFile) {
    const std::string id = selectedId().toStdString();
    if (id.empty()) {
        return false;
    }
    const bool ok =
        Identities::shared().with([&](sign::IdentityStore& s) { return s.remove(id, deleteFile); });
    reload();
    return ok;
}

void IdentitiesWidget::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QWidget::changeEvent(event);
}

void IdentitiesWidget::retranslate() {
    list_->setHeaderLabels({tr("Удостоверение"), tr("Кем выдано"), tr("Действует до"), tr("Вид")});
    list_->setAccessibleName(tr("Удостоверения"));
    addFile_->setText(tr("Добавить &файл…"));
    windows_->setText(tr("Выбрать в &Windows…"));
    windows_->setToolTip(CertPicker::available() ? tr("Сертификат из хранилища Windows «Личное»")
                                                 : tr("Доступно только в окне программы на экране"));
    create_->setText(tr("Со&здать…"));
    default_->setText(tr("По &умолчанию"));
    export_->setText(tr("&Экспорт…"));
    export_->setToolTip(
        tr("Сохранить сертификат (.cer, .p7c, .fdf), чтобы другие добавили его в доверенные"));
    password_->setText(tr("Сменить &пароль…"));
    forget_->setText(tr("Забыть пароль"));
    remove_->setText(tr("&Удалить"));
    timeoutLabel_->setText(tr("Спрашивать пароль:"));
    const QStringList timeouts = {tr("каждый раз"),     tr("через 1 минуту"), tr("через 10 минут"),
                                  tr("через 30 минут"), tr("через 2 часа"),   tr("один раз до выхода")};
    for (int i = 0; i < timeout_->count(); ++i) {
        timeout_->setItemText(i, timeouts.value(i));
    }
}

}  // namespace ot::ui
