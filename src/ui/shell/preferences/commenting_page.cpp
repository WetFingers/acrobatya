#include "ui/shell/preferences/commenting_page.h"

#include <QCheckBox>
#include <QEvent>
#include <QFontComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>

#include "core/annot/defaults.h"
#include "services/settings.h"
#include "services/settings_keys.h"
#include "ui/widgets/property_sheet.h"

namespace ot::ui {

CommentingPage::CommentingPage(QWidget* parent) : PreferencesPage(parent) {
    setObjectName(QStringLiteral("commentingPage"));
    auto* column = new QVBoxLayout(this);
    column->setContentsMargins(0, 0, 0, 0);
    sheet_ = new PropertySheet(this);
    column->addWidget(sheet_);
    column->addStretch(1);

    font_ = new QFontComboBox();
    font_->setObjectName(QStringLiteral("commentPopupFontBox"));
    size_ = new QSpinBox();
    size_->setObjectName(QStringLiteral("commentPopupSizeBox"));
    size_->setRange(settings::kCommentPopupFontSize.min, settings::kCommentPopupFontSize.max);
    hover_ = new QCheckBox();
    hover_->setObjectName(QStringLiteral("commentHoverBox"));
    connector_ = new QCheckBox();
    connector_->setObjectName(QStringLiteral("commentConnectorBox"));
    openOnCreate_ = new QCheckBox();
    openOnCreate_->setObjectName(QStringLiteral("commentOpenOnCreateBox"));
    loginName_ = new QCheckBox();
    loginName_->setObjectName(QStringLiteral("commentLoginNameBox"));
    keepTool_ = new QCheckBox();
    keepTool_->setObjectName(QStringLiteral("commentKeepToolBox"));
    name_ = new QLineEdit();
    name_->setObjectName(QStringLiteral("identityNameEdit"));
    title_ = new QLineEdit();
    title_->setObjectName(QStringLiteral("identityTitleEdit"));
    organization_ = new QLineEdit();
    organization_->setObjectName(QStringLiteral("identityOrganizationEdit"));
    loginHint_ = new QLabel();
    loginHint_->setObjectName(QStringLiteral("identityLoginHint"));
    loginHint_->setWordWrap(true);

    sections_ << sheet_->addSection(QString());
    rows_ << sheet_->addRow(QString(), font_);
    rows_ << sheet_->addRow(QString(), size_);
    sheet_->addWidget(hover_);
    sheet_->addWidget(connector_);
    sections_ << sheet_->addSection(QString());
    sheet_->addWidget(openOnCreate_);
    sheet_->addWidget(loginName_);
    sheet_->addWidget(keepTool_);
    sections_ << sheet_->addSection(QString());
    rows_ << sheet_->addRow(QString(), name_);
    rows_ << sheet_->addRow(QString(), title_);
    rows_ << sheet_->addRow(QString(), organization_);
    sheet_->addWidget(loginHint_);
    retranslate();
}

QString CommentingPage::title() const { return tr("Комментирование"); }

void CommentingPage::load(const Settings& settings) {
    font_->setCurrentFont(QFont(settings.get(settings::kCommentPopupFont)));
    size_->setValue(settings.get(settings::kCommentPopupFontSize));
    hover_->setChecked(settings.get(settings::kCommentPopupOnHover));
    connector_->setChecked(settings.get(settings::kCommentConnectorLines));
    openOnCreate_->setChecked(settings.get(settings::kCommentOpenPopupOnCreate));
    loginName_->setChecked(settings.get(settings::kCommentAlwaysLoginName));
    keepTool_->setChecked(settings.get(settings::kCommentKeepTool));
    name_->setText(settings.get(settings::kCommentIdentityName));
    title_->setText(settings.get(settings::kCommentIdentityTitle));
    organization_->setText(settings.get(settings::kCommentIdentityOrganization));
}

void CommentingPage::apply(Settings& settings) {
    settings.set(settings::kCommentPopupFont, font_->currentFont().family());
    settings.set(settings::kCommentPopupFontSize, size_->value());
    settings.set(settings::kCommentPopupOnHover, hover_->isChecked());
    settings.set(settings::kCommentConnectorLines, connector_->isChecked());
    settings.set(settings::kCommentOpenPopupOnCreate, openOnCreate_->isChecked());
    settings.set(settings::kCommentAlwaysLoginName, loginName_->isChecked());
    settings.set(settings::kCommentKeepTool, keepTool_->isChecked());
    settings.set(settings::kCommentIdentityName, name_->text().trimmed());
    settings.set(settings::kCommentIdentityTitle, title_->text().trimmed());
    settings.set(settings::kCommentIdentityOrganization, organization_->text().trimmed());
}

void CommentingPage::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        retranslate();
    }
    PreferencesPage::changeEvent(event);
}

void CommentingPage::retranslate() {
    const QStringList sections = {tr("Просмотр комментариев"), tr("Создание комментариев"),
                                  tr("Личные данные (Identity)")};
    for (int i = 0; i < sections_.size(); ++i) {
        sections_[i]->setText(sections.value(i));
    }
    const QStringList rows = {tr("&Шрифт окна записки:"), tr("&Кегль, пт:"), tr("&Имя:"), tr("&Должность:"),
                              tr("&Организация:")};
    for (int i = 0; i < rows_.size(); ++i) {
        rows_[i]->setText(rows.value(i));
    }
    hover_->setText(tr("Показывать текст комментария при &наведении"));
    connector_->setText(tr("Показывать &линии-связки от комментария к окну записки"));
    openOnCreate_->setText(tr("Открывать окно записки при её &создании"));
    loginName_->setText(tr("Всегда использовать имя &входа в систему как имя автора"));
    keepTool_->setText(tr("Оставлять инструмент комментария &выбранным"));
    loginHint_->setText(
        tr("Имя входа в систему: %1. Автор новых комментариев — оно или имя из личных данных.")
            .arg(QString::fromStdString(annot::loginName())));
}

}  // namespace ot::ui
