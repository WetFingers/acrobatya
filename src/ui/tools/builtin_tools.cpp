// 16 инструментов «Все инструменты» в порядке эталона 6.2 (иконки — эталон 6.5, kToolIcons). Сессия, которая
// делает инструмент, меняет его строку на registry.add(std::make_unique<<Имя>Tool>()) — оболочка не правится.
#include <QCoreApplication>

#include <memory>

#include "ui/theme/icons.h"
#include "ui/tools/certificate/certificate_tool.h"
#include "ui/tools/comment/comment_tool.h"
#include "ui/tools/edit/edit_tool.h"
#include "ui/tools/fillsign/fillsign_tool.h"
#include "ui/tools/measure/measure_tool.h"
#include "ui/tools/print/print_tool.h"
#include "ui/tools/stub_tool.h"
#include "ui/tools/tool_registry.h"

namespace ot::ui {

StubTool::StubTool(QString id, const char* title, QString icon, int stage)
    : id_(std::move(id)), title_(title), icon_(std::move(icon)), stage_(stage) {}

QString StubTool::title() const { return QCoreApplication::translate("ot::ui::ToolRegistry", title_); }

namespace {

std::unique_ptr<Tool> stub(const char* id, const char* title, int stage) {
    return std::make_unique<StubTool>(QLatin1String(id), title, QLatin1String(toolIconName(id)), stage);
}

}  // namespace

void registerBuiltinTools(ToolRegistry& registry) {
    registry.add(std::make_unique<EditTool>(registry));
    registry.add(stub("organize", QT_TRANSLATE_NOOP("ot::ui::ToolRegistry", "Упорядочить страницы"), 7));
    registry.add(std::make_unique<CommentTool>(registry));
    registry.add(std::make_unique<FillSignTool>(registry));
    registry.add(stub("prepareform", QT_TRANSLATE_NOOP("ot::ui::ToolRegistry", "Подготовить форму"), 6));
    registry.add(stub("create", QT_TRANSLATE_NOOP("ot::ui::ToolRegistry", "Создать PDF"), 7));
    registry.add(stub("combine", QT_TRANSLATE_NOOP("ot::ui::ToolRegistry", "Объединить файлы"), 7));
    registry.add(stub("export", QT_TRANSLATE_NOOP("ot::ui::ToolRegistry", "Экспорт PDF"), 7));
    registry.add(stub("protect", QT_TRANSLATE_NOOP("ot::ui::ToolRegistry", "Защитить"), 7));
    registry.add(stub("compress", QT_TRANSLATE_NOOP("ot::ui::ToolRegistry", "Сжать PDF"), 9));
    registry.add(std::make_unique<CertificateTool>(registry));
    registry.add(stub("scan", QT_TRANSLATE_NOOP("ot::ui::ToolRegistry", "Сканировать и OCR"), 8));
    registry.add(std::make_unique<PrintTool>(registry));
    registry.add(std::make_unique<MeasureTool>(registry));
    registry.add(
        stub("accessibility", QT_TRANSLATE_NOOP("ot::ui::ToolRegistry", "Специальные возможности"), 8));
    registry.add(stub("javascript", QT_TRANSLATE_NOOP("ot::ui::ToolRegistry", "Использовать JavaScript"), 6));
}

}  // namespace ot::ui
