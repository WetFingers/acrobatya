// Заглушка инструмента будущего этапа (эталон 6.1 «Левая панель», 6.2): в «Все инструменты» — серая строка с
// иконкой и подсказкой «появится в версии 0.N»; включить её нельзя. Сессия этапа N регистрирует настоящий
// инструмент под тем же id (ToolRegistry::add заменяет заглушку).
#pragma once

#include <QString>

#include "ui/tools/tool.h"

namespace ot::ui {

class StubTool : public Tool {
public:
    // title — исходная строка перевода (QT_TRANSLATE_NOOP в контексте ot::ui::ToolRegistry).
    StubTool(QString id, const char* title, QString icon, int stage);

    QString id() const override { return id_; }
    QString title() const override;
    QString icon() const override { return icon_; }
    int stage() const override { return stage_; }
    bool isStub() const override { return true; }

private:
    QString id_;
    const char* title_;
    QString icon_;
    int stage_;
};

}  // namespace ot::ui
