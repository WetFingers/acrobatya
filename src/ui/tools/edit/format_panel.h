// Панель FORMAT TEXT инструмента «Редактировать PDF» (план 3.4, п. 2; эталон 3.9 «Инструмент», 6.2 п. 1;
// edit-03 — UI): семейство (список «В документе» — шрифты блоков страницы, и «Системные» — DirectWrite,
// services/system_fonts_dwrite.h, если включено в Установках), кегль, цвет, Ж/К/Ч, выравнивание,
// межстрочный (доля кегля) и межбуквенный (Tc, пт) интервалы, верхний и нижний индекс. Каретка в блоке —
// правка идёт к выделенному (без выделения — к стилю набора у каретки; абзацу — выравнивание и интервал);
// каретки нет — поля задают стиль следующего «Добавить текст». Поля показывают стиль у каретки
// (EditMode::caretChanged).
#pragma once

#include <QPointer>
#include <QStringList>
#include <QWidget>

#include "core/edit/paragraph_editor.h"

class QComboBox;

namespace ot::ui {

class ColorSwatch;
class EditMode;
class NumberField;
class ToolButton;

class FormatPanel : public QWidget {
    Q_OBJECT
public:
    explicit FormatPanel(QWidget* parent = nullptr);
    ~FormatPanel() override;

    // Режим вкладки (nullptr — нет документа): поля — по его каретке.
    void setMode(EditMode* mode);
    EditMode* mode() const { return mode_; }
    // Семейства шрифтов документа (раздел «В документе» списка).
    void setDocumentFonts(const QStringList& families);
    void setShowSystemFonts(bool on);
    // Перечитать стиль у каретки.
    void refresh();

    QComboBox* familyBox() const { return family_; }
    NumberField* sizeField() const { return size_; }
    ColorSwatch* colorSwatch() const { return color_; }
    ToolButton* boldButton() const { return bold_; }
    ToolButton* italicButton() const { return italic_; }
    ToolButton* underlineButton() const { return underline_; }
    ToolButton* alignButton(edit::Align align) const;
    ToolButton* superscriptButton() const { return super_; }
    ToolButton* subscriptButton() const { return sub_; }
    NumberField* lineSpacingField() const { return lineSpacing_; }
    NumberField* charSpacingField() const { return charSpacing_; }

protected:
    void changeEvent(QEvent* event) override;

private:
    void rebuildFamilies(const QString& current);
    void apply(const edit::StyleChange& change);
    void retranslate();

    QPointer<EditMode> mode_;
    QMetaObject::Connection caretConnection_;
    QMetaObject::Connection editingConnection_;
    QStringList documentFonts_;
    bool showSystem_ = true;
    QComboBox* family_ = nullptr;
    NumberField* size_ = nullptr;
    ColorSwatch* color_ = nullptr;
    ToolButton* bold_ = nullptr;
    ToolButton* italic_ = nullptr;
    ToolButton* underline_ = nullptr;
    ToolButton* align_[4] = {};
    ToolButton* super_ = nullptr;
    ToolButton* sub_ = nullptr;
    NumberField* lineSpacing_ = nullptr;
    NumberField* charSpacing_ = nullptr;
    class QLabel* lineLabel_ = nullptr;
    class QLabel* charLabel_ = nullptr;
    bool updating_ = false;
};

}  // namespace ot::ui
