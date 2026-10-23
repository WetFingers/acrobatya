// Инструмент Acrobat для «Все инструменты» (эталон 6.2; view-30): id — имя папки src/ui/tools/<id>/ (edit,
// organize, comment…), название по-русски (tr), иконка Lucide (эталон 6.5), этап, в котором он сделан.
// Заглушка будущего этапа (isStub) видна серой с подсказкой «появится в версии 0.N»; настоящий инструмент
// регистрируется под тем же id и заменяет её (ToolRegistry::add) — оболочка не правится. Активный инструмент
// один (ToolRegistry::activate): activate(DocumentView&) — включить на вкладке (свой ViewTool в
// ToolController, слои), deactivate() — выключить; panel() — виджет левой панели (им владеет инструмент,
// ToolPanel только показывает его), commands() — команды с клавишами для реестра команд (эталон 6.8),
// cursorFor() — курсор над документом, если инструменту нужен свой.
#pragma once

#include <QCursor>
#include <QKeySequence>
#include <QList>
#include <QPointF>
#include <QString>

#include <functional>
#include <optional>
#include <vector>

class QWidget;

namespace ot::ui {

class DocumentView;

// Команда инструмента (id `группа.действие`, клавиши — из таблицы эталона 6.8; однобуквенные — singleKey).
struct ToolCommand {
    QString id;
    QString title;
    QList<QKeySequence> shortcuts;
    std::function<void()> run;
    bool singleKey = false;
};

class Tool {
public:
    Tool() = default;
    virtual ~Tool() = default;
    Tool(const Tool&) = delete;
    Tool& operator=(const Tool&) = delete;

    virtual QString id() const = 0;
    virtual QString title() const = 0;
    virtual QString icon() const = 0;
    // Этап плана, в котором инструмент сделан (у заглушки — в котором появится): подсказка «версия 0.N».
    virtual int stage() const = 0;
    virtual bool isStub() const { return false; }

    virtual void activate(DocumentView& view) { (void)view; }
    virtual void deactivate() {}
    virtual QWidget* panel() { return nullptr; }
    virtual std::vector<ToolCommand> commands() const { return {}; }
    virtual std::optional<QCursor> cursorFor(const DocumentView& view, QPointF viewportPoint) const {
        (void)view;
        (void)viewportPoint;
        return std::nullopt;
    }
};

}  // namespace ot::ui
