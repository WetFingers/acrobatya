// «Свойства комментария» (план 5.2, п. 3; эталон 3.4 «Свойства комментария», 6.3; comment-08): вкладки «Вид»
// (поля по типу: цвет, заливка, толщина и стиль линии с облаком, непрозрачность, значок записки и вложения,
// концы линии, шрифт, кегль, цвет и выравнивание текстового поля), «Общие» (автор, тема, тип, дата изменения)
// и «История проверок» (статусы рецензии — сессия 5.4; пока пусто), внизу — «Заблокировано» (/F Locked) и
// «Сделать свойствами по умолчанию» (стиль инструмента этого типа — следующая аннотация получит его). Данные
// — задачей сессии (до загрузки диалог серый), «ОК» пишет только изменённое одним шагом «Set annotation
// properties»; документ только для чтения — поля только для просмотра. Открывается окно-модально (open(),
// WA_DeleteOnClose) — тесты и --script не стоят на exec().
#pragma once

#include <QDialog>
#include <QPointer>

#include <optional>

#include "core/annot/annots.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTabWidget;
class QTreeWidget;

namespace ot {
class DocumentSession;
}

namespace ot::ui {

class ColorSwatch;
class NumberField;
class PageView;
class SegmentedBar;

class AnnotPropsDialog : public QDialog {
    Q_OBJECT
public:
    enum Tab { Appearance, General, History };
    // Стиль линии: сплошная, штрихи и облако (у прямоугольника, овала и многоугольника).
    enum LineStyle { Solid, Dashed, Dotted, Cloud1, Cloud2 };

    AnnotPropsDialog(PageView& view, const annot::AnnotRef& ref, QWidget* parent = nullptr);
    // Открыть над окном вида (окно-модально, удаляется при закрытии).
    static AnnotPropsDialog* openFor(PageView& view, const annot::AnnotRef& ref);

    void showTab(Tab tab);
    bool loaded() const noexcept { return loaded_; }
    const annot::AnnotRef& ref() const noexcept { return ref_; }
    const annot::AnnotProps& props() const noexcept { return props_; }
    // Название типа для заголовка и «Общих» («Текстовое поле», «Облако»…).
    static QString typeTitle(const annot::AnnotProps& props);

    QTabWidget* tabs() const { return tabs_; }
    ColorSwatch* colorSwatch() const { return color_; }
    ColorSwatch* fillSwatch() const { return fill_; }
    NumberField* widthField() const { return width_; }
    QComboBox* lineStyleBox() const { return lineStyle_; }
    NumberField* opacityField() const { return opacity_; }
    QComboBox* iconBox() const { return icon_; }
    QComboBox* lineStartBox() const { return lineStart_; }
    QComboBox* lineEndBox() const { return lineEnd_; }
    QComboBox* fontBox() const { return font_; }
    NumberField* fontSizeField() const { return fontSize_; }
    ColorSwatch* textColorSwatch() const { return textColor_; }
    SegmentedBar* alignBar() const { return align_; }
    QLineEdit* authorEdit() const { return author_; }
    QLineEdit* subjectEdit() const { return subject_; }
    QLabel* modifiedLabel() const { return modified_; }
    QLabel* historyLabel() const { return history_; }
    // 5.4: статусы записи (дата, автор, статус) — по дереву документа; пусто — подпись historyLabel.
    QTreeWidget* historyList() const { return historyList_; }
    QCheckBox* lockedBox() const { return locked_; }
    QCheckBox* makeDefaultBox() const { return makeDefault_; }
    QPushButton* okButton() const { return ok_; }
    // Строка поля «Вид» видна (по типу аннотации).
    bool rowVisible(QWidget* editor) const;
    // Изменённые поля (то, что «ОК» запишет).
    annot::AnnotPatch patch() const;

signals:
    void loadedChanged();
    // Записано (после ответа сессии) — или ничего не менялось.
    void applied(const ot::annot::AnnotRef& ref);

public slots:
    void accept() override;

private:
    QWidget* buildAppearance();
    QWidget* buildGeneral();
    QWidget* buildHistory();
    void fill(const annot::AnnotProps& props);
    void setRowVisible(QWidget* editor, bool visible);
    LineStyle lineStyleOf(const annot::AnnotProps& p) const;
    // Свойства с правкой диалога (для «По умолчанию»).
    annot::AnnotProps edited() const;

    PageView& view_;
    QPointer<DocumentSession> session_;
    annot::AnnotRef ref_;
    annot::AnnotProps props_;
    bool loaded_ = false;
    QTabWidget* tabs_ = nullptr;
    ColorSwatch* color_ = nullptr;
    QLabel* colorLabel_ = nullptr;
    ColorSwatch* fill_ = nullptr;
    NumberField* width_ = nullptr;
    QLabel* widthLabel_ = nullptr;
    QComboBox* lineStyle_ = nullptr;
    NumberField* opacity_ = nullptr;
    QComboBox* icon_ = nullptr;
    QComboBox* lineStart_ = nullptr;
    QComboBox* lineEnd_ = nullptr;
    QComboBox* font_ = nullptr;
    NumberField* fontSize_ = nullptr;
    ColorSwatch* textColor_ = nullptr;
    SegmentedBar* align_ = nullptr;
    QLabel* textSection_ = nullptr;
    QLineEdit* author_ = nullptr;
    QLineEdit* subject_ = nullptr;
    QLabel* type_ = nullptr;
    QLabel* modified_ = nullptr;
    QLabel* history_ = nullptr;
    QTreeWidget* historyList_ = nullptr;
    void loadHistory();
    QCheckBox* locked_ = nullptr;
    QCheckBox* makeDefault_ = nullptr;
    QPushButton* ok_ = nullptr;
    std::vector<std::pair<QWidget*, QLabel*>> rows_;  // редактор → подпись строки «Вида»
};

}  // namespace ot::ui
