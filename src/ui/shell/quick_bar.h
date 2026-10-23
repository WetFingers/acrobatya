// QuickBar — плавающая панель внизу документа (эталон 6.1 «Документ»; основа cloud-38): Выделение, Рука |
// Записка, Выделить текст, Рисовать, Добавить текст, Подпись | Масштаб рамкой. Живые — инструменты просмотра
// (ToolController вида), будущие — серые с подсказкой «появится в версии 0.N». Состав и порядок — список id в
// settings.ini (quickbar/order, «|» — разделитель; настройка панели — 9.1). Однобуквенные клавиши
// (эталон 6.8) видны в подсказках, когда включены (setSingleKeyHints).
#pragma once

#include <QPointer>

#include <vector>

#include "ui/view/tools/view_tool.h"
#include "ui/widgets/floating_bar.h"

namespace ot::ui {

class PageView;

class QuickBar : public FloatingBar {
    Q_OBJECT
public:
    static constexpr char kOrderKey[] = "quickbar/order";

    struct Item {
        const char* id;
        const char* icon;
        const char* title;  // QT_TRANSLATE_NOOP("ot::ui::QuickBar", …)
        int stage;          // 0 — уже работает
        const char* key;    // однобуквенная клавиша эталона 6.8 или пусто
    };
    static const std::vector<Item>& items();
    static QStringList defaultOrder();

    explicit QuickBar(QWidget* parent = nullptr);

    // Связать с видом: кнопка включает инструмент вида, смена инструмента — нажатая кнопка.
    void attach(PageView* view);
    void setSingleKeyHints(bool on);
    bool singleKeyHints() const { return keyHints_; }

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslate();
    void sync(ViewToolId tool);
    void trigger(const QString& id);

    QPointer<PageView> view_;
    bool keyHints_ = false;
};

}  // namespace ot::ui
