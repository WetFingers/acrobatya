// «Установки → Комментирование» (план 5.1, п. 4; эталон 3.4, 3.18; comment-24, comment-42): просмотр —
// шрифт и кегль окна записки, текст комментария при наведении, линии-связки от значка к окну; создание —
// открывать окно при создании записки, «Всегда использовать имя входа в систему как имя автора», оставлять
// инструмент выбранным; Identity — имя, должность, организация (автор /T новых комментариев, если имя входа
// не выбрано). Ключи commenting/… (services/settings_keys.h).
#pragma once

#include <QList>

#include "ui/shell/preferences/preferences_page.h"

class QCheckBox;
class QFontComboBox;
class QLabel;
class QLineEdit;
class QSpinBox;

namespace ot::ui {

class PropertySheet;

class CommentingPage : public PreferencesPage {
    Q_OBJECT
public:
    explicit CommentingPage(QWidget* parent = nullptr);

    QString categoryId() const override { return QStringLiteral("commenting"); }
    QString title() const override;
    void load(const Settings& settings) override;
    void apply(Settings& settings) override;

    QFontComboBox* fontBox() const { return font_; }
    QSpinBox* fontSizeBox() const { return size_; }
    QCheckBox* hoverBox() const { return hover_; }
    QCheckBox* connectorBox() const { return connector_; }
    QCheckBox* openOnCreateBox() const { return openOnCreate_; }
    QCheckBox* loginNameBox() const { return loginName_; }
    QCheckBox* keepToolBox() const { return keepTool_; }
    QLineEdit* nameEdit() const { return name_; }
    QLineEdit* titleEdit() const { return title_; }
    QLineEdit* organizationEdit() const { return organization_; }

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslate();

    PropertySheet* sheet_ = nullptr;
    QFontComboBox* font_ = nullptr;
    QSpinBox* size_ = nullptr;
    QCheckBox* hover_ = nullptr;
    QCheckBox* connector_ = nullptr;
    QCheckBox* openOnCreate_ = nullptr;
    QCheckBox* loginName_ = nullptr;
    QCheckBox* keepTool_ = nullptr;
    QLineEdit* name_ = nullptr;
    QLineEdit* title_ = nullptr;
    QLineEdit* organization_ = nullptr;
    QLabel* loginHint_ = nullptr;
    QList<QLabel*> sections_;
    QList<QLabel*> rows_;
};

}  // namespace ot::ui
