// ToolRegistry — инструменты «Все инструменты» (эталон 6.2, 6.1 «Левая панель»; view-30): один реестр на
// процесс, порядок — эталона 6.2 (16 id: edit … javascript), прочие — следом в порядке регистрации. add() с
// тем же id заменяет прежний (настоящий инструмент сменяет заглушку); registerBuiltinTools — 16 регистраций
// (зовут main.cpp, сцены и тесты; новая сессия меняет в builtin_tools.cpp одну строку заглушки на свой
// *Tool). Активный инструмент один: activate(id, вкладка) — прежний deactivate(), новый activate(); заглушку
// включить нельзя. Смена вкладки — setView (инструмент переходит на неё). Закреплённые звёздочкой — наверху
// списка, в порядке закрепления; список хранится в settings.ini (tools/pinned). Поток: GUI.
#pragma once

#include <QObject>
#include <QPointer>
#include <QStringList>

#include <memory>
#include <vector>

#include "ui/tools/tool.h"

namespace ot::ui {

class ToolRegistry : public QObject {
    Q_OBJECT
public:
    static constexpr char kPinnedKey[] = "tools/pinned";

    static ToolRegistry& instance();
    // 16 id эталона 6.2 по порядку.
    static const QStringList& builtinOrder();

    ToolRegistry();
    ~ToolRegistry() override;

    Tool* add(std::unique_ptr<Tool> tool);
    bool remove(const QString& id);
    Tool* find(const QString& id) const;
    // Порядок 6.2, затем прочие.
    std::vector<Tool*> tools() const;
    // То же, закреплённые — первыми (список «Все инструменты»).
    std::vector<Tool*> listed() const;
    int count() const { return static_cast<int>(tools_.size()); }

    bool activate(const QString& id, DocumentView* view);
    void deactivate();
    void setView(DocumentView* view);
    Tool* active() const { return active_; }
    DocumentView* view() const;

    QStringList pinned() const;
    bool isPinned(const QString& id) const;
    void setPinned(const QString& id, bool pinned);

signals:
    void toolsChanged();
    void activated(ot::ui::Tool* tool);
    void deactivated();
    void pinnedChanged();

private:
    std::vector<std::unique_ptr<Tool>> tools_;
    Tool* active_ = nullptr;
    QPointer<DocumentView> view_;
};

// 16 инструментов эталона 6.2: сейчас все — заглушки с этапом (живые инструменты просмотра — в QuickBar);
// следующие сессии заменяют строки своими *Tool.
void registerBuiltinTools(ToolRegistry& registry);

}  // namespace ot::ui
