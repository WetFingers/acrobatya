#include "ui/shell/preferences/trusted_widget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <algorithm>
#include <exception>
#include <memory>

#include "core/sign/identity.h"
#include "services/http_fetcher.h"
#include "services/settings.h"
#include "services/trust_service.h"
#include "services/trust_updater.h"
#include "ui/dialogs/trust_dialog.h"
#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/tools/certificate/sign_texts.h"
#include "ui/widgets/tone.h"

namespace ot::ui {

namespace trusted_widget {

constexpr int kAnchorRole = Qt::UserRole + 1;

bool writeBytes(const QString& path, const sign::Bytes& bytes) {
    QFile f(path);
    if (bytes.empty() || !f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    return f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<qint64>(bytes.size())) ==
           static_cast<qint64>(bytes.size());
}

sign::CertFormat formatOf(const QString& path) {
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("p7c") || suffix == QLatin1String("p7b")) {
        return sign::CertFormat::P7c;
    }
    return suffix == QLatin1String("fdf") ? sign::CertFormat::Fdf : sign::CertFormat::Cer;
}

// Загрузка списков доверия в фоне: настройки и обновитель живут, пока идёт работа (TrustUpdater держит
// Settings ссылкой).
struct UpdateJob {
    explicit UpdateJob(const QString& dir) : updater(settings, httpFetcher(), dir) {}
    Settings settings;
    TrustUpdater updater;
};

}  // namespace trusted_widget

TrustedWidget::TrustedWidget(QWidget* parent) : QWidget(parent) {
    setObjectName(QStringLiteral("trustedWidget"));
    const Metrics& m = Theme::sizes();
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(m.grid * 2);
    auto* filterRow = new QHBoxLayout();
    filterLabel_ = new QLabel(this);
    filter_ = new QComboBox(this);
    filter_->setObjectName(QStringLiteral("trustedFilter"));
    filter_->addItem(QString(), -1);
    for (const sign::TrustSource s : {sign::TrustSource::User, sign::TrustSource::Eutl,
                                      sign::TrustSource::Aatl, sign::TrustSource::Windows}) {
        filter_->addItem(QString(), static_cast<int>(s));
    }
    filterLabel_->setBuddy(filter_);
    filterRow->addWidget(filterLabel_);
    filterRow->addWidget(filter_, 1);
    column->addLayout(filterRow);
    list_ = new QTreeWidget(this);
    list_->setObjectName(QStringLiteral("trustedList"));
    list_->setColumnCount(4);
    list_->setRootIsDecorated(false);
    list_->setUniformRowHeights(true);
    list_->header()->setStretchLastSection(false);
    list_->header()->setSectionResizeMode(QHeaderView::Interactive);
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
    import_ = button("trustedImport", "upload");
    export_ = button("trustedExport", "download");
    exportAll_ = button("trustedExportAll", nullptr);
    edit_ = button("trustedEdit", "shield-check");
    remove_ = button("trustedRemove", "trash-2");
    windowsImport_ = button("trustedWindowsImport", "key-round");
    grid->addWidget(import_, 0, 0);
    grid->addWidget(export_, 0, 1);
    grid->addWidget(exportAll_, 0, 2);
    grid->addWidget(edit_, 1, 0);
    grid->addWidget(remove_, 1, 1);
    grid->addWidget(windowsImport_, 1, 2);
    column->addLayout(grid);
    windowsCaption_ = new QLabel(this);
    windowsCaption_->setProperty("caption", true);
    column->addWidget(windowsCaption_);
    winSignatures_ = new QCheckBox(this);
    winSignatures_->setObjectName(QStringLiteral("trustedWindowsSignatures"));
    winCertified_ = new QCheckBox(this);
    winCertified_->setObjectName(QStringLiteral("trustedWindowsCertified"));
    column->addWidget(winSignatures_);
    column->addWidget(winCertified_);
    listsCaption_ = new QLabel(this);
    listsCaption_->setProperty("caption", true);
    column->addWidget(listsCaption_);
    eutl_ = new QCheckBox(this);
    eutl_->setObjectName(QStringLiteral("trustedEutl"));
    aatl_ = new QCheckBox(this);
    aatl_->setObjectName(QStringLiteral("trustedAatl"));
    ask_ = new QCheckBox(this);
    ask_->setObjectName(QStringLiteral("trustedAsk"));
    column->addWidget(eutl_);
    column->addWidget(aatl_);
    column->addWidget(ask_);
    auto* updateRow = new QHBoxLayout();
    update_ = button("trustedUpdate", "refresh-cw");
    lists_ = new QLabel(this);
    lists_->setObjectName(QStringLiteral("trustedLists"));
    lists_->setWordWrap(true);
    updateRow->addWidget(update_);
    updateRow->addWidget(lists_, 1);
    column->addLayout(updateRow);
    note_ = new QLabel(this);
    note_->setObjectName(QStringLiteral("trustedNote"));
    note_->setWordWrap(true);
    note_->hide();
    column->addWidget(note_);

    connect(filter_, &QComboBox::currentIndexChanged, this, &TrustedWidget::reload);
    connect(list_, &QTreeWidget::currentItemChanged, this, &TrustedWidget::syncButtons);
    connect(list_, &QTreeWidget::itemDoubleClicked, this, &TrustedWidget::editSelected);
    connect(import_, &QPushButton::clicked, this, [this] {
        const QString path = chooseFile(true);
        if (!path.isEmpty()) {
            importFile(path);
        }
    });
    connect(export_, &QPushButton::clicked, this, [this] {
        const QString path = chooseFile(false, QStringLiteral("certificate.cer"));
        if (!path.isEmpty()) {
            exportSelected(path);
        }
    });
    connect(exportAll_, &QPushButton::clicked, this, [this] {
        const QString path = chooseFile(false, QStringLiteral("trusted.p7c"));
        if (!path.isEmpty()) {
            exportAll(path);
        }
    });
    connect(edit_, &QPushButton::clicked, this, &TrustedWidget::editSelected);
    connect(remove_, &QPushButton::clicked, this, &TrustedWidget::removeSelected);
    connect(windowsImport_, &QPushButton::clicked, this, [this] { importWindows(true, true); });
    connect(winSignatures_, &QCheckBox::toggled, this, &TrustedWidget::setWindowsRoots);
    connect(winCertified_, &QCheckBox::toggled, this, &TrustedWidget::setWindowsRoots);
    connect(update_, &QPushButton::clicked, this, &TrustedWidget::updateNow);
    connect(&TrustService::shared(), &TrustService::changed, this, &TrustedWidget::reload,
            Qt::QueuedConnection);
    retranslate();
    reload();
}

void TrustedWidget::load(const Settings& settings) {
    eutl_->setChecked(settings.get(settings::kTrustEutl));
    aatl_->setChecked(settings.get(settings::kTrustAatl));
    ask_->setChecked(settings.get(settings::kTrustListsAsk));
    reload();
}

void TrustedWidget::apply(Settings& settings) {
    settings.set(settings::kTrustEutl, eutl_->isChecked());
    settings.set(settings::kTrustAatl, aatl_->isChecked());
    settings.set(settings::kTrustListsAsk, ask_->isChecked());
}

void TrustedWidget::reload() {
    const QString want = selectedThumbprint();
    const std::shared_ptr<const sign::TrustStore> store = TrustService::shared().snapshot();
    const int source = filter_->currentData().toInt();
    const QSignalBlocker block(list_);
    list_->clear();
    const int icon = Theme::sizes().iconRow;
    int user = 0;
    int eutl = 0;
    int aatl = 0;
    for (const sign::TrustAnchor& a : store->anchors()) {
        user += a.source == sign::TrustSource::User ? 1 : 0;
        eutl += a.source == sign::TrustSource::Eutl ? 1 : 0;
        aatl += a.source == sign::TrustSource::Aatl ? 1 : 0;
        if (source >= 0 && static_cast<int>(a.source) != source) {
            continue;
        }
        auto* item = new QTreeWidgetItem(list_);
        const QString name = !a.label.empty()             ? QString::fromStdString(a.label)
                             : !a.cert.commonName.empty() ? QString::fromStdString(a.cert.commonName)
                                                          : QString::fromStdString(a.cert.subject);
        item->setText(0, name);
        item->setToolTip(0, QString::fromStdString(a.cert.subject));
        item->setIcon(0, QIcon(Icons::get(a.cert.ca ? QStringLiteral("award") : QStringLiteral("id-card"),
                                          IconTone::Muted, icon)));
        item->setText(1, SignTexts::trustSource(a.source));
        item->setText(2, SignTexts::trustFlags(a.flags));
        const QDateTime until = QDateTime::fromString(QString::fromStdString(a.cert.notAfter), Qt::ISODate);
        item->setText(3, until.isValid()
                             ? QLocale().toString(until.toLocalTime().date(), QLocale::ShortFormat)
                             : QString());
        item->setData(0, trusted_widget::kAnchorRole, QString::fromStdString(a.sha256));
    }
    // Имя — половина ширины, остальное — по содержимому (список прокручивается сам, страницу не раздвигает).
    list_->setColumnWidth(0, std::max(Theme::sizes().rightPanel, list_->viewport()->width() / 2));
    list_->resizeColumnToContents(1);
    list_->resizeColumnToContents(2);
    list_->resizeColumnToContents(3);
    if (!select(want) && list_->topLevelItemCount() > 0) {
        list_->setCurrentItem(list_->topLevelItem(0));
    }
    {
        const QSignalBlocker a(winSignatures_);
        const QSignalBlocker b(winCertified_);
        winSignatures_->setChecked((store->windowsRootsFlags() & sign::kTrustSignatures) != 0);
        winCertified_->setChecked((store->windowsRootsFlags() & sign::kTrustCertification) != 0);
    }
    lists_->setProperty("counts", QStringLiteral("%1/%2/%3").arg(user).arg(eutl).arg(aatl));
    showListsState();
    syncButtons();
}

void TrustedWidget::showListsState() {
    const QStringList counts = lists_->property("counts").toString().split(QLatin1Char('/'));
    const QString checked = Settings().get(settings::kTrustListsCheckedAt);
    const QDateTime at = QDateTime::fromString(checked, Qt::ISODate);
    QString text =
        tr("Своих: %1 · список ЕС: %2 · Adobe: %3.").arg(counts.value(0), counts.value(1), counts.value(2));
    text += QLatin1Char(' ');
    text += at.isValid()
                ? tr("Списки проверялись %1.").arg(QLocale().toString(at.toLocalTime(), QLocale::ShortFormat))
                : tr("Списки ещё не загружались.");
    if (!updating_) {
        lists_->setText(text);
    }
}

QString TrustedWidget::selectedThumbprint() const {
    const QTreeWidgetItem* item = list_->currentItem();
    return item != nullptr ? item->data(0, trusted_widget::kAnchorRole).toString() : QString();
}

bool TrustedWidget::select(const QString& thumbprint) {
    if (thumbprint.isEmpty()) {
        return false;
    }
    for (int i = 0; i < list_->topLevelItemCount(); ++i) {
        if (list_->topLevelItem(i)->data(0, trusted_widget::kAnchorRole).toString() == thumbprint) {
            list_->setCurrentItem(list_->topLevelItem(i));
            return true;
        }
    }
    return false;
}

void TrustedWidget::syncButtons() {
    const bool any = !selectedThumbprint().isEmpty();
    export_->setEnabled(any);
    edit_->setEnabled(any);
    remove_->setEnabled(any);
    exportAll_->setEnabled(!TrustService::shared().snapshot()->anchors().empty());
    update_->setEnabled(!updating_);
}

void TrustedWidget::fail(const QString& text) {
    note_->setText(text);
    SignTexts::setNote(note_, Tone::Danger);
    note_->show();
}

void TrustedWidget::done(const QString& text) {
    note_->setText(text);
    SignTexts::setNote(note_, Tone::Ok);
    note_->show();
}

QString TrustedWidget::chooseFile(bool open, const QString& suggested) {
    if (chooser_) {
        return chooser_(this, open);
    }
    if (open) {
        return QFileDialog::getOpenFileName(
            this, tr("Импорт сертификатов"), QString(),
            tr("Сертификаты (*.cer *.crt *.pem *.der *.p7b *.p7c *.fdf);;Все файлы (*)"));
    }
    return QFileDialog::getSaveFileName(
        this, tr("Экспорт сертификатов"), suggested,
        tr("Сертификат (*.cer);;PKCS#7 (*.p7c);;Обмен сертификатами Acrobat (*.fdf)"));
}

bool TrustedWidget::confirm(const QString& question) {
    if (confirm_) {
        return confirm_(question);
    }
    return QMessageBox::question(this, tr("Доверенные сертификаты"), question) == QMessageBox::Yes;
}

int TrustedWidget::importFile(const QString& path) {
    try {
        const int n = TrustService::shared().edit([&](sign::TrustStore& s) {
            return s.importFile(std::filesystem::path(path.toStdWString()), sign::kTrustSignatures);
        });
        reload();
        if (n > 0) {
            done(tr("Добавлено доверенных сертификатов: %1.").arg(n));
        } else {
            done(tr("Новых сертификатов в «%1» нет.").arg(QFileInfo(path).fileName()));
        }
        return n;
    } catch (const std::exception& e) {
        fail(tr("«%1» не импортирован: %2").arg(QFileInfo(path).fileName(), QString::fromUtf8(e.what())));
    }
    return 0;
}

bool TrustedWidget::exportSelected(const QString& path) {
    const std::string sha = selectedThumbprint().toStdString();
    const sign::Bytes bytes =
        TrustService::shared().snapshot()->exportAnchor(sha, trusted_widget::formatOf(path));
    if (!trusted_widget::writeBytes(path, bytes)) {
        fail(tr("Сертификат не сохранён: %1").arg(QFileInfo(path).fileName()));
        return false;
    }
    done(tr("Сертификат сохранён: %1.").arg(QFileInfo(path).fileName()));
    return true;
}

bool TrustedWidget::exportAll(const QString& path) {
    const sign::Bytes bytes = TrustService::shared().snapshot()->exportAll();
    if (!trusted_widget::writeBytes(path, bytes)) {
        fail(tr("Список не сохранён: %1").arg(QFileInfo(path).fileName()));
        return false;
    }
    done(tr("Все доверенные сертификаты сохранены: %1.").arg(QFileInfo(path).fileName()));
    return true;
}

bool TrustedWidget::editSelected() {
    const std::string sha = selectedThumbprint().toStdString();
    const std::shared_ptr<const sign::TrustStore> store = TrustService::shared().snapshot();
    const sign::TrustAnchor* a = store->find(sha);
    if (a == nullptr) {
        return false;
    }
    TrustDialog dialog(a->cert, a->flags, true, this);
    std::optional<unsigned> flags;
    if (trustPrompt_) {
        flags = trustPrompt_(dialog);
    } else if (dialog.exec() == QDialog::Accepted) {
        flags = dialog.flags();
    }
    if (!flags) {
        return false;
    }
    const unsigned value = *flags;
    TrustService::shared().edit([&](sign::TrustStore& s) { s.setFlags(sha, value); });
    reload();
    return true;
}

bool TrustedWidget::removeSelected() {
    const std::string sha = selectedThumbprint().toStdString();
    const QTreeWidgetItem* item = list_->currentItem();
    if (sha.empty() || item == nullptr) {
        return false;
    }
    if (!confirm(
            tr("Убрать «%1» из доверенных? Подписи, которые доверены только через него, станут «подписант не "
               "доверен».")
                .arg(item->text(0)))) {
        return false;
    }
    const bool ok = TrustService::shared().edit([&](sign::TrustStore& s) { return s.remove(sha); });
    reload();
    return ok;
}

int TrustedWidget::importWindows(bool root, bool ca) {
    if (!confirm(
            tr("Добавить в доверенные корневые и промежуточные центры сертификации из хранилища Windows? "
               "Хранилище только читается; доверие — для подписей."))) {
        return 0;
    }
    try {
        const int n = TrustService::shared().edit(
            [&](sign::TrustStore& s) { return s.importWindows(root, ca, sign::kTrustSignatures); });
        reload();
        done(tr("Из хранилища Windows добавлено сертификатов: %1.").arg(n));
        return n;
    } catch (const std::exception& e) {
        fail(QString::fromUtf8(e.what()));
    }
    return 0;
}

void TrustedWidget::setWindowsRoots() {
    unsigned flags = 0;
    flags |= winSignatures_->isChecked() ? sign::kTrustSignatures : 0u;
    flags |= winCertified_->isChecked() ? sign::kTrustCertification : 0u;
    TrustService::shared().edit([flags](sign::TrustStore& s) { s.setWindowsRootsFlags(flags); });
}

void TrustedWidget::updateNow() {
    if (updating_) {
        return;
    }
    // Флажки списков — те, что сейчас в окне (ещё до «ОК»).
    {
        Settings settings;
        apply(settings);
        settings.sync();
    }
    updating_ = true;
    syncButtons();
    lists_->setText(tr("Загрузка списков доверия…"));
    auto job = std::make_shared<trusted_widget::UpdateJob>(TrustService::shared().dir());
    QtConcurrent::run([job] {
        return job->updater.check(true);
    }).then(this, [this, job](QFuture<TrustUpdater::Outcome> f) {
        updating_ = false;
        TrustUpdater::Outcome outcome = TrustUpdater::Outcome::Failed;
        try {
            outcome = f.result();
        } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): сбой — как «не загрузилось»
        }
        QString message;
        bool ok = true;
        switch (outcome) {
            case TrustUpdater::Outcome::Disabled:
                message = tr("Списки доверия выключены: отметьте список ЕС или Adobe.");
                ok = false;
                break;
            case TrustUpdater::Outcome::Failed:
                message = tr("Списки доверия не загрузились: нет сети или сервер недоступен.");
                ok = false;
                break;
            case TrustUpdater::Outcome::Pending: {
                const TrustListUpdate& u = job->updater.pending();
                QString question =
                    tr("Загружены списки доверия: ЕС — %1 сертификатов, Adobe — %2. Установить?")
                        .arg(u.eutlCount())
                        .arg(u.aatlCount());
                if (u.needsConsent()) {
                    question +=
                        QLatin1Char('\n') +
                        tr("Подпись списка ЕС не удалось подтвердить полностью — устанавливайте, только "
                           "если доверяете источнику.");
                }
                if (confirm(question)) {
                    const int n = job->updater.install();
                    TrustService::shared().reload();
                    message = tr("Списки доверия установлены: якорей %1.").arg(n);
                } else {
                    job->updater.discard();
                    message = tr("Списки доверия не установлены.");
                }
                break;
            }
            case TrustUpdater::Outcome::Installed:
                TrustService::shared().reload();
                message = tr("Списки доверия обновлены.");
                break;
            case TrustUpdater::Outcome::NotDue:
                message = tr("Списки доверия свежие.");
                break;
        }
        reload();
        if (ok) {
            done(message);
        } else {
            fail(message);
        }
        emit updateFinished(ok, message);
    });
}

void TrustedWidget::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QWidget::changeEvent(event);
}

void TrustedWidget::retranslate() {
    filterLabel_->setText(tr("&Показать:"));
    const QStringList filters = {tr("все"), tr("добавленные вручную"), tr("список доверия ЕС"),
                                 tr("список Adobe (AATL)"), tr("хранилище Windows")};
    for (int i = 0; i < filter_->count(); ++i) {
        filter_->setItemText(i, filters.value(i));
    }
    list_->setHeaderLabels({tr("Сертификат"), tr("Источник"), tr("Доверять для"), tr("Действует до")});
    list_->setAccessibleName(tr("Доверенные сертификаты"));
    import_->setText(tr("&Импорт…"));
    export_->setText(tr("&Экспорт…"));
    exportAll_->setText(tr("Экспорт &всех…"));
    edit_->setText(tr("Изменить &доверие…"));
    remove_->setText(tr("&Удалить"));
    windowsImport_->setText(tr("Импорт из &Windows…"));
    windowsCaption_->setText(tr("Корневые сертификаты Windows"));
    winSignatures_->setText(tr("Доверять корневым сертификатам Windows для &подписей"));
    winCertified_->setText(tr("…и для &сертифицированных документов"));
    listsCaption_->setText(tr("Списки доверия"));
    eutl_->setText(tr("Загружать список доверия &ЕС (EUTL)"));
    aatl_->setText(tr("Загружать список Adobe (&AATL)"));
    ask_->setText(tr("&Спрашивать перед установкой"));
    update_->setText(tr("&Обновить сейчас"));
    showListsState();
}

}  // namespace ot::ui
