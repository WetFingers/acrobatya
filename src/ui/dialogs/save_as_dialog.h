// «Сохранить как» с параметрами записи (план 2.5; эталон 4.4 «Сохранить как…»; security-09): файл (поле и
// «Обзор…» — нативный диалог), способ записи — «Как решит программа» (Saver::Auto: инкремент для лёгких
// правок, полная для тяжёлых), «Дописать изменения» (инкрементально), «Записать файл заново» (полностью); для
// полной записи — «Потоки объектов» (меньше файл), «Сжимать изображения», «Подмножества шрифтов». Подписанный
// документ — подсказка: полная запись сделает подписи недействительными (пишется только копия). «Плоская
// копия» (5.3; эталон 3.5): комментарии и Fill & Sign впекаются в содержимое страниц новой копии
// (SaveOptions::flattenAnnotations — всегда копия и полная запись), документ остаётся как есть. Пункт
// «Быстрый просмотр в Интернете» сюда добавит его владелец (9.3).
#pragma once

#include <QDialog>

#include <functional>

#include "core/doc/saver.h"

class QCheckBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;

namespace ot::ui {

class SaveAsDialog : public QDialog {
    Q_OBJECT
public:
    // Выбор файла (по умолчанию — нативный QFileDialog::getSaveFileName; тесты подставляют свой).
    using FileChooser = std::function<QString(QWidget* parent, const QString& suggested)>;

    SaveAsDialog(const QString& suggested, bool isSigned, QWidget* parent = nullptr);

    void setFileChooser(FileChooser chooser) { chooser_ = std::move(chooser); }

    QLineEdit* pathEdit() const { return path_; }
    QPushButton* browseButton() const { return browse_; }
    QRadioButton* autoButton() const { return auto_; }
    QRadioButton* incrementalButton() const { return incremental_; }
    QRadioButton* fullButton() const { return full_; }
    QCheckBox* objectStreamsBox() const { return objstms_; }
    QCheckBox* compressImagesBox() const { return images_; }
    QCheckBox* subsetFontsBox() const { return subset_; }
    QCheckBox* flatCopyBox() const { return flat_; }
    QLabel* signedNote() const { return signedNote_; }

    QString path() const;
    void setPath(const QString& path);
    SaveOptions options() const;

protected:
    void changeEvent(QEvent* event) override;

private:
    void browse();
    void updateEnabled();
    void retranslate();

    bool isSigned_ = false;
    FileChooser chooser_;
    QLabel* pathLabel_ = nullptr;
    QLineEdit* path_ = nullptr;
    QPushButton* browse_ = nullptr;
    QLabel* modeLabel_ = nullptr;
    QRadioButton* auto_ = nullptr;
    QRadioButton* incremental_ = nullptr;
    QRadioButton* full_ = nullptr;
    QCheckBox* objstms_ = nullptr;
    QCheckBox* images_ = nullptr;
    QCheckBox* subset_ = nullptr;
    QCheckBox* flat_ = nullptr;
    QLabel* signedNote_ = nullptr;
    QPushButton* ok_ = nullptr;
    QPushButton* cancel_ = nullptr;
};

}  // namespace ot::ui
