// Данные формы (план 6.1, п. 3; эталон 3.6 «Данные», 7.3; forms-35, forms-36, security-36):
//   FormDataExportDialog — «Экспорт данных формы»: формат (Acrobat FDF, XFDF, XML, текст с разделителями
//     табуляции), число полей с данными, файл (по умолчанию — рядом с документом, имя документа с расширением
//     формата; «Обзор…» — нативный «Сохранить как», в тестах — setFileChooser);
//   FormDataReportDialog — итог импорта: принято N из M, списки «Нет в этой форме», «Только для чтения», «Не
//     приняты» (не вариант списка, отказ проверки, кнопки и подписи);
//   confirmFormDataImport — подтверждение импорта с именем файла и числом значений (эталон 7.3, security-36).
// Запись и импорт делает FormFillTool (exportDataTo, importDataFrom) задачами документа.
#pragma once

#include <QDialog>
#include <QString>

#include <functional>

#include "core/forms/data_io.h"

class QButtonGroup;
class QLabel;
class QLineEdit;
class QPushButton;

namespace ot::ui {

class FormDataExportDialog : public QDialog {
    Q_OBJECT
public:
    // documentPath — путь документа (пусто — «Без имени»), values — число значений к записи.
    FormDataExportDialog(const QString& documentPath, int values, QWidget* parent = nullptr);

    forms::DataFormat format() const;
    void setFormat(forms::DataFormat format);
    QString path() const;
    void setPath(const QString& path);
    QPushButton* exportButton() const noexcept { return export_; }
    // Выбор файла «Обзор…» (тесты): пусто — нативный «Сохранить как».
    using FileChooser = std::function<QString(const QString& suggested, forms::DataFormat format)>;
    void setFileChooser(FileChooser chooser) { chooser_ = std::move(chooser); }

    static QString extension(forms::DataFormat format);  // ".fdf", ".xfdf", ".xml", ".txt"
    static QString formatTitle(forms::DataFormat format);

private:
    void browse();
    void updatePathExtension();

    QString documentPath_;
    QButtonGroup* formats_ = nullptr;
    QLineEdit* path_ = nullptr;
    QPushButton* export_ = nullptr;
    FileChooser chooser_;
};

class FormDataReportDialog : public QDialog {
    Q_OBJECT
public:
    FormDataReportDialog(const QString& file, const forms::DataImportReport& report,
                         QWidget* parent = nullptr);
    QLabel* summary() const noexcept { return summary_; }

private:
    QLabel* summary_ = nullptr;
};

// «Импортировать данные формы из «x.fdf» (N значений)?» — true: импортировать.
bool confirmFormDataImport(QWidget* parent, const QString& file, int values);

}  // namespace ot::ui
