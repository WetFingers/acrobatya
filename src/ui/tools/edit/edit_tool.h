// Инструмент «Редактировать PDF» из «Все инструменты» (план 3.4; эталон 3.9 «Инструмент», 6.2 п. 1, 6.3
// «Редактирование»; edit-01, edit-02, edit-03, edit-05, edit-06, edit-07): включение — режим «Редактировать»
// вкладки (EditMode в слоте ViewToolId::Edit её PageView; E — однобуквенная клавиша), панель слева 320 —
// «Текст», «Изображение», FORMAT TEXT, ADJUST OBJECTS, «Найти и заменить»; Колонтитулы, Водяной знак, Фон,
// Ссылка, Вложить файл — серые до 0.7. Документ подписан, с /Perms, только для чтения или защищённый просмотр
// — правка отключена: кнопки серые, причина — строкой панели и подсказкой.
#pragma once

#include <QLabel>
#include <QObject>
#include <QPointer>
#include <QString>

#include <vector>

#include "ui/tools/edit/format_panel.h"
#include "ui/tools/tool.h"

class QVBoxLayout;

namespace ot::ui {

class EditMode;
class PageView;
class ToolButton;
class ToolRegistry;

class EditTool : public QObject, public Tool {
    Q_OBJECT
public:
    explicit EditTool(ToolRegistry& registry);
    ~EditTool() override;

    QString id() const override { return QStringLiteral("edit"); }
    QString title() const override;
    QString icon() const override;
    int stage() const override { return 3; }
    void activate(DocumentView& view) override;
    void deactivate() override;
    QWidget* panel() override;
    std::vector<ot::Command> commands() const override;
    // Шрифт нового текста, запасной шрифт, системные шрифты в списке — из «Установки → Редактирование».
    void settingsChanged() override;

    // Режим «Редактировать» вида (ставится в слот при первом обращении).
    static EditMode& modeOf(PageView& view);
    // Режим текущей вкладки инструмента; нет вкладки — nullptr.
    EditMode* mode() const;
    DocumentView* view() const { return view_; }
    // Почему правка отключена (пусто — можно править).
    QString lockReason() const { return lock_; }

    // Панель: кнопки и строка причины (тесты, сцены).
    ToolButton* textButton() const { return text_; }
    ToolButton* addTextButton() const { return addText_; }
    ToolButton* addImageButton() const { return addImage_; }
    // ADJUST OBJECTS: кнопки выбранной картинки по objectName (imageRotateCw, imageCrop…).
    ToolButton* objectButton(const QString& name) const;
    FormatPanel* formatPanel() const { return format_; }
    QLabel* lockLabel() const { return lockLabel_; }

    // «Текст» — войти в режим правки текста вкладки.
    void editText();

signals:
    void lockChanged(const QString& reason);

private:
    void buildPanel();
    void updateLock();
    void applyLock();
    void syncButtons();
    void updateDocumentFonts();
    ToolButton* addRow(QVBoxLayout* column, const QString& icon, const QString& label);
    void addCaption(QVBoxLayout* column, const QString& text);

    ToolRegistry& registry_;
    QPointer<DocumentView> view_;
    QPointer<QWidget> panel_;
    QPointer<ToolButton> text_;
    QPointer<ToolButton> addText_;
    QPointer<ToolButton> addImage_;
    QPointer<ToolButton> replace_;
    QList<QPointer<ToolButton>> objects_;
    QPointer<FormatPanel> format_;
    QPointer<QLabel> lockLabel_;
    QString lock_;
    QString docLock_;  // подпись или /Perms (ответ задачи)
    quint64 lockRequest_ = 0;
    std::vector<QMetaObject::Connection> connections_;  // с текущей вкладкой
};

}  // namespace ot::ui
