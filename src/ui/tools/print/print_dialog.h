// Диалог «Печать» (Ctrl+P; план 2.6; эталон 3.3, 6.3 «OCR, сканирование, печать»; view-37…43, create-16):
// свой, как у Acrobat, с предпросмотром справа. Группы: принтер (QPrinterInfo; по умолчанию — print/printer
// или системный) и «Свойства…» — нативный QPrintDialog на том же QPrinter (лотки, качество драйвера); копии и
// «Разобрать по копиям», «Печать в оттенках серого», «Печать как изображение» с dpi (view-43); «Страницы для
// печати» — все, текущая, диапазон «1-3, 7, 12-» с логическими номерами, выделенные миниатюры (view-10) или
// страницы закладки (view-11), нечётные/чётные, обратный порядок; «Размер и обработка страниц» — Размер
// (подогнать, реальный, уменьшить большие, масштаб), Постер (масштаб плиток, перекрытие, метки обреза,
// подписи, только большие), Несколько (2/4/6/9/16, порядок, границы, автоповорот), Буклет (обе стороны /
// лицевая / оборотная, переплёт, автоповорот; принтер без дуплекса — «только лицевая» и подсказка);
// ориентация; двусторонняя печать (длинный/короткий край); «Комментарии и формы» (документ и пометки /
// документ / только поля); «Сводка комментариев» — серая до 0.9. План считается сразу (core/print +
// services/print_job.h): 0 страниц или неверный диапазон — «Печать» серая. «Печать» — PrintRunner в фоне;
// «Microsoft Print to PDF» — файл по «Установки → Печать» (папка и шаблон имени, «спрашивать»), без окна
// драйвера (GDI).
#pragma once

#include <QDialog>
#include <QList>
#include <QPointer>

#include <functional>
#include <memory>

#include "services/print_job.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPrinter;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QStackedWidget;

namespace ot {
class DocumentSession;
}

namespace ot::ui {

class DocumentView;
class MessageBar;
class NumberField;
class PrintPreview;
class PrintRunner;
class SegmentedBar;

class PrintDialog : public QDialog {
    Q_OBJECT
public:
    // Имя файла «Печать в PDF» (по умолчанию — нативный QFileDialog; тесты подставляют свой).
    using FileChooser = std::function<QString(QWidget* parent, const QString& suggested)>;

    // pages — заранее выбранные страницы (миниатюры, закладка): «Выделенные страницы» отмечены; пусто — по
    // выделению миниатюр вкладки (DocumentView::selectedPages).
    explicit PrintDialog(DocumentView& view, QList<int> pages = {}, QWidget* parent = nullptr);
    ~PrintDialog() override;

    void setFileChooser(FileChooser chooser) { chooser_ = std::move(chooser); }

    // Параметры по полям диалога и план (листы) по ним.
    PrintSettings settings() const;
    const PrintPlan& plan() const { return plan_; }
    print::Paper paper() const { return paper_; }
    QString printer() const;
    void setPrinter(const QString& name);
    void setLayoutMode(print::Layout layout);
    // Пересчитать план и предпросмотр сейчас (поля меняют его сами).
    void refresh();
    // «Печать»: задание в фоне (PrintRunner — ребёнок окна-родителя), диалог закрывается. nullptr — печатать
    // нечего или имя файла не выбрано.
    PrintRunner* print();

    QComboBox* printerBox() const { return printer_; }
    QPushButton* propertiesButton() const { return properties_; }
    QSpinBox* copiesBox() const { return copies_; }
    QCheckBox* collateBox() const { return collate_; }
    QCheckBox* grayBox() const { return gray_; }
    QCheckBox* asImageBox() const { return asImage_; }
    QComboBox* imageDpiBox() const { return imageDpi_; }
    QRadioButton* allButton() const { return all_; }
    QRadioButton* currentButton() const { return current_; }
    QRadioButton* rangeButton() const { return range_; }
    QLineEdit* rangeEdit() const { return rangeEdit_; }
    QRadioButton* selectedButton() const { return selected_; }
    QComboBox* subsetBox() const { return subset_; }
    QCheckBox* reverseBox() const { return reverse_; }
    SegmentedBar* layoutBar() const { return layoutBar_; }
    QRadioButton* fitButton() const { return fit_; }
    QRadioButton* actualButton() const { return actual_; }
    QRadioButton* shrinkButton() const { return shrink_; }
    QRadioButton* customScaleButton() const { return customScale_; }
    NumberField* scaleField() const { return scale_; }
    NumberField* tileScaleField() const { return tileScale_; }
    NumberField* overlapField() const { return overlap_; }
    QCheckBox* cutMarksBox() const { return cutMarks_; }
    QCheckBox* labelsBox() const { return labels_; }
    QCheckBox* largeOnlyBox() const { return largeOnly_; }
    QComboBox* perSheetBox() const { return perSheet_; }
    QComboBox* orderBox() const { return order_; }
    QCheckBox* bordersBox() const { return borders_; }
    QCheckBox* nupRotateBox() const { return nupRotate_; }
    QComboBox* bookletSubsetBox() const { return bookletSubset_; }
    QComboBox* bindingBox() const { return binding_; }
    QCheckBox* bookletRotateBox() const { return bookletRotate_; }
    MessageBar* bookletHint() const { return bookletHint_; }
    QRadioButton* autoOrientationButton() const { return autoOrientation_; }
    QRadioButton* portraitButton() const { return portrait_; }
    QRadioButton* landscapeButton() const { return landscape_; }
    QCheckBox* duplexBox() const { return duplex_; }
    QRadioButton* longEdgeButton() const { return longEdge_; }
    QRadioButton* shortEdgeButton() const { return shortEdge_; }
    QComboBox* contentBox() const { return content_; }
    QPushButton* summaryButton() const { return summary_; }
    MessageBar* rangeError() const { return rangeError_; }
    QLabel* infoLabel() const { return info_; }
    PrintPreview* preview() const { return preview_; }
    QPushButton* printButton() const { return print_; }

signals:
    void printStarted(ot::ui::PrintRunner* runner);

protected:
    void changeEvent(QEvent* event) override;

private:
    void build();
    void loadDefaults();
    void printerChanged();
    void showPrinterProperties();
    void updateEnabled();
    void retranslate();
    QString pdfOutputPath();

    QPointer<DocumentView> view_;
    std::shared_ptr<DocumentSession> session_;
    QList<int> pages_;
    FileChooser chooser_;
    std::unique_ptr<QPrinter> qprinter_;  // «Свойства…» (нативный диалог) настраивает его
    print::Paper paper_;
    PrintPlan plan_;
    int maxDpi_ = 300;
    bool duplexSupported_ = false;  // принтер печатает на обеих сторонах (при смене принтера)
    bool useLabels_ = true;
    bool building_ = true;

    QLabel* printerLabel_ = nullptr;
    QComboBox* printer_ = nullptr;
    QPushButton* properties_ = nullptr;
    QLabel* copiesLabel_ = nullptr;
    QSpinBox* copies_ = nullptr;
    QCheckBox* collate_ = nullptr;
    QCheckBox* gray_ = nullptr;
    QCheckBox* asImage_ = nullptr;
    QComboBox* imageDpi_ = nullptr;
    QList<QLabel*> sections_;
    QRadioButton* all_ = nullptr;
    QRadioButton* current_ = nullptr;
    QRadioButton* range_ = nullptr;
    QLineEdit* rangeEdit_ = nullptr;
    QRadioButton* selected_ = nullptr;
    QLabel* subsetLabel_ = nullptr;
    QComboBox* subset_ = nullptr;
    QCheckBox* reverse_ = nullptr;
    MessageBar* rangeError_ = nullptr;
    SegmentedBar* layoutBar_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QRadioButton* fit_ = nullptr;
    QRadioButton* actual_ = nullptr;
    QRadioButton* shrink_ = nullptr;
    QRadioButton* customScale_ = nullptr;
    NumberField* scale_ = nullptr;
    QList<QLabel*> rowLabels_;  // подписи строк страниц «Размер и обработка» — по порядку retranslate
    NumberField* tileScale_ = nullptr;
    NumberField* overlap_ = nullptr;
    QCheckBox* cutMarks_ = nullptr;
    QCheckBox* labels_ = nullptr;
    QCheckBox* largeOnly_ = nullptr;
    QComboBox* perSheet_ = nullptr;
    QComboBox* order_ = nullptr;
    QCheckBox* borders_ = nullptr;
    QCheckBox* nupRotate_ = nullptr;
    QComboBox* bookletSubset_ = nullptr;
    QComboBox* binding_ = nullptr;
    QCheckBox* bookletRotate_ = nullptr;
    MessageBar* bookletHint_ = nullptr;
    QLabel* orientationLabel_ = nullptr;
    QRadioButton* autoOrientation_ = nullptr;
    QRadioButton* portrait_ = nullptr;
    QRadioButton* landscape_ = nullptr;
    QCheckBox* duplex_ = nullptr;
    QRadioButton* longEdge_ = nullptr;
    QRadioButton* shortEdge_ = nullptr;
    QLabel* contentLabel_ = nullptr;
    QComboBox* content_ = nullptr;
    QPushButton* summary_ = nullptr;
    MessageBar* rightsNote_ = nullptr;
    MessageBar* noPrinters_ = nullptr;
    QLabel* info_ = nullptr;
    PrintPreview* preview_ = nullptr;
    QPushButton* print_ = nullptr;
    QPushButton* cancel_ = nullptr;
};

}  // namespace ot::ui
