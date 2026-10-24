// «Установки → Печать» (план 2.6; эталон 3.18 «Печать», 3.3; view-37, create-16): принтер по умолчанию
// диалога «Печать» (системный или выбранный), качество — потолок dpi растра (у «Microsoft Print to PDF» 1200
// dpi — 300), масштаб по умолчанию (подогнать / реальный размер / уменьшить большие); «Печать в PDF» (замена
// настроек принтера Adobe PDF) — папка, шаблон имени файла ({name} — имя документа) и «Спрашивать имя файла».
#pragma once

#include <QList>
#include <functional>

#include "ui/shell/preferences/preferences_page.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;

namespace ot::ui {

class PropertySheet;

class PrintPage : public PreferencesPage {
    Q_OBJECT
public:
    using FolderChooser = std::function<QString(QWidget* parent, const QString& current)>;

    explicit PrintPage(QWidget* parent = nullptr);

    QString categoryId() const override { return QStringLiteral("print"); }
    QString title() const override;
    void load(const Settings& settings) override;
    void apply(Settings& settings) override;

    void setFolderChooser(FolderChooser chooser) { chooser_ = std::move(chooser); }

    QComboBox* printerBox() const { return printer_; }
    QComboBox* qualityBox() const { return quality_; }
    QComboBox* scaleBox() const { return scale_; }
    QLineEdit* folderEdit() const { return folder_; }
    QPushButton* browseButton() const { return browse_; }
    QLineEdit* nameEdit() const { return name_; }
    QCheckBox* askBox() const { return ask_; }

protected:
    void changeEvent(QEvent* event) override;

private:
    void browse();
    void retranslate();

    PropertySheet* sheet_ = nullptr;
    QList<QLabel*> sections_;
    QList<QLabel*> rows_;
    FolderChooser chooser_;
    QComboBox* printer_ = nullptr;
    QComboBox* quality_ = nullptr;
    QComboBox* scale_ = nullptr;
    QLineEdit* folder_ = nullptr;
    QPushButton* browse_ = nullptr;
    QLineEdit* name_ = nullptr;
    QLabel* nameHint_ = nullptr;
    QCheckBox* ask_ = nullptr;
};

}  // namespace ot::ui
