#include "ui/shell/preferences/tsa_widget.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <exception>

#include "services/http_fetcher.h"
#include "services/settings.h"
#include "services/tsa_settings.h"
#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/tools/certificate/sign_texts.h"
#include "ui/widgets/property_sheet.h"
#include "ui/widgets/tone.h"

namespace ot::ui {

namespace tsa_widget {

constexpr int kServerNameRole = Qt::UserRole + 1;

// «Сервер времени»: имя, адрес, логин и пароль.
class ServerDialog : public QDialog {
public:
    ServerDialog(const sign::TsaServer& initial, QWidget* parent) : QDialog(parent) {
        setObjectName(QStringLiteral("tsaServerDialog"));
        setWindowTitle(TsaWidget::tr("Сервер времени"));
        const Metrics& m = Theme::sizes();
        auto* column = new QVBoxLayout(this);
        column->setContentsMargins(m.grid * 6, m.grid * 6, m.grid * 6, m.grid * 4);
        auto* sheet = new PropertySheet(this);
        name_ = new QLineEdit(QString::fromStdString(initial.name));
        url_ = new QLineEdit(QString::fromStdString(initial.url));
        url_->setPlaceholderText(QStringLiteral("http://timestamp.example.com"));
        user_ = new QLineEdit(QString::fromStdString(initial.user));
        password_ = new QLineEdit(QString::fromStdString(initial.password));
        password_->setEchoMode(QLineEdit::Password);
        sheet->addRow(TsaWidget::tr("&Имя:"), name_);
        sheet->addRow(TsaWidget::tr("&Адрес:"), url_);
        sheet->addRow(TsaWidget::tr("&Логин:"), user_);
        sheet->addRow(TsaWidget::tr("&Пароль:"), password_);
        column->addWidget(sheet);
        auto* note = new QLabel(TsaWidget::tr("Пароль хранится только до выхода из программы."), this);
        note->setWordWrap(true);
        column->addWidget(note);
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        buttons->button(QDialogButtonBox::Ok)->setText(TsaWidget::tr("ОК"));
        buttons->button(QDialogButtonBox::Cancel)->setText(TsaWidget::tr("Отмена"));
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        column->addWidget(buttons);
        setMinimumWidth(m.dialogWidth / 2);
    }
    sign::TsaServer server() const {
        sign::TsaServer s;
        s.name = name_->text().trimmed().toStdString();
        s.url = url_->text().trimmed().toStdString();
        s.user = user_->text().trimmed().toStdString();
        s.password = password_->text().toStdString();
        return s;
    }

private:
    QLineEdit* name_ = nullptr;
    QLineEdit* url_ = nullptr;
    QLineEdit* user_ = nullptr;
    QLineEdit* password_ = nullptr;
};

bool validUrl(const std::string& url) {
    const QUrl u(QString::fromStdString(url));
    return u.isValid() && (u.scheme() == QLatin1String("http") || u.scheme() == QLatin1String("https")) &&
           !u.host().isEmpty();
}

}  // namespace tsa_widget

TsaWidget::TsaWidget(QWidget* parent) : QWidget(parent), servers_(sign::TsaServers::defaults()) {
    setObjectName(QStringLiteral("tsaWidget"));
    const Metrics& m = Theme::sizes();
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(m.grid * 2);
    list_ = new QTreeWidget(this);
    list_->setObjectName(QStringLiteral("tsaList"));
    list_->setColumnCount(2);
    list_->setRootIsDecorated(false);
    list_->setUniformRowHeights(true);
    list_->header()->setStretchLastSection(true);
    list_->setMinimumHeight(m.row * 5);
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
    add_ = button("tsaAdd", "plus");
    edit_ = button("tsaEdit", nullptr);
    remove_ = button("tsaRemove", "trash-2");
    default_ = button("tsaDefault", "star");
    check_ = button("tsaCheck", "clock-check");
    grid->addWidget(add_, 0, 0);
    grid->addWidget(edit_, 0, 1);
    grid->addWidget(remove_, 0, 2);
    grid->addWidget(default_, 1, 0);
    grid->addWidget(check_, 1, 1);
    column->addLayout(grid);
    checkLabel_ = new QLabel(this);
    checkLabel_->setObjectName(QStringLiteral("tsaCheckResult"));
    checkLabel_->setWordWrap(true);
    checkLabel_->hide();
    column->addWidget(checkLabel_);
    timestamp_ = new QCheckBox(this);
    timestamp_->setObjectName(QStringLiteral("tsaTimestamp"));
    column->addWidget(timestamp_);
    auto* row = new QHBoxLayout();
    fallbackLabel_ = new QLabel(this);
    fallback_ = new QComboBox(this);
    fallback_->setObjectName(QStringLiteral("tsaFallback"));
    for (const sign::TimestampFallback f :
         {sign::TimestampFallback::Ask, sign::TimestampFallback::WithoutTimestamp,
          sign::TimestampFallback::Cancel}) {
        fallback_->addItem(QString(), QLatin1String(sign::fallbackName(f)));
    }
    fallback_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    fallback_->setMinimumContentsLength(16);
    fallbackLabel_->setBuddy(fallback_);
    row->addWidget(fallbackLabel_);
    row->addWidget(fallback_, 1);
    column->addLayout(row);

    connect(list_, &QTreeWidget::currentItemChanged, this, &TsaWidget::syncButtons);
    connect(list_, &QTreeWidget::itemDoubleClicked, this, [this] { edit_->click(); });
    connect(add_, &QPushButton::clicked, this, [this] {
        if (const std::optional<sign::TsaServer> s = ask({})) {
            addServer(*s);
        }
    });
    connect(edit_, &QPushButton::clicked, this, [this] {
        const sign::TsaServer* current = servers_.find(selectedName().toStdString());
        if (current == nullptr) {
            return;
        }
        if (const std::optional<sign::TsaServer> s = ask(*current)) {
            updateSelected(*s);
        }
    });
    connect(remove_, &QPushButton::clicked, this, &TsaWidget::removeSelected);
    connect(default_, &QPushButton::clicked, this, &TsaWidget::setDefaultSelected);
    connect(check_, &QPushButton::clicked, this, &TsaWidget::checkSelected);
    retranslate();
    fill();
}

void TsaWidget::load(const Settings& settings) {
    servers_ = loadTsaServers(settings);
    timestamp_->setChecked(settings.get(settings::kTimestampSignatures));
    const int index = fallback_->findData(settings.get(settings::kTimestampFallback));
    fallback_->setCurrentIndex(index >= 0 ? index : 0);
    fill();
}

void TsaWidget::apply(Settings& settings) {
    saveTsaServers(settings, servers_);
    settings.set(settings::kTimestampSignatures, timestamp_->isChecked());
    settings.set(settings::kTimestampFallback, fallback_->currentData().toString());
}

void TsaWidget::fill(const QString& select) {
    const QString want = select.isEmpty() ? selectedName() : select;
    const QSignalBlocker block(list_);
    list_->clear();
    const int icon = Theme::sizes().iconRow;
    for (const sign::TsaServer& s : servers_.servers()) {
        auto* item = new QTreeWidgetItem(list_);
        const QString name = QString::fromStdString(s.name);
        item->setText(0, name);
        item->setText(1, QString::fromStdString(s.url));
        item->setData(0, tsa_widget::kServerNameRole, name);
        if (s.name == servers_.defaultName()) {
            item->setIcon(0, QIcon(Icons::get(QStringLiteral("star"), IconTone::Accent, icon)));
            QFont f = item->font(0);
            f.setBold(true);
            item->setFont(0, f);
            item->setToolTip(0, tr("Служба по умолчанию"));
        } else {
            item->setIcon(0, QIcon(Icons::get(QStringLiteral("clock"), IconTone::Muted, icon)));
        }
    }
    list_->resizeColumnToContents(0);
    if (!this->select(want) && list_->topLevelItemCount() > 0) {
        list_->setCurrentItem(list_->topLevelItem(0));
    }
    syncButtons();
}

bool TsaWidget::select(const QString& name) {
    for (int i = 0; i < list_->topLevelItemCount(); ++i) {
        if (list_->topLevelItem(i)->data(0, tsa_widget::kServerNameRole).toString() == name) {
            list_->setCurrentItem(list_->topLevelItem(i));
            return true;
        }
    }
    return false;
}

QString TsaWidget::selectedName() const {
    const QTreeWidgetItem* item = list_->currentItem();
    return item != nullptr ? item->data(0, tsa_widget::kServerNameRole).toString() : QString();
}

void TsaWidget::syncButtons() {
    const QString name = selectedName();
    const bool any = !name.isEmpty();
    edit_->setEnabled(any);
    remove_->setEnabled(any);
    default_->setEnabled(any && name.toStdString() != servers_.defaultName());
    check_->setEnabled(any && !checking_);
}

std::optional<sign::TsaServer> TsaWidget::ask(const sign::TsaServer& initial) {
    if (prompt_) {
        return prompt_(this, initial);
    }
    tsa_widget::ServerDialog dialog(initial, this);
    if (dialog.exec() != QDialog::Accepted) {
        return std::nullopt;
    }
    return dialog.server();
}

bool TsaWidget::addServer(const sign::TsaServer& server) {
    if (!tsa_widget::validUrl(server.url) || !servers_.add(server)) {
        checkLabel_->setText(tr("Сервер не добавлен: нужно своё имя и адрес вида http://… или https://…"));
        SignTexts::setNote(checkLabel_, Tone::Danger);
        checkLabel_->show();
        return false;
    }
    fill(QString::fromStdString(server.name));
    return true;
}

bool TsaWidget::updateSelected(const sign::TsaServer& server) {
    if (!tsa_widget::validUrl(server.url) || !servers_.update(selectedName().toStdString(), server)) {
        checkLabel_->setText(tr("Сервер не изменён: имя занято или адрес неверен."));
        SignTexts::setNote(checkLabel_, Tone::Danger);
        checkLabel_->show();
        return false;
    }
    fill(QString::fromStdString(server.name));
    return true;
}

bool TsaWidget::removeSelected() {
    const bool ok = servers_.remove(selectedName().toStdString());
    fill();
    return ok;
}

bool TsaWidget::setDefaultSelected() {
    const bool ok = servers_.setDefault(selectedName().toStdString());
    fill();
    return ok;
}

void TsaWidget::checkSelected() {
    const sign::TsaServer* current = servers_.find(selectedName().toStdString());
    if (current == nullptr || checking_) {
        return;
    }
    checking_ = true;
    syncButtons();
    checkLabel_->setText(tr("Запрашиваю тестовую метку у «%1»…").arg(QString::fromStdString(current->name)));
    SignTexts::setNote(checkLabel_, Tone::Info);
    checkLabel_->show();
    const sign::TsaServer server = *current;
    QtConcurrent::run([server] {
        return sign::checkTsaServer(httpFetcher(), server);
    }).then(this, [this, server](QFuture<sign::TsaCheck> f) {
        checking_ = false;
        sign::TsaCheck c;
        try {
            c = f.result();
        } catch (const std::exception& e) {
            c.message = e.what();
        }
        QString message;
        if (c.ok) {
            message = tr("«%1» ответил за %2 мс: метка времени %3.")
                          .arg(QString::fromStdString(server.name))
                          .arg(c.ms)
                          .arg(SignTexts::dateTime(c.info.genTime));
            SignTexts::setNote(checkLabel_, Tone::Ok);
        } else {
            message = tr("«%1» не выдал метку: %2.")
                          .arg(QString::fromStdString(server.name), SignTexts::tsaStatus(c.status));
            SignTexts::setNote(checkLabel_, Tone::Danger);
        }
        checkLabel_->setText(message);
        syncButtons();
        emit checkFinished(c.ok, message);
    });
}

void TsaWidget::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    QWidget::changeEvent(event);
}

void TsaWidget::retranslate() {
    list_->setHeaderLabels({tr("Служба времени"), tr("Адрес")});
    list_->setAccessibleName(tr("Серверы времени"));
    add_->setText(tr("&Добавить…"));
    edit_->setText(tr("&Изменить…"));
    remove_->setText(tr("&Удалить"));
    default_->setText(tr("По &умолчанию"));
    check_->setText(tr("&Проверить"));
    timestamp_->setText(tr("Ставить &метку времени при подписи (уровень B-T)"));
    fallbackLabel_->setText(tr("Если служба не &ответила:"));
    const QStringList fallbacks = {tr("спросить, подписать ли без метки"), tr("подписать без метки"),
                                   tr("не подписывать")};
    for (int i = 0; i < fallback_->count(); ++i) {
        fallback_->setItemText(i, fallbacks.value(i));
    }
}

}  // namespace ot::ui
