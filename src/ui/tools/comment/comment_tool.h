// Инструмент «Добавить комментарии» из «Все инструменты» (план 5.1; эталон 3.4 «Инструмент», 6.2 п. 3;
// comment-01, comment-02, comment-03, comment-42; 5.2 — comment-04, comment-05, comment-09): включение —
// панель слева 320 с действиями группы (Записка, Текстовое поле, Текстовая выноска, Выделить, Подчеркнуть,
// Зачеркнуть, Волнистая, Вставить текст, Заменить текст, Рисовать, Ластик); выбор действия — режим
// CommentMode в слоте ViewToolId::Comment вида вкладки; «Фигуры» — линия, стрелка, прямоугольник, овал,
// многоугольник, облако, ломаная (comment-07); «Штамп и вложение» — Штамп (K, текущий штамп подписью под
// кнопкой), «Палитра штампов…» (ui/dialogs/stamps_palette.h; comment-11, 12, 13, 41) и Вложить файл
// (comment-14). Клавиши — S и Ctrl+6 (записка), J (текстовое поле), U (выделить текст), D, R (прямоугольник),
// K. «Формат текста» (шрифт, кегль, цвет, выравнивание) правит открытый редактор поля, выбранные поля
// FreeText или — без них — стиль следующего поля. Добавлять комментарии нельзя (защищённый просмотр, только
// чтение, не PDF, право /P) — кнопки серые, причина — строкой панели и подсказкой.
#pragma once

#include <QLabel>
#include <QObject>
#include <QPointer>
#include <QString>

#include <map>
#include <vector>

#include <QComboBox>

#include "ui/tools/comment/comment_mode.h"
#include "ui/tools/tool.h"
#include "ui/widgets/color_swatch.h"
#include "ui/widgets/number_field.h"
#include "ui/widgets/segmented_bar.h"
#include "ui/widgets/tool_button.h"

class QFormLayout;
class QVBoxLayout;

namespace ot::ui {

class PageView;
class StampsPalette;

class ToolRegistry;

class CommentTool : public QObject, public Tool {
    Q_OBJECT
public:
    explicit CommentTool(ToolRegistry& registry);
    ~CommentTool() override;

    QString id() const override { return QStringLiteral("comment"); }
    QString title() const override;
    QString icon() const override;
    int stage() const override { return 5; }
    void activate(DocumentView& view) override;
    void deactivate() override;
    QWidget* panel() override;
    std::vector<ot::Command> commands() const override;
    void settingsChanged() override;

    // Режим комментариев вида (ставится в слот при первом обращении).
    static CommentMode& modeOf(PageView& view);
    CommentMode* mode() const;
    DocumentView* view() const;
    QString lockReason() const { return lock_; }

    // Включить действие на вкладке инструмента: режим вида + кнопка панели.
    void start(CommentAction action);
    ToolButton* actionButton(CommentAction action) const;
    QLabel* lockLabel() const { return lockLabel_; }
    // Стиль нового комментария текущего действия (comment-08, умолчания): цвет, толщина (карандаш),
    // непрозрачность, значок записки; «Закрепить» (comment-42) — инструмент остаётся после действия
    // (commenting/keepToolSelected).
    ColorSwatch* styleColor() const { return color_; }
    NumberField* styleWidth() const { return width_; }
    NumberField* styleOpacity() const { return opacity_; }
    QComboBox* noteIcon() const { return icon_; }
    ToolButton* pinButton() const { return pin_; }
    // «Формат текста» (5.2, comment-09).
    QComboBox* fontBox() const { return font_; }
    NumberField* fontSizeField() const { return fontSize_; }
    ColorSwatch* textColorSwatch() const { return textColor_; }
    SegmentedBar* alignBar() const { return align_; }
    // Фигуры (5.2, comment-07): заливка /IC, концы линии /LE.
    ColorSwatch* fillSwatch() const { return fill_; }
    QComboBox* lineStartBox() const { return lineStart_; }
    QComboBox* lineEndBox() const { return lineEnd_; }
    // Штамп (5.2): палитра — окно-модальный диалог (WA_DeleteOnClose); выбор штампа или картинки буфера сразу
    // включает действие «Штамп»; «Личные данные…» — Установки → Комментирование. Подпись — текущий штамп.
    StampsPalette* showStampPalette();
    QLabel* stampLabel() const { return stampLabel_; }

signals:
    void lockChanged(const QString& reason);

private:
    void buildPanel();
    void updateLock();
    void syncButtons();
    ToolButton* addRow(QVBoxLayout* column, const QString& icon, const QString& label);
    void addCaption(QVBoxLayout* column, const QString& text);
    void buildStyle(QVBoxLayout* column);
    void loadStyle();
    void saveStyle();
    void setPinned(bool pinned);
    void buildFormat(QVBoxLayout* column);
    void formatChanged();
    void loadFormatFromSelection();
    void updateStampLabel();
    static bool textAction(CommentAction action) noexcept;

    ToolRegistry& registry_;
    QPointer<DocumentView> view_;
    QPointer<QWidget> panel_;
    std::map<CommentAction, QPointer<ToolButton>> buttons_;
    QPointer<QLabel> lockLabel_;
    QString lock_;
    QPointer<ColorSwatch> color_;
    QPointer<NumberField> width_;
    QPointer<NumberField> opacity_;
    QPointer<QComboBox> icon_;
    QPointer<ToolButton> pin_;
    QPointer<QLabel> colorLabel_;
    QPointer<QLabel> widthLabel_;
    QPointer<QComboBox> font_;
    QPointer<NumberField> fontSize_;
    QPointer<ColorSwatch> textColor_;
    QPointer<SegmentedBar> align_;
    QPointer<QFormLayout> styleForm_;
    QPointer<ColorSwatch> fill_;
    QPointer<QComboBox> lineStart_;
    QPointer<QComboBox> lineEnd_;
    QPointer<QLabel> stampLabel_;
    QPointer<ToolButton> paletteButton_;
    std::vector<annot::AnnotRef> formatTargets_;  // выбранные поля FreeText
    bool loadingStyle_ = false;
    std::vector<QMetaObject::Connection> connections_;
};

}  // namespace ot::ui
