// «Настроить внешний вид подписи» — конструктор (план 4.6, п. 1; эталон 3.8 «Диалог подписания» — внешний
// вид; sign-13): название; графика слева — нет / имя крупно / изображение из файла (PNG, JPEG; скан
// рукописной подписи), «только изображение»; текст справа — имя, причина, место, дата, различающееся имя
// (DN), ярлыки строк; логотип фоном; язык текста. Предпросмотр — core/sign/appearance.h previewAppearance на
// образце данных (имя удостоверения, причина). Хранение списка — services/sign_settings.h (зовущий).
#pragma once

#include <QDialog>
#include <QPixmap>
#include <QSizeF>

#include <functional>

#include "core/sign/appearance.h"
#include "services/sign_settings.h"

class QButtonGroup;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;

namespace ot::ui {

class PropertySheet;

// Растр предпросмотра вида по рамке sizePt (пункты) с ярлыками интерфейса: на экране — 96 dpi × zoom (рамка
// 72 pt — 96 × zoom логических px), чётко при devicePixelRatio; картинка не читается — пусто.
QPixmap appearancePreview(sign::SignatureAppearance look, const sign::AppearanceData& data, QSizeF sizePt,
                          qreal devicePixelRatio, double zoom = 1.0);

class AppearanceDialog : public QDialog {
    Q_OBJECT
public:
    using FileChooser = std::function<QString(QWidget* parent, const QString& title)>;

    AppearanceDialog(NamedAppearance initial, sign::AppearanceData sample, QWidget* parent = nullptr);

    NamedAppearance appearance() const;
    void setFileChooser(FileChooser chooser) { chooser_ = std::move(chooser); }
    void setImage(const QString& path);
    void setLogo(const QString& path);

    QLineEdit* nameField() const { return name_; }
    QRadioButton* graphicButton(sign::AppearanceGraphic graphic) const;
    QCheckBox* imageOnlyBox() const { return imageOnly_; }
    QCheckBox* textBox(const QString& key) const;  // name, reason, location, date, dn, labels
    QComboBox* languageBox() const { return language_; }
    QLabel* previewLabel() const { return preview_; }
    QLabel* errorLabel() const { return error_; }
    QPushButton* saveButton() const { return save_; }

    // «Сохранить»: имя и картинка для «Изображение» обязательны.
    void submit();

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslate();
    void refresh();
    QString choose(const QString& title);

    sign::AppearanceData sample_;
    QString imagePath_;
    QString logoPath_;
    FileChooser chooser_;
    PropertySheet* sheet_ = nullptr;
    QLineEdit* name_ = nullptr;
    QButtonGroup* graphic_ = nullptr;
    QRadioButton* none_ = nullptr;
    QRadioButton* byName_ = nullptr;
    QRadioButton* image_ = nullptr;
    QPushButton* imageButton_ = nullptr;
    QLabel* imageLabel_ = nullptr;
    QCheckBox* imageOnly_ = nullptr;
    QCheckBox* textName_ = nullptr;
    QCheckBox* textReason_ = nullptr;
    QCheckBox* textLocation_ = nullptr;
    QCheckBox* textDate_ = nullptr;
    QCheckBox* textDn_ = nullptr;
    QCheckBox* textLabels_ = nullptr;
    QPushButton* logoButton_ = nullptr;
    QPushButton* logoClear_ = nullptr;
    QLabel* logoLabel_ = nullptr;
    QComboBox* language_ = nullptr;
    QLabel* preview_ = nullptr;
    QLabel* error_ = nullptr;
    QPushButton* save_ = nullptr;
    QPushButton* cancel_ = nullptr;
    QList<QLabel*> rows_;
    QList<QLabel*> sections_;
};

}  // namespace ot::ui
